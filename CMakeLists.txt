cmake_minimum_required(VERSION 3.20)
project(unllm CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)
file(GLOB UNLLM_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER UNLLM_SOURCES EXCLUDE REGEX "logic_bridge[.]cpp$")
add_library(unllm STATIC ${UNLLM_SOURCES})
target_include_directories(unllm PUBLIC include)
target_link_libraries(unllm PUBLIC sqlite3 Threads::Threads)

# Symbolic upgrade dependencies live in this checkout. Legacy builds can turn
# this off explicitly; a requested symbolic build must not silently omit them.
option(UNLLM_SYMBOLIC "Build the symbolic upgrade and native reasoning bridge" ON)
option(UNLLM_PACKAGE_NATIVE "Build and install the public CLI with the C++ symbolic runtime" ${UNLLM_SYMBOLIC})
if(UNLLM_PACKAGE_NATIVE AND NOT UNLLM_SYMBOLIC)
  message(FATAL_ERROR "UNLLM_PACKAGE_NATIVE requires UNLLM_SYMBOLIC")
endif()
if(UNLLM_SYMBOLIC)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
  target_compile_definitions(unllm PUBLIC UNLLM_SYMBOLIC_READY UNLLM_UPGRADE_ROOT="${CMAKE_SOURCE_DIR}")
  target_link_libraries(unllm PUBLIC PkgConfig::JSONC)
