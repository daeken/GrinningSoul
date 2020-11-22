#include "macho.h"

#include <mach/machine.h>
#include <mach/vm_prot.h>
#include <sys/mman.h>
#include <iostream>
#include <ios>
#include <cstring>
#include <dlfcn.h>

struct mach_header_64 {
	uint32_t	magic;		/* mach magic number identifier */
	cpu_type_t	cputype;	/* cpu specifier */
	cpu_subtype_t	cpusubtype;	/* machine specifier */
	uint32_t	filetype;	/* type of file */
	uint32_t	ncmds;		/* number of load commands */
	uint32_t	sizeofcmds;	/* the size of all the load commands */
	uint32_t	flags;		/* flags */
	uint32_t	reserved;	/* reserved */
};

#define LC_REQ_DYLD 0x80000000
#define	LC_SEGMENT	0x1	/* segment of this file to be mapped */
#define	LC_SYMTAB	0x2	/* link-edit stab symbol table info */
#define	LC_SYMSEG	0x3	/* link-edit gdb symbol table info (obsolete) */
#define	LC_THREAD	0x4	/* thread */
#define	LC_UNIXTHREAD	0x5	/* unix thread (includes a stack) */
#define	LC_LOADFVMLIB	0x6	/* load a specified fixed VM shared library */
#define	LC_IDFVMLIB	0x7	/* fixed VM shared library identification */
#define	LC_IDENT	0x8	/* object identification info (obsolete) */
#define LC_FVMFILE	0x9	/* fixed VM file inclusion (internal use) */
#define LC_PREPAGE      0xa     /* prepage command (internal use) */
#define	LC_DYSYMTAB	0xb	/* dynamic link-edit symbol table info */
#define	LC_LOAD_DYLIB	0xc	/* load a dynamicly linked shared library */
#define LC_LOAD_WEAK_DYLIB 0x80000018
#define	LC_ID_DYLIB	0xd	/* dynamicly linked shared lib identification */
#define LC_LOAD_DYLINKER 0xe	/* load a dynamic linker */
#define LC_ID_DYLINKER	0xf	/* dynamic linker identification */
#define	LC_PREBOUND_DYLIB 0x10	/* modules prebound for a dynamicly */
				/*  linked shared library */
#define LC_SEGMENT_64	0x19	/* 64-bit segment of this file to be mapped */
#define LC_ROUTINES_64	0x1a	/* 64-bit image routines */
#define LC_DYLD_INFO    0x22    /* compressed dyld information */
#define LC_DYLD_INFO_ONLY (0x22|LC_REQ_DYLD)    /* compressed dyld information only */

struct load_command {
	uint32_t cmd;		/* type of load command */
	uint32_t cmdsize;		/* total size of command in bytes */
};

struct segment_command_64 {	/* for 64-bit architectures */
	uint32_t	cmd;		/* LC_SEGMENT_64 */
	uint32_t	cmdsize;	/* includes sizeof section_64 structs */
	char		segname[16];	/* segment name */
	uint64_t	vmaddr;		/* memory address of this segment */
	uint64_t	vmsize;		/* memory size of this segment */
	uint64_t	fileoff;	/* file offset of this segment */
	uint64_t	filesize;	/* amount to map from the file */
	vm_prot_t	maxprot;	/* maximum VM protection */
	vm_prot_t	initprot;	/* initial VM protection */
	uint32_t	nsects;		/* number of sections in segment */
	uint32_t	flags;		/* flags */
};

struct section_64 {
	char sectname[16];
	char segname[16];
	uint64_t addr, size;
	uint32_t offset, align, reloff, nreloc, flags, reserved1, reserved2;
};

struct main_command_64 {
	uint32_t	cmd;		/* LC_SEGMENT_64 */
	uint32_t	cmdsize;	/* includes sizeof section_64 structs */
	uint64_t    offset, stacksize;
};

