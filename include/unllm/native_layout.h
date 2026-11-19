#pragma once
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace unllm {
// A staged installation puts both workers next to the CLI and its resources
// under share/unllm. An in-tree development build uses build-cpp instead.
struct NativeLayout {
    std::filesystem::path resources, bin, lib;
    bool installed;

    static bool selected() {
#ifdef UNLLM_DEFAULT_NATIVE_RUNTIME
        return true;
#else
        const char* runtime = std::getenv("UNLLM_SYMBOLIC_RUNTIME");
        return runtime && std::string_view(runtime) == "native";
#endif
    }

    static NativeLayout locate(const std::filesystem::path& cli_dir,
                               const std::filesystem::path& checkout) {
        const auto prefix = cli_dir.parent_path();
        const auto resources = prefix / "share/unllm";
        if (std::filesystem::is_regular_file(resources / "dependencies-native.lock.json"))
            return {resources, cli_dir, prefix / "lib", true};
#ifdef UNLLM_DEFAULT_NATIVE_RUNTIME
        const auto workers = cli_dir / "native";
        return {checkout, workers, workers, false};
#else
        return {checkout, checkout / "build-cpp", checkout / "build-cpp", false};
#endif
    }
};
}
