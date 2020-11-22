#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>
using namespace std;

class MachO {
public:
	MachO(const char* fn);
	void bind(string* dylibs, uint8_t* data, uint32_t size);
	void rebase(uint8_t* data, uint32_t size);
	void rebaseAt(uint64_t address, int type);
	void doExport(uint8_t* data, uint32_t size);
	uint64_t* segment_offsets;
	uint64_t base_address, main;

	vector<uint64_t> relocations, objcClasses;
    vector<tuple<uint64_t, string, string>> imports;
    vector<tuple<uint64_t, string>> exports;
	unordered_map<string, tuple<uint64_t, uint64_t, uint64_t, unordered_map<string, tuple<uint64_t, uint64_t>>, bool, void*>> segments; // offset, vm size, file size, section addresses+sizes, writable, data
};