struct dyld_info_command {
    uint32_t   cmd;		/* LC_DYLD_INFO or LC_DYLD_INFO_ONLY */
    uint32_t   cmdsize;		/* sizeof(struct dyld_info_command) */
    uint32_t   rebase_off;
    uint32_t   rebase_size;
    uint32_t   bind_off;
    uint32_t   bind_size;
    uint32_t   weak_bind_off;
    uint32_t   weak_bind_size;
    uint32_t   lazy_bind_off;
    uint32_t   lazy_bind_size;
    uint32_t   export_off;
    uint32_t   export_size;
};

MachO::MachO(const char* fn) {
	auto fp = fopen(fn, "rb");
	fseek(fp, 0L, SEEK_END);
	auto size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	// LEAKY!
	auto data = (uint8_t*) malloc(size);
	fread(data, 1, size, fp);

	auto header = (mach_header_64*) data;
	assert(header->magic == 0xfeedfacf);
	//if(header->cputype != (CPU_TYPE_ARM | CPU_ARCH_ABI64))
	//	return;
	assert(header->cputype == (CPU_TYPE_ARM | CPU_ARCH_ABI64));
	main = 0;

	auto cmd = (load_command*) (data + sizeof(mach_header_64));
	auto top = 0ULL;
	auto bottom = -1ULL;
	auto dylibCount = 0;
	auto segmentCount = 0;
	for(auto i = 0; i < header->ncmds; ++i) {
		if(cmd->cmd == LC_SEGMENT_64) {
			segmentCount++;
			auto seg = (segment_command_64*) cmd;
			if(strcmp(seg->segname, "__PAGEZERO") != 0) {
				auto taddr = seg->vmaddr + seg->vmsize;
				if(taddr > top)
					top = taddr;
				if(seg->vmaddr < bottom)
					bottom = seg->vmaddr;
			} else if(seg->vmaddr + seg->vmsize < bottom)
			    bottom = seg->vmaddr + seg->vmsize;

		} else if(cmd->cmd == LC_LOAD_DYLIB || cmd->cmd == LC_LOAD_WEAK_DYLIB)
			dylibCount++;
		cmd = (load_command*) (((uint8_t*) cmd) + cmd->cmdsize);
	}

	base_address = bottom;
	printf("Base address %p\n", (void*) base_address);
	auto vmsize = top - bottom;
	if(vmsize & 0xFFF)
		vmsize += 0x1000 - (vmsize & 0xFFF);

	auto dylibs = new string[dylibCount + 1];
	dylibs[0] = "";
	segment_offsets = new uint64_t[segmentCount];
	dylibCount = 1;
	segmentCount = 0;

	dyld_info_command* dyld_info = nullptr;

	cmd = (load_command*) (data + sizeof(mach_header_64));
	for(auto i = 0; i < header->ncmds; ++i) {
		switch(cmd->cmd) {
			case LC_SEGMENT_64: {
				auto seg = (segment_command_64*) cmd;
				//if(strcmp(seg->segname, "__objc_classlist") != 0)
				segment_offsets[segmentCount++] = seg->vmaddr - bottom;
				auto sects = (section_64*) ((uint8_t*) cmd + sizeof(segment_command_64));
				unordered_map<string, tuple<uint64_t, uint64_t>> sections;
				for(auto j = 0; j < seg->nsects; ++j) {
                    sections[string(sects[j].sectname).substr(0, 16)] = {sects[j].addr - bottom, sects[j].size};
                    if(strncmp(sects[j].sectname, "__objc_classlist", 16) == 0) {
                        assert(objcClasses.empty());
                        auto objc_classbase = (uint64_t*) (data + sects[j].addr - seg->vmaddr + seg->fileoff);
                        auto objc_classnum = sects[j].size / 8;
                        for(auto k = 0; k < objc_classnum; ++k)
                            objcClasses.push_back(*objc_classbase++ - bottom);
                    }
                }
                segments[string(seg->segname).substr(0, 16)] = {seg->vmaddr - bottom, seg->vmsize, seg->filesize, sections, (seg->initprot & VM_PROT_WRITE) != 0, data + seg->fileoff};
				break;
			}
			case 0x80000028: { // LC_MAIN
				auto mcmd = (main_command_64*) cmd;
				main = mcmd->offset;
				break;
			}
			case LC_LOAD_DYLIB:
			case LC_LOAD_WEAK_DYLIB: {
				auto dpath = (char*) cmd + 0x18;
				dylibs[dylibCount++] = dpath;
				break;
			}
			case LC_DYLD_INFO_ONLY:
				dyld_info = (dyld_info_command*) cmd;
				break;
			default:
				printf("Unknown command 0x%x\n", cmd->cmd);
				break;
		}
		cmd = (load_command*) (((uint8_t*) cmd) + cmd->cmdsize);
	}

	assert(dyld_info != nullptr);

	if(dyld_info->rebase_off != 0)
    	rebase(data + dyld_info->rebase_off, dyld_info->rebase_size);
    if(dyld_info->bind_off != 0)
    	bind(dylibs, data + dyld_info->bind_off, dyld_info->bind_size);
    if(dyld_info->weak_bind_off != 0)
        bind(dylibs, data + dyld_info->weak_bind_off, dyld_info->weak_bind_size);
    if(dyld_info->lazy_bind_off != 0)
        bind(dylibs, data + dyld_info->lazy_bind_off, dyld_info->lazy_bind_size);
    if(dyld_info->export_off != 0)
        doExport(data + dyld_info->export_off, dyld_info->export_size);

	/*if(objc_classnum != 0U) {
		auto objch = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW);
		ASSERT(objch != nullptr);
		auto _objc_initWeak = (uint8_t*) dlsym(objch, "objc_initWeak");
		ASSERT(_objc_initWeak != nullptr);
		auto addClassTableEntryOffset = 0xbd4d;
		auto _objc_initWeakOffset = 0x1bb14;
		auto addClassTableEntry = (void (*)(void* cls, bool addMeta)) (_objc_initWeak - _objc_initWeakOffset + addClassTableEntryOffset);
		LOG("Adding %i Obj-C classes", objc_classnum);
		for(auto i = 0; i < objc_classnum; ++i) {
			LOG("Adding class at %p", ((uint64_t**) objc_classbase)[i]);
			//auto foo = true;
			//while(foo) { }
			addClassTableEntry(((uint64_t**) objc_classbase)[i], false);
			LOG("Done");
		}
	}*/
}