endif()
if(UNLLM_SYMBOLIC AND NOT UNLLM_PACKAGE_NATIVE)
  find_path(SWIPL_INCLUDE_DIR SWI-Prolog.h
    HINTS ${CMAKE_SOURCE_DIR}/.deps/native/usr/lib/swipl/include)
  find_library(SWIPL_LIBRARY NAMES swipl
    HINTS ${CMAKE_SOURCE_DIR}/.deps/native/usr/lib/swipl/lib/x86_64-linux)
  find_program(SOUFFLE_EXECUTABLE souffle HINTS ${CMAKE_SOURCE_DIR}/.deps/bin)
  find_path(SOUFFLE_INCLUDE_DIR souffle/SouffleInterface.h
    HINTS ${CMAKE_SOURCE_DIR}/.deps/native/usr/include)
  if(NOT SWIPL_INCLUDE_DIR OR NOT SWIPL_LIBRARY OR NOT SOUFFLE_EXECUTABLE OR NOT SOUFFLE_INCLUDE_DIR)
    message(FATAL_ERROR "Symbolic dependencies missing. Run tools/setup_upgrade.py, or explicitly set UNLLM_SYMBOLIC=OFF.")
  endif()
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/symbolic)
  set(BACKGROUND_CPP ${CMAKE_BINARY_DIR}/symbolic/background.cpp)
  add_custom_command(OUTPUT ${BACKGROUND_CPP}
    COMMAND ${SOUFFLE_EXECUTABLE} -t none -g ${BACKGROUND_CPP}
      ${CMAKE_SOURCE_DIR}/reasoning/souffle/background.dl
    DEPENDS reasoning/souffle/background.dl
    VERBATIM)
  add_library(unllm_logic SHARED src/logic_bridge.cpp ${BACKGROUND_CPP})
  # Souffle 2.5's public generated-code headers target C++17. The host
  # application and generated user programs retain C++20.
  set_target_properties(unllm_logic PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_compile_definitions(unllm_logic PRIVATE __EMBEDDED_SOUFFLE__
    UNLLM_PROLOG_RULES="${CMAKE_SOURCE_DIR}/reasoning/prolog/rules.pl")
  target_include_directories(unllm_logic PRIVATE include ${SWIPL_INCLUDE_DIR} ${SOUFFLE_INCLUDE_DIR})
  target_link_libraries(unllm_logic PRIVATE PkgConfig::JSONC ${SWIPL_LIBRARY} ${CMAKE_DL_LIBS})
  target_compile_options(unllm_logic PRIVATE -Wno-unused-result)
  target_compile_definitions(unllm PUBLIC UNLLM_LOGIC_LIBRARY="$<TARGET_FILE:unllm_logic>")
  add_dependencies(unllm unllm_logic)
endif()

# The native package implements the ranked search ABI in C++ with Xapian.
option(UNLLM_TANTIVY "Enable ranked search for knowledge packs" ON)
if(NOT UNLLM_PACKAGE_NATIVE)
  find_program(CARGO cargo HINTS $ENV{HOME}/.cargo/bin)
endif()
if(UNLLM_PACKAGE_NATIVE AND UNLLM_TANTIVY)
  pkg_check_modules(XAPIAN REQUIRED IMPORTED_TARGET xapian-core)
  find_package(ICU REQUIRED COMPONENTS uc)
  add_library(unllm_ranked STATIC src/search/ranked.cpp)
  target_include_directories(unllm_ranked PUBLIC include)
  target_link_libraries(unllm_ranked PRIVATE PkgConfig::XAPIAN ICU::uc PkgConfig::JSONC sqlite3)
  add_executable(unllm_index native/index.cpp)
  target_link_libraries(unllm_index PRIVATE unllm_ranked PkgConfig::XAPIAN)
  target_link_libraries(unllm PRIVATE unllm_ranked)
  target_compile_definitions(unllm PUBLIC UNLLM_HAVE_TANTIVY=1)
  install(TARGETS unllm_index RUNTIME DESTINATION bin)
elseif(UNLLM_TANTIVY AND CARGO)
  set(UNLLM_SEARCH_LIB ${CMAKE_CURRENT_SOURCE_DIR}/search/target/release/libunllm_search.a)
  add_custom_command(OUTPUT ${UNLLM_SEARCH_LIB}
    COMMAND ${CARGO} build --release --quiet
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/search
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/search/src/lib.rs ${CMAKE_CURRENT_SOURCE_DIR}/search/Cargo.toml
    COMMENT "Building the Tantivy search helper")
  add_custom_target(unllm_search_rs DEPENDS ${UNLLM_SEARCH_LIB})
  add_dependencies(unllm unllm_search_rs)
  target_compile_definitions(unllm PUBLIC UNLLM_HAVE_TANTIVY=1)
  target_link_libraries(unllm PUBLIC ${UNLLM_SEARCH_LIB} ${CMAKE_DL_LIBS} m)
endif()

# The reader (a small trained model run with ONNX Runtime on the CPU). Built
# when ONNX Runtime is installed; without it, answers come from unllm's own
# rules only.
option(UNLLM_ONNX "Read passages with the reader model (needs ONNX Runtime)" ON)
find_package(onnxruntime QUIET)
# UNLLM_ORT_LIBDIR: a folder holding another ONNX Runtime (libonnxruntime.so.1
# and its providers) to link and load instead of the packaged one, with the
# packaged headers: the Python bench's GPU build, for example, when the
# packaged GPU provider does not load (2026-10: built for an older cuDNN).
set(UNLLM_ORT_LIBDIR "" CACHE PATH "Folder of the ONNX Runtime libraries to link and load")
if(UNLLM_ONNX AND onnxruntime_FOUND)
  target_compile_definitions(unllm PUBLIC UNLLM_HAVE_ONNX=1)
  if(UNLLM_ORT_LIBDIR)
    get_target_property(ORT_INC onnxruntime::onnxruntime INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(unllm PUBLIC ${ORT_INC})
    target_link_libraries(unllm PUBLIC ${UNLLM_ORT_LIBDIR}/libonnxruntime.so.1)
    target_link_options(unllm INTERFACE "-Wl,-rpath,${UNLLM_ORT_LIBDIR}")
  else()
    target_link_libraries(unllm PUBLIC onnxruntime::onnxruntime)
  endif()
endif()

# Look inside installed knowledge packs (list, query, bench).
option(UNLLM_RERANKER "Order found passages with the cross-encoder reranker (needs ONNX Runtime and sentencepiece)" ON)
# The static sentencepiece built from source into ~/.local is preferred: the
# packaged shared one (2026-10) was linked against an older abseil and does
# not load.
find_library(SENTENCEPIECE_LIB NAMES libsentencepiece.a sentencepiece HINTS $ENV{HOME}/.local/lib)
find_path(SENTENCEPIECE_INC sentencepiece_processor.h HINTS $ENV{HOME}/.local/include)
if(UNLLM_RERANKER AND onnxruntime_FOUND AND SENTENCEPIECE_LIB AND SENTENCEPIECE_INC)
  target_compile_definitions(unllm PUBLIC UNLLM_HAVE_RERANKER=1)
  target_include_directories(unllm PRIVATE ${SENTENCEPIECE_INC})  # only reranker.cpp needs it; PUBLIC put ~/.local/include ahead of ours for dependants
  target_link_libraries(unllm PUBLIC ${SENTENCEPIECE_LIB})
  # The archive carries its own protobuf; kept private, or ONNX Runtime binds
  # to it instead of the system's and crashes on start.
  target_link_options(unllm INTERFACE -Wl,--exclude-libs,libsentencepiece.a)
endif()

add_executable(unllm_rerank tools/unllm_rerank.cpp)
target_link_libraries(unllm_rerank PRIVATE unllm)

add_executable(unllm_pack tools/unllm_pack.cpp)
target_link_libraries(unllm_pack PRIVATE unllm)

# The vector index's maintenance: a shard embedded and encoded with the index's own tables, the index audited
# against the packs, and rewritten with shards dropped or added (no faiss, no Python).
add_executable(unllm_embed tools/unllm_embed.cpp)
target_link_libraries(unllm_embed PRIVATE unllm)

# Measuring tool for the meaning search: vectors and top ids per question.
add_executable(unllm_meaning tools/unllm_meaning.cpp)
target_link_libraries(unllm_meaning PRIVATE unllm)

# The front end: questions about the world, one at a time or as a conversation.
add_executable(unllm_cli tools/unllm_main.cpp)
target_link_libraries(unllm_cli PRIVATE unllm)
set_target_properties(unllm_cli PROPERTIES OUTPUT_NAME unllm)

if(UNLLM_PACKAGE_NATIVE)
  add_subdirectory(native)
  target_compile_definitions(unllm PUBLIC UNLLM_DEFAULT_NATIVE_RUNTIME)
  add_dependencies(unllm_cli unllm_algorithm unllm_symbolic_worker unllm_logic_worker unllm_update_native unllm_embed)
  if(TARGET unllm_index)
    add_dependencies(unllm_cli unllm_index)
  endif()
  install(TARGETS unllm_cli unllm_embed unllm_pack unllm_meaning unllm_rerank RUNTIME DESTINATION bin)
  install(DIRECTORY data/ DESTINATION share/unllm FILES_MATCHING PATTERN "*.txt" PATTERN "vectors.bin")
endif()

# One program per exam: each has its own main(). ctest runs them all.
enable_testing()
if(TARGET unllm_ranked)
  add_executable(unllm_ranked_probe tests/cpp_migration/ranked_probe.cpp)
  target_link_libraries(unllm_ranked_probe PRIVATE unllm_ranked)
  add_test(NAME ranked_compatibility COMMAND "${CMAKE_SOURCE_DIR}/tests/cpp_migration/ranked.sh"
    "$<TARGET_FILE:unllm_index>" "$<TARGET_FILE:unllm_ranked_probe>"
    "${CMAKE_SOURCE_DIR}/tests/cpp_migration/fixtures/ranked_reference.txt")
endif()
add_executable(unllm_tests tests/exam.cpp)
target_link_libraries(unllm_tests PRIVATE unllm)
add_test(NAME exam COMMAND unllm_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
foreach(t dialogue world calc extract)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tests/${t}_exam.cpp)
    add_executable(unllm_${t}_tests tests/${t}_exam.cpp)
    target_link_libraries(unllm_${t}_tests PRIVATE unllm)
    add_test(NAME ${t} COMMAND unllm_${t}_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endforeach()
# The pack builder's text cleaning (Python, standard library only).
if(NOT UNLLM_PACKAGE_NATIVE)
  find_program(PYTHON3 python3)
endif()
if(PYTHON3 AND NOT UNLLM_PACKAGE_NATIVE)
  add_test(NAME ingest COMMAND ${PYTHON3} -I ${CMAKE_SOURCE_DIR}/tests/test_ingest.py)
endif()
if(UNLLM_SYMBOLIC AND NOT UNLLM_PACKAGE_NATIVE)
  if(NOT EXISTS ${CMAKE_SOURCE_DIR}/.venv/bin/python)
    message(FATAL_ERROR "Symbolic Python runtime missing; run tools/setup_upgrade.py")
  endif()
  foreach(t upgrade synthesis upgrade_cli app_synthesis behaviour_synthesis semantic_generation)
    add_test(NAME ${t} COMMAND ${CMAKE_SOURCE_DIR}/.venv/bin/python ${CMAKE_SOURCE_DIR}/tests/test_${t}.py)
    set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 240 RUN_SERIAL TRUE)
  endforeach()
  set_tests_properties(behaviour_synthesis PROPERTIES TIMEOUT 360)
  set_tests_properties(semantic_generation PROPERTIES TIMEOUT 360)
endif()

# The breadth gauge: how much of a real-world question corpus is understood.
# The exam stays the correctness gate; this one only measures reach.
add_executable(unllm_coverage tools/coverage.cpp)
target_link_libraries(unllm_coverage PRIVATE unllm)
