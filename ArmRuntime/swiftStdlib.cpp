#include <cstdint>

extern "C" {

typedef struct {
    uint64_t major, minor, patch;
} _SwiftNSOperatingSystemVersion;

/// REPLACE _swift_stdlib_operatingSystemVersion
__attribute__((visibility("default")))
_SwiftNSOperatingSystemVersion replace__swift_stdlib_operatingSystemVersion() {
    return {13, 3, 0};
}

}