uint64_t readUleb(uint8_t* data, uint32_t& i) {
	auto val = 0ULL;
	for(auto j = 0; ; ++j) {
		val |= ((uint64_t) (data[i] & 0x7F)) << (j * 7);
		if((data[i++] & 0x80) == 0)
			break;
	}
	return val;
}

#define REBASE_TYPE_POINTER					1
#define REBASE_TYPE_TEXT_ABSOLUTE32				2
#define REBASE_TYPE_TEXT_PCREL32				3
#define REBASE_OPCODE_MASK					0xF0
#define REBASE_IMMEDIATE_MASK					0x0F
#define REBASE_OPCODE_DONE					0x00
#define REBASE_OPCODE_SET_TYPE_IMM				0x10
#define REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB		0x20
#define REBASE_OPCODE_ADD_ADDR_ULEB				0x30
#define REBASE_OPCODE_ADD_ADDR_IMM_SCALED			0x40
#define REBASE_OPCODE_DO_REBASE_IMM_TIMES			0x50
#define REBASE_OPCODE_DO_REBASE_ULEB_TIMES			0x60
#define REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB			0x70
#define REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB	0x80

void MachO::rebaseAt(uint64_t address, int type) {
	relocations.push_back(address);
}

void MachO::rebase(uint8_t* data, uint32_t size) {
	auto seg_index = 0;
	auto seg_offset = 0LL;
	auto address = 0ULL;
	auto type = 0;

	for(auto i = 0U; i < size;) {
		switch(data[i] & REBASE_OPCODE_MASK) {
			case REBASE_OPCODE_DONE:
				++i;
				break;
			case REBASE_OPCODE_SET_TYPE_IMM:
				type = data[i++] & REBASE_IMMEDIATE_MASK;
				break;
			case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: {
				seg_index = data[i++] & REBASE_IMMEDIATE_MASK;
				seg_offset = (int64_t) readUleb(data, i);
				address = (uint64_t) (segment_offsets[seg_index] + seg_offset);
				break;
			}
			case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB: {
				++i;
				rebaseAt(address, type);
				address += (int64_t) readUleb(data, i) + 8;
				break;
			}
			case REBASE_OPCODE_ADD_ADDR_ULEB:
			    ++i;
                address += (int64_t) readUleb(data, i);
			    break;
			case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
				address += (data[i++] & REBASE_IMMEDIATE_MASK) * 8;
				break;
			case REBASE_OPCODE_DO_REBASE_IMM_TIMES: {
				auto imm = data[i++] & REBASE_IMMEDIATE_MASK;
				for(auto j = 0; j < imm; ++j) {
					rebaseAt(address, type);
					address += 8;
				}
				break;
			}
			case REBASE_OPCODE_DO_REBASE_ULEB_TIMES: {
				++i;
				auto count = readUleb(data, i);
				for(auto j = 0; j < count; ++j) {
					rebaseAt(address, type);
					address += 8;
				}
				break;
			}
			case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB: {
				++i;
				auto count = readUleb(data, i);
				auto skip = readUleb(data, i);
				for(auto j = 0; j < count; ++j) {
					rebaseAt(address, type);
					address += skip + 8;
				}
				break;
			}
			default:
				printf("Unknown rebase opcode 0x%02x\n", data[i] & REBASE_OPCODE_MASK);
				assert(false);
		}
	}
}

