#include "gs.h"
#include "cpu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <dlfcn.h>
#include <libgen.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <fstream>
#include <iostream>
#include <ios>
#include <set>
#include <objc/message.h>
#include <boost/format.hpp>
#include "exceptionHandlers.h"
#include "../init.h"
#include "trampoliner.h"
using namespace std;

#include "wrappers.h"

extern "C" {
    void kickoff();
}

__attribute__((noreturn)) void bailout(const char* fn, int line) {
    log("~~~Bailing out from {} line {}", fn, line);
    fflush(stderr);
    __builtin_trap();
    kill(0, 9);
    while(true) {}
}

Init* ginit;

struct __objc_class {
    uint64_t metaclass, superclass, cache, vtable, data;
};

#pragma pack(push, 0)
struct __objc_data {
    uint32_t flags, istart, isize, unk1;
    uint64_t unk2, name, methods, protocols, ivars, weakivarlayout, props;
};
#pragma pack(pop)

struct __objc_method_list {
    uint32_t flags, count;
};

struct __objc_method {
    const char *name, *signature;
    uint64_t implementation;
};

set<Class> rewritten;
void rewriteClass(Class cls) {
    if(rewritten.count(cls) != 0) return;
    rewritten.insert(cls);
    //log("Class " << hex << (uint64_t) cls);
    auto name = class_getName(cls);
    if(name == nullptr || strcmp(name, "NSObject") == 0 || strcmp(name, "CLSUserDefaults") == 0) return;
    //log("Trampolining ObjC class: " << name);

    auto superclass = class_getSuperclass(cls);
    if(superclass != nullptr) rewriteClass(superclass);
    auto metaclass = (Class) objc_getMetaClass(name);
    if(metaclass != nullptr) rewriteClass(metaclass);

    //log("Getting method list for " << name);
    auto methodCount = 0U;
    auto methods = class_copyMethodList(cls, &methodCount);
    for(auto i = 0; i < methodCount; ++i) {
        auto method = methods[i];
        auto sel = method_getName(method);
        auto mname = sel_getName(sel);
        auto signature = method_getTypeEncoding(method);
        auto implementation = (uint64_t) method_getImplementation(method);
        if(!isArmCodePointer(implementation)) continue;
        /*log("\tGot method " << mname);
        log("\tSignature pointer " << hex << (uint64_t) signature);
        log("\tSignature " << signature);*/
        //method_setImplementation(method, (IMP) TrampolinerInstance.getNATrampoline(implementation, signature));
        assert((uint64_t) method->method_imp == implementation);
        method->method_imp = (IMP) TrampolinerInstance.getNATrampoline(implementation, (boost::format("[%1% %2%]") % name % mname).str(), signature);
    }
    if(methodCount != 0)
        free(methods);
}

void neuterImage(void* addr) {
    vm_size_t vmsize;
    int _basic64[VM_REGION_BASIC_INFO_COUNT_64];
    auto info = (vm_region_basic_info_64_t) _basic64;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object;
    auto vmaddr = (uint64_t) addr;
    auto status = vm_region_64(mach_task_self(), (vm_address_t*) &vmaddr, &vmsize, VM_REGION_BASIC_INFO, (vm_region_info_t) info, &info_count, &object);
    if(status != KERN_SUCCESS) {
        log("Could not get vmregion for new image...?");
        return;
    }
    log("Text region is 0x{:x}-0x{:x}", vmaddr, vmaddr + vmsize);
    mprotect((void*) vmaddr, (size_t) vmsize, PROT_READ | PROT_WRITE);
    log("No longer executable. Win.");
}

bool interested = false, initializedAll = false;
bool* initialized;
uint64_t* imageBases;
void imageAdded(void* addr, uint64_t slide) {
    if(!interested) return;
    auto magic = *(uint32_t*) ((uint8_t*) addr + 0x1C);
    if((magic & 0xFFFFFF00U) != 0xDEADBE00) return;
    log("Got image added! 0x{:x}", (uint64_t) addr);
    log("Image magic: 0x{:x}", magic);
    neuterImage(addr);
    auto base = imageBases[magic & 0xFF] = (uint64_t) addr;
    initialized[magic & 0xFF] = false;
    initializedAll = false;
}

unordered_map<string, string> funcDb;

#include "replacements.generated.h"
static void* armRuntimeLib;
unordered_map<SEL, uint64_t> armSelReplacementFunctions;

struct ProtocolConformanceDescriptor {
    int32_t ProtocolDescriptor, NominalTypeDescriptor, ProtocolWitnessTable;
    uint32_t ConformanceFlags;
};

