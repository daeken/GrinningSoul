#include <cstdio>
#include "simenv.h"
#include "macho.h"

uint32_t addString(string str, uint8_t** stringTable, uint32_t* stringTableSize) {
    auto data = str.c_str();
    auto size = (int) str.length() + 1;
    for(auto i = 0; i < (int) (*stringTableSize) - size + 1; ++i)
        if(memcmp(*stringTable + i, data, size) == 0)
            return i;
    *stringTable = (uint8_t*) realloc(*stringTable, *stringTableSize + size);
    memcpy(*stringTable + *stringTableSize, data, size);
    auto off = *stringTableSize;
    *stringTableSize += size;
    return off;
}

int main(int argc, char** argv) {
    if(argc < 2) {
        printf("Usage: %s <macho>\n", argv[0]);
        return 1;
    }

    MachO macho { argv[1] };
    printf("base:%llx\n", macho.base_address);
    printf("entrypoint:%llx\n", macho.main);
    for(auto addr : macho.objcClasses)
        printf("objcClass:%llx\n", addr);
    for(auto& [addr, dylib, name] : macho.imports)
        printf("import:%llx;%s;%s\n", addr, dylib.c_str(), name.c_str());
    for(auto& [addr, name] : macho.exports)
        printf("export:%llx;%s\n", addr, name.c_str());

    for(auto& [segname, tup] : macho.segments) {
        printf("segment:%s\n", segname.c_str());
        auto& [offset, vmSize, fileSize, sectionAddresses, writable, data] = tup;
        for(auto& [sectname, stup] : sectionAddresses) {
            auto& [addr, size] = stup;
            printf("section:%s;%llx;%llx\n", sectname.c_str(), addr, size);
        }
    }

    return 0;
}