#define BIND_TYPE_POINTER					1
#define BIND_TYPE_TEXT_ABSOLUTE32				2
#define BIND_TYPE_TEXT_PCREL32					3
#define BIND_SPECIAL_DYLIB_SELF					 0
#define BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE			-1
#define BIND_SPECIAL_DYLIB_FLAT_LOOKUP				-2
#define BIND_SYMBOL_FLAGS_WEAK_IMPORT				0x1
#define BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION			0x8
#define BIND_OPCODE_MASK					0xF0
#define BIND_IMMEDIATE_MASK					0x0F
#define BIND_OPCODE_DONE					0x00
#define BIND_OPCODE_SET_DYLIB_ORDINAL_IMM			0x10
#define BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB			0x20
#define BIND_OPCODE_SET_DYLIB_SPECIAL_IMM			0x30
#define BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM		0x40
#define BIND_OPCODE_SET_TYPE_IMM				0x50
#define BIND_OPCODE_SET_ADDEND_SLEB				0x60
#define BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB			0x70
#define BIND_OPCODE_ADD_ADDR_ULEB				0x80
#define BIND_OPCODE_DO_BIND					0x90
#define BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB			0xA0
#define BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED			0xB0
#define BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB		0xC0

void* curdlsym(void* handle, const char* symname) {
	if(strcmp(symname, "dyld_stub_binder") == 0)
		return nullptr;
	auto symptr = dlsym(handle, symname);
	if(symptr == nullptr)
		symptr = dlsym(handle, symname + 1);
	assert(symptr != nullptr);
	return symptr;
}

