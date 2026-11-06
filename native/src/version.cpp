#include "lumacore/version.hpp"

#define LC_STRINGIFY_IMPL(x) #x
#define LC_STRINGIFY(x) LC_STRINGIFY_IMPL(x)

namespace lc {

Version version() noexcept {
    return Version{LUMACORE_VERSION_MAJOR, LUMACORE_VERSION_MINOR, LUMACORE_VERSION_PATCH};
}

const char* version_string() noexcept {
    return LC_STRINGIFY(LUMACORE_VERSION_MAJOR) "." LC_STRINGIFY(
        LUMACORE_VERSION_MINOR) "." LC_STRINGIFY(LUMACORE_VERSION_PATCH);
}

}  // namespace lc
