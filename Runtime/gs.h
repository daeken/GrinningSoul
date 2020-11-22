#pragma once

#include <stdint.h>

#include "cpu.h"
#include "logging.h"

#define PAGEBASE(addr) ((addr) & ~0xFFFULL)
#define PAGEOFF(addr) ((addr) & 0xFFFULL)
#define LARGEPAGEBASE(addr) ((addr) & ~0x3FFFULL)

void initializeImages();
#define BAILOUT() bailout(__FILE__, __LINE__)
__attribute__((noreturn)) void bailout(const char* fn, int line);

bool isKeychainUsable();

struct BlockInternal {
    void* isa;
    int flags, reserved;
    uint64_t function;
};