void MachO::bind(string* dylibs, uint8_t* data, uint32_t size) {
	auto seg_index = 0U;
	auto seg_offset = 0LL;
	auto lib_ordinal = 0U;
	auto type = 0U;
	auto flags = 0U;
	auto special_dylib = 1U;
	auto symname = "";
	auto address = 0ULL;
	auto addend = 0ULL;
	
	for(auto i = 0U; i < size;) {
		switch(data[i] & BIND_OPCODE_MASK) {
			case BIND_OPCODE_DONE:
				++i;
				break;
		    case BIND_OPCODE_SET_ADDEND_SLEB:
		        ++i;
		        addend = readUleb(data, i);
		        break;
			case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: {
				seg_index = data[i++] & BIND_IMMEDIATE_MASK;
				seg_offset = (int64_t) readUleb(data, i);
				address = (uint64_t) (segment_offsets[seg_index] + seg_offset);
				break;
			}
			case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
				lib_ordinal = data[i++] & BIND_IMMEDIATE_MASK;
				break;
		    case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
		        i++;
		        lib_ordinal = readUleb(data, i);
		        break;
		    case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
                i++;
		        lib_ordinal = 0; // TODO: Handle this properly!
		        break;
			case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM: {
				i++;
				symname = (char*) &data[i];
				while(data[i++] != 0);
				break;
			}
			case BIND_OPCODE_SET_TYPE_IMM:
				type = data[i++] & BIND_IMMEDIATE_MASK;
				break;
			case BIND_OPCODE_DO_BIND: {
				imports.emplace_back(address, dylibs[lib_ordinal], symname);
				address += 8;
				++i;
				break;
			}
			case BIND_OPCODE_ADD_ADDR_ULEB: {
				++i;
				auto off = readUleb(data, i);
				address += off;
				break;
			}
			case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB: {
				//LOG("Binding '%s' in %i to %p", symname, lib_ordinal, (void*) address);
                imports.emplace_back(address, dylibs[lib_ordinal], symname);
				++i;
				address += readUleb(data, i) + 8;
				break;
			}
			case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED: {
				//LOG("Binding '%s' in %i to %p", symname, lib_ordinal, (void*) address);
                imports.emplace_back(address, dylibs[lib_ordinal], symname);
				address += (data[i++] & BIND_IMMEDIATE_MASK) * 8 + 8;
				break;
			}
			case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB: {
			    ++i;
                auto count = (int64_t) readUleb(data, i);
                auto skip = (int64_t) readUleb(data, i);
                for(auto j = 0; j < count; ++j) {
                    imports.emplace_back(address, dylibs[lib_ordinal], symname);
                    address += skip + 8;
                }
                break;
            }
			default:
				printf("Unknown bind opcode 0x%02x\n", data[i] & BIND_OPCODE_MASK);
				assert(false);
		}
	}
}

#define EXPORT_SYMBOL_FLAGS_KIND_MASK				0x03
#define EXPORT_SYMBOL_FLAGS_KIND_REGULAR			0x00
#define EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL			0x01
#define EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION			0x04
#define EXPORT_SYMBOL_FLAGS_REEXPORT				0x08
#define EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER			0x10

void MachO::doExport(uint8_t *data, uint32_t size) {
    function<void(uint32_t, string)> parse([&](auto offset, auto prefix) {
        cout << "Parsing at offset 0x" << hex << offset << endl;
        assert(offset < size);
        auto i = offset;
        auto terminalSize = (int) readUleb(data, i);
        if(terminalSize != 0) {
            auto start = i;
            auto flags = readUleb(data, i);
            if(flags & EXPORT_SYMBOL_FLAGS_REEXPORT) {
                auto ordinal = readUleb(data, i);
                string symname { (const char*) &data[i] };
                while(data[i++] != 0);
            } else if(flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
                auto stubOffset = readUleb(data, i);
                auto resolverOffset = readUleb(data, i);
            } else {
                auto symOffset = readUleb(data, i);
                //cout << "0x" << hex << i << ": Found export " << prefix << " at 0x" << hex << symOffset << endl;
                exports.emplace_back(symOffset, prefix);
            }
            assert(start + terminalSize == i);
        }
        auto childCount = data[i++];
        while(childCount--) {
            auto clabel = prefix + (const char*) &data[i];
            while(data[i++] != 0);
            parse((uint32_t) readUleb(data, i), clabel);
        }
    });
    parse(0, "");
}
