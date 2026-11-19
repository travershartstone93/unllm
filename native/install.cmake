# Staged native runtime. The CMake/Java dependency builder and optional
# generated-Python interpreter are separate from what is installed here.
set(native_commands unllm_symbolic_worker unllm_logic_worker unllm_facts_native
    unllm_generate_stream unllm_generate_gui unllm_update_native
    unllm_ingest_native unllm_evaluate_native)
set_target_properties(${native_commands} PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
set_target_properties(unllm_native_logic PROPERTIES INSTALL_RPATH "$ORIGIN")
install(TARGETS ${native_commands} unllm_native_logic RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
set(share share/unllm)
set(staged "${UNLLM_ROOT}/.deps")
set(sdk "${staged}/cvc5-native/cvc5-Linux-x86_64-shared")
foreach(lib libcvc5.so.1 libgmp.so.10 libpoly.so.0 libpolyxx.so.0)
  # Some SDK distributions use symlinks. Install the actual SONAME object.
  file(REAL_PATH "${sdk}/lib/${lib}" library)
  install(FILES "${library}" DESTINATION lib RENAME "${lib}")
endforeach()
file(REAL_PATH "${SWIPL_LIBRARY}" swipl)
install(FILES "${swipl}" DESTINATION lib RENAME libswipl.so.10)
# The staged SWI distribution points its RUNPATH at its distributor's
# installation. Resolve its GMP dependency next to the installed library.
install(CODE [=[
  set(object "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libswipl.so.10")
  file(READ_ELF "${object}" RUNPATH old_runpath)
  if(old_runpath AND NOT old_runpath STREQUAL "$ORIGIN")
    file(RPATH_CHANGE FILE "${object}" OLD_RPATH "${old_runpath}" NEW_RPATH "$ORIGIN")
  endif()
]=])
install(DIRECTORY "${UNLLM_ROOT}/reasoning" DESTINATION "${share}")
foreach(file reasoning_relations.json reasoning_ontology.json program_semantics.json
    program_frame_vocabulary.json calculator_methods.json behaviour_operators.json)
  install(FILES "${UNLLM_ROOT}/data/${file}" DESTINATION "${share}/data")
endforeach()
install(DIRECTORY "${UNLLM_ROOT}/resources/targets" DESTINATION "${share}/resources"
    PATTERN "__pycache__" EXCLUDE)
install(FILES "${staged}/erg.dat" DESTINATION "${share}/.deps")
install(PROGRAMS "${staged}/ace-0.9.34/ace" DESTINATION "${share}/.deps/ace-0.9.34")
set(swi "${staged}/native/usr/lib/swipl")
set(swi_dest "${share}/.deps/native/usr/lib/swipl")
install(FILES "${swi}/boot.prc" "${swi}/ABI" "${swi}/LICENSE" DESTINATION "${swi_dest}")
install(DIRECTORY "${swi}/boot" DESTINATION "${swi_dest}" FILES_MATCHING PATTERN "*.pl")
install(DIRECTORY "${swi}/library" DESTINATION "${swi_dest}" FILES_MATCHING
    PATTERN "*.pl" PATTERN "ext" EXCLUDE PATTERN "janus*" EXCLUDE PATTERN "jpl*" EXCLUDE)
set(apps "${staged}/apps")
install(DIRECTORY "${apps}/generated" DESTINATION "${share}/.deps/apps" FILES_MATCHING
    PATTERN "*.cpp" PATTERN "*.h" PATTERN "*.py" PATTERN "*.a" PATTERN "manifest.json" PATTERN "__pycache__" EXCLUDE)
install(DIRECTORY "${apps}/native/include/antlr4-runtime" DESTINATION "${share}/.deps/apps/native/include")
install(FILES "${apps}/native/lib/libantlr4-runtime.a" DESTINATION "${share}/.deps/apps/native/lib")
install(FILES "${apps}/antlr-source/LICENSE.txt" DESTINATION "${share}/.deps/apps/antlr-source")
install(FILES "${staged}/ace-0.9.34/LICENSE" DESTINATION "${share}/licenses/ace")
install(FILES "${sdk}/COPYING" DESTINATION "${share}/licenses/cvc5")
install(DIRECTORY "${sdk}/licenses/" DESTINATION "${share}/licenses/cvc5")
install(FILES "${UNLLM_ROOT}/LICENSE" "${UNLLM_ROOT}/dependencies-native.lock.json" "${UNLLM_ROOT}/dependencies-apps.lock.json" "${UNLLM_ROOT}/dependencies-upgrade.lock.json" DESTINATION "${share}")

if(EXISTS "${UNLLM_ARROW_SDK}/licenses")
  install(DIRECTORY "${UNLLM_ARROW_SDK}/licenses/" DESTINATION "${share}/licenses/apache-arrow")
endif()
foreach(lib IN ITEMS "${ARROW_LIB}" "${PARQUET_LIB}")
  file(READ_ELF "${lib}" SONAME soname)
  file(REAL_PATH "${lib}" object)
  install(FILES "${object}" DESTINATION lib RENAME "${soname}")
endforeach()
