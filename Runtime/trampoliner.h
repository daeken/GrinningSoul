#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <iostream>
#include <ios>
using namespace std;

class BaseSignature;

const uint64_t ARM_TO_NATIVE = 1ULL << 62ULL;
const uint64_t NATIVE_TO_ARM = 2ULL << 62ULL;

struct TrampolineTrampoline {
    uint64_t trampoline, target;
    BaseSignature* signature;
    uint64_t canary;

    inline bool isArmToNative() { return (trampoline & ARM_TO_NATIVE) == ARM_TO_NATIVE; }
    inline bool isNativeToArm() { return (trampoline & NATIVE_TO_ARM) == NATIVE_TO_ARM; }
};

class Trampoliner {
public:
    Trampoliner();
    uint64_t getKnownTrampoline(uint64_t target);
    uint64_t getKnownInverse(uint64_t target);
    uint64_t getANTrampoline(uint64_t target, const string& name, const string& signature);
    uint64_t getNATrampoline(uint64_t target, const string& name, const string& signature);
    uint64_t getANTrampoline(uint64_t target, BaseSignature* signature);
    uint64_t getNATrampoline(uint64_t target, BaseSignature* signature);
    TrampolineTrampoline* allocateTrampolineTrampoline(uint64_t trampoline, uint64_t target, BaseSignature* signature);
    inline TrampolineTrampoline* asTrampoline(uint64_t addr) {
        if(addr < (uint64_t) trampolineBase || addr >= (uint64_t) trampolineBase + sizeof(TrampolineTrampoline) * trampolineMax)
            return nullptr;
        return (TrampolineTrampoline*) addr;
    }

    inline void checkCanaries() {
        if(canary1 != 0xDEADBEEFCAFEBABEULL) {
            log("Canary 1 corrupted!");
            BAILOUT();
        }
        if(canary2 != 0xDEADBEEFCAFEBABEULL) {
            log("Canary 2 corrupted!");
            BAILOUT();
        }
        for(auto i = 0; i < trampolineCounter; ++i) {
            auto t = &trampolineBase[i];
            if(~t->canary != t->target) {
                log("Canary value in trampoline {} at 0x{:x} invalid! Expected 0x{:x} and got 0x{:x}", i, (uint64_t) t, ~t->target);
                BAILOUT();
            }
        }
    }

    uint64_t canary1 = 0xDEADBEEFCAFEBABEULL;
    mutex mutex;
    uint64_t canary2 = 0xDEADBEEFCAFEBABEULL;
    unordered_map<uint64_t, uint64_t> *instances, *inverses;
    unordered_map<string, uint64_t> armToNative, nativeToArm;
    static const uint64_t trampolineMax = 1048576;
    TrampolineTrampoline* trampolineBase;
    int trampolineCounter = 0;
};

extern Trampoliner TrampolinerInstance;