bool attemptSwiftInitialization(TargetFile* file, uint64_t base) {
    return true;
    Segment *typeref = nullptr, *reflstr = nullptr, *fieldmd = nullptr, *capture = nullptr, *assocty = nullptr,
            *proto = nullptr, *types = nullptr, *builtin = nullptr, *protos = nullptr;
    auto hasSwift = false;
    for(auto soff = file->segments; soff->size != 0; ++soff) {
        if(strcmp(soff->section, "__TEXT") != 0) continue;
        if(strcmp(soff->segment, "__swift5_typeref") == 0)
            typeref = soff;
        else if(strcmp(soff->segment, "__swift5_reflstr") == 0)
            reflstr = soff;
        else if(strcmp(soff->segment, "__swift5_fieldmd") == 0)
            fieldmd = soff;
        else if(strcmp(soff->segment, "__swift5_capture") == 0)
            capture = soff;
        else if(strcmp(soff->segment, "__swift5_assocty") == 0)
            assocty = soff;
        else if(strcmp(soff->segment, "__swift5_proto") == 0)
            proto = soff;
        else if(strcmp(soff->segment, "__swift5_types") == 0)
            types = soff;
        else if(strcmp(soff->segment, "__swift5_builtin") == 0)
            builtin = soff;
        else if(strcmp(soff->segment, "__swift5_protos") == 0)
            protos = soff;
        else
            continue;
        hasSwift = true;
    }
    if(!hasSwift) return true;

    log("~~~Found swift!");

    /*auto scl = dlopen("/usr/lib/swift/libswiftCore.dylib", RTLD_LAZY);
    if(scl == nullptr) BAILOUT();
    auto demangle = (const char*(*)(const char*, int, int, int, int)) dlsym(scl, "swift_demangle");
    if(demangle == nullptr) BAILOUT();*/

    for(auto ioff = file->imports; ioff->addr != 0; ++ioff) {
        if(ioff->symbol[0] != '_' || ioff->symbol[1] != '$' || ioff->symbol[2] != 's') continue;
        log("~~~Found swift import: {}", ioff->symbol);
        //log("~~~Demangled to: " << demangle(ioff->symbol, strlen(ioff->symbol), 0, 0, 0));
    }

    if(proto != nullptr) {
        auto pstart = base + proto->addr, psize = proto->size;
        log("~~~Swift protos start at 0x{:x}", pstart);
        for(auto i = 0; i < psize; i += 4) {
            auto off = *(int32_t*) (pstart + i);
            auto pcd = (uint64_t) (pstart + i + off);
            log("~~~Found swift protocol conformance descriptor at 0x{:x}", pcd);
        }
        BAILOUT();
    }
    return true;
}

