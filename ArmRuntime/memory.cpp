#include <cstdint>
#include <cstdlib>

extern "C" {

/// REPLACE memcpy
__attribute__((visibility("default")))
uint8_t* replace_memcpy(uint8_t* dest, const uint8_t* src, uint64_t n) {
    for(auto i = 0; i < n; ++i)
        dest[i] = src[i];
    return dest;
}

/// REPLACE memset
__attribute__((visibility("default")))
uint8_t* replace_memset(uint8_t* dest, int value, size_t n) {
    for(auto i = 0; i < n; ++i)
        dest[i] = (uint8_t) value;
    return dest;
}

/// REPLACE memmove
__attribute__((visibility("default")))
uint8_t* replace_memmove(uint8_t* dest, const uint8_t* src, uint64_t n) {
    if(n == 0)
        return dest;
    if(dest + n < src || src + n < dest)
        replace_memcpy(dest, src, n);
    else if(n > 16384) {
        auto buf = (uint8_t*) malloc(n);
        replace_memcpy(buf, src, n);
        replace_memcpy(dest, buf, n);
        free(buf);
    } else {
        uint8_t buf[n];
        replace_memcpy(buf, src, n);
        replace_memcpy(dest, buf, n);
    }
    return dest;
}

}
