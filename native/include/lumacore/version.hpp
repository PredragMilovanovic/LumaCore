#pragma once

namespace lc {

struct Version {
    int major;
    int minor;
    int patch;
};

/// Engine version, taken from the CMake project version at build time.
[[nodiscard]] Version version() noexcept;

/// Version as "major.minor.patch". The returned pointer refers to static storage.
[[nodiscard]] const char* version_string() noexcept;

}  // namespace lc