bool initializeImage(int num) {
    if(initialized[num]) return true;
    auto base = imageBases[num];
    if(base == 0) return false;
    initialized[num] = true;
    auto file = &ginit->files[num];
    log("Attempting to initialize {}", file->fn);
    log("Re-neutering");
    neuterImage((void*) base);
    for(auto ioff = file->imports; ioff->addr != 0; ++ioff) {
        auto fn = string(ioff->dylib);
        auto spos = fn.find_last_of('/');
        if(spos != -1)
            fn = fn.substr(spos + 1);
        auto found = false;
        auto ptr = (uint64_t*) (base + ioff->addr);
        if(*ptr >> 62) {
            //log("Skipping " << ioff->symbol << " at 0x" << hex << *ptr);
            continue;
        }
        for(auto& [osym, sym] : armReplacements) {
            if(osym != ioff->symbol) continue;
            //log("Found ARM replacement for " << osym << " at 0x" << hex << ioff->addr);
            if(armRuntimeLib == nullptr) {
                //log("... But ARM runtime not loaded yet! Will reinitialize later.");
                initialized[num] = false;
                return false;
            }
            mprotect((void*) PAGEBASE((uint64_t) ptr), (size_t) 0x1000, PROT_READ | PROT_WRITE);
            auto psym = *ptr = (uint64_t) dlsym(armRuntimeLib, sym.c_str());
            if(psym == 0) {
                log("Failed to actually load ARM replacement for {} ({})", osym, sym);
                BAILOUT();
            }
            found = true;
            break;
        }
        if(!found)
            for(auto& [dylib, symbol, func] : allWrappers) {
                if((!fn.empty() && !dylib.empty() && dylib != fn) || symbol != ioff->symbol) continue;
                log("Found native wrapper for {} in {} at 0x{:x}", symbol, dylib, ioff->addr);
                mprotect((void*) PAGEBASE((uint64_t) ptr), (size_t) 0x1000, PROT_READ | PROT_WRITE);
                *ptr = (2ULL << 62) | (uint64_t) func;
                found = true;
                break;
            }
        if(!found && fn.empty())
            for(auto i = 0; i < ginit->fileCount && !found; ++i) {
                if(i == num || imageBases[i] == 0) continue;
                auto ofile = &ginit->files[i];
                for(auto eoff = ofile->exports; eoff->addr != 0; ++eoff) {
                    if(strcmp(eoff->symbol, ioff->symbol) == 0) {
                        //log("Found guest-side handler for '" << ioff->symbol << "'");
                        //log("Actual address " << hex << imageBases[i] + eoff->addr);
                        //log("Setting to pointer " << hex << (uint64_t) ptr);
                        mprotect((void*) PAGEBASE((uint64_t) ptr), (size_t) 0x1000, PROT_READ | PROT_WRITE);
                        *ptr = imageBases[i] + eoff->addr;
                        found = true;
                        break;
                    }
                }
            }
        if(!found) {
            auto iter = funcDb.find(ioff->symbol);
            if(iter == funcDb.end() && *ioff->symbol == '_')
                iter = funcDb.find(&ioff->symbol[1]);
            if(iter != funcDb.end()) {
                //log("Found matching funcDb entry for '" << ioff->symbol << "'");
                mprotect((void*) PAGEBASE((uint64_t) ptr), (size_t) 0x1000, PROT_READ | PROT_WRITE);
                if(*ptr == 0) {
                    //log("But the symbol is null; nonexistent?");
                    /*log("... But the symbol hasn't been resolved yet. Trying that now.");
                    void* handle = nullptr;
                    if(ioff->dylib != nullptr && ioff->dylib[0] != 0)
                        handle = dlopen(ioff->dylib, RTLD_NOW);
                    if(handle != nullptr)
                        log("Got handle for dylib '" << ioff->dylib << "'; let's resolve.");
                    *ptr = (uint64_t) dlsym(handle == nullptr ? RTLD_DEFAULT : handle, ioff->symbol + 1);
                    if(*ptr == 0)
                        log("Couldn't get the symbol -- this will end badly!");*/
                } else {
                    *ptr = TrampolinerInstance.getANTrampoline(*ptr, ioff->symbol + 1, iter->second);
                    found = true;
                }
            } else {
                //log("No match for '{}'", ioff->symbol);
            }
        }
    }
    log("Rewriting Obj-C classes");
    for(auto coff = file->objcClasses; *coff != 0; ++coff) {
        //log("Trying to rewrite class 0x" << hex << *coff);
        rewriteClass((Class) (base + *coff));
    }
    log("Done with Obj-C class rewriting");

    if(!attemptSwiftInitialization(file, base))
        return initialized[num] = false;
    return true;
}

void initializeImages() {
    if(initializedAll) return;
    log("Starting image initialization");
    auto allGood = true;
    for(auto i = 0; i < ginit->fileCount; ++i)
        if(!initializeImage(i))
            allGood = false;
    log("Finished image initialization.  All good? {}", allGood);
    initializedAll = allGood;
}

#include "swiftDemangler.h"

