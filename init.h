#pragma once

#include <cstdint>

struct Import {
    const char* dylib;
    const char* symbol;
    uint64_t addr;
};

struct Export {
    const char* symbol;
    uint64_t addr;
};

struct Segment {
    const char* section;
    const char* segment;
    uint64_t addr, size;
};

struct TargetFile {
    const char* fn;
    uint64_t main;
    uint64_t* objcClasses;
    Import* imports;
    Export* exports;
    Segment* segments;
};

struct Init {
    int fileCount;
    TargetFile* files;
};