void kickoff(const char* gspath) {
	log("Starting emulator");

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    registerExceptionHandlers();

	log("Testing Keychain");
	if(isKeychainUsable())
	    log("Keychain usable!");
	else {
        log("Keychain is unusable");
        //BAILOUT();
    }

	char path[1024];
	uint32_t size = 1023;
	path[size] = 0;
	void* lib = dlopen("/usr/lib/system/libdyld.dylib", RTLD_NOW);
	assert(lib != nullptr);
	auto _NSGetExecutablePath = (int (*)(char* buf, uint32_t* bufsize)) dlsym(lib, "_NSGetExecutablePath");
	assert(_NSGetExecutablePath != nullptr);
	_NSGetExecutablePath(path, &size);
	for(auto i = strlen(path) - 1; i >= 0; --i)
		if(path[i] == '/') {
			path[i] = 0;
			break;
		}
	log("Path: {}", path);
	//chdir(path);

    CpuInstance.dumpRegs(); // Ensure CPU is active.

    {
        log("Initializing AVAudioSession (kludge!)");
        assert(dlopen("System/Library/Frameworks/AVFoundation.framework/AVFoundation", RTLD_LAZY) != nullptr);
        auto avas = objc_getClass("AVAudioSession");
        auto instance = ((id(*)(Class, SEL)) objc_msgSend)(avas, sel_registerName("sharedInstance"));
        log("Got AVAudioSession instance: 0x{:x}", (uint64_t) instance);
    }

	ifstream sin(fmt::format("{}/funcdb2", gspath));
	string line;
	while(getline(sin, line)) {
	    auto ep = line.find('=');
	    if(line.empty() || ep == string::npos) continue;
	    auto symName = line.substr(0, ep);
	    auto encoding = line.substr(ep + 1);
	    funcDb[symName] = encoding;
	}

    auto init = new Init;
    auto fp = fopen((string(path) + "/gsinit.bin").c_str(), "rb");
    auto read32 = [&]() -> uint32_t { uint32_t val; fread(&val, sizeof(uint32_t), 1, fp); return val; };
    auto read64 = [&]() -> uint64_t { uint32_t val; fread(&val, sizeof(uint64_t), 1, fp); return val; };
    auto readString = [&](auto len) -> const char* {
        auto fn = new char[len];
        fread(fn, len, 1, fp);
        return (const char*) fn;
    };
    init->fileCount = read32();
    init->files = new TargetFile[init->fileCount];
    for(auto i = 0; i < init->fileCount; ++i) {
        auto file = &init->files[i];
        auto fnlen = read32();
        auto clscount = read32();
        auto impcount = read32();
        auto expcount = read32();
        auto segcount = read32();
        file->main = read64();
        file->fn = readString(fnlen);
        file->objcClasses = new uint64_t[clscount + 1];
        file->objcClasses[clscount] = 0;
        for(auto j = 0; j < clscount; ++j)
            file->objcClasses[j] = read64();
        file->imports = new Import[impcount + 1];
        file->imports[impcount].addr = 0;
        for(auto j = 0; j < impcount; ++j) {
            auto imp = &file->imports[j];
            imp->addr = read64();
            auto dlen = read32();
            auto nlen = read32();
            imp->dylib = readString(dlen);
            imp->symbol = readString(nlen);
        }
        file->exports = new Export[expcount + 1];
        file->exports[expcount].addr = 0;
        for(auto j = 0; j < expcount; ++j) {
            auto exp = &file->exports[j];
            exp->addr = read64();
            exp->symbol = readString(read32());
        }
        file->segments = new Segment[segcount + 1];
        file->segments[segcount].size = 0;
        for(auto j = 0; j < segcount; ++j) {
            auto seg = &file->segments[j];
            seg->addr = read64();
            seg->size = read64();
            auto clen = read32();
            auto glen = read32();
            seg->section = readString(clen);
            seg->segment = readString(glen);
        }
    }
    ginit = init;

    imageBases = new uint64_t[init->fileCount];
    memset(imageBases, 0, sizeof(uint64_t) * init->fileCount);
    initialized = new bool[init->fileCount];
    memset(initialized, 0, sizeof(bool) * init->fileCount);

	auto _dyld_register_func_for_add_image = (void(*)(void (*)(void*, uint64_t))) dlsym(lib, "_dyld_register_func_for_add_image");
	_dyld_register_func_for_add_image(imageAdded);
    assert(_dyld_register_func_for_add_image != nullptr);
    interested = true;

    /*char obpath[1024];
    snprintf(obpath, 1024, "%s/%s", path, ofn);
    log("Trying load of " << obpath);

    Wimpy wimpy{obpath};
    wimpyLink(wimpy);

    log("Loaded! Trying to run main at " << hex << wimpy.entrypoint);
    auto cpu = new Cpu();
    cpu->runFrom(wimpy.entrypoint);*/

    log("Loading ARM runtime dylib");
    armRuntimeLib = dlopen((string(path) + "/libarmruntime.dylib").c_str(), RTLD_NOW);
    log("???");
    assert(armRuntimeLib != nullptr);
    for(auto [selName, funcName] : armSelReplacements)
        armSelReplacementFunctions[sel_registerName(selName)] = (uint64_t) dlsym(armRuntimeLib, funcName);

    uint64_t main = -1ULL;
    const char* mainBinary = nullptr;

    for(auto i = 0; i < init->fileCount; ++i) {
        auto file = &init->files[i];
        log("Trying load of {}", file->fn);

        auto obin = dlopen(file->fn, RTLD_NOW);
        if(obin == nullptr)
            log("{}", dlerror());
        assert(obin != nullptr);
        auto base = imageBases[i];
        assert(base != 0);
        if(file->main != 0) {
            mainBinary = file->fn;
            main = base + file->main;
            log("Completed load and got main");
            log("Main at 0x{:x}", (uint64_t) main);
        } else
            log("Completed load");
        log("Base at 0x{:x}", (uint64_t) base);
        log("Calling all initialization");
        initializeImages();
        log("Done with initialization");
    }

    /*auto numClasses = objc_getClassList(nullptr, 0);
    if(numClasses > 0) {
        auto classes = new Class[numClasses];
        objc_getClassList(classes, numClasses);
        for(int i = 0; i < numClasses; i++)
            rewriteClass(classes[i]);
        delete[] classes;
    }*/

	log("Loaded! Trying to run main at 0x{:x}", main);
	auto state = CpuInstance.currentState();
	state->X0 = 1;
	auto argv = new const char*[1];
    auto mainPath = fmt::format("{}/{}", path, mainBinary);
	argv[0] = mainPath.c_str();
	state->X1 = (uint64_t) argv;
	CpuInstance.runFrom(main);
	log("Got to end of process ... ? Spinning.");
	while(true)
	    sleep(10);
}
