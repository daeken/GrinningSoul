#include "wrappers.h"
#include "objcWrappers.h"
#include "objcppWrappers.h"
#include "jmpWrappers.h"

#include <iostream>
#include <ios>
#include <fcntl.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CommonCrypto/CommonHMAC.h>
using namespace std;

void wrap_sscanf(CpuState* state) {
    auto match = (const char*) state->X0;
    auto pattern = (const char*) state->X1;
    log("In wrapper for sscanf");
    log("Matching against '{}'", match);
    log("With pattern '{}'", pattern);
    auto sp = (void**) state->SP;
    auto _0 = *sp++;
    auto _1 = *sp++;
    auto _2 = *sp++;
    auto _3 = *sp++;
    auto _4 = *sp++;
    auto _5 = *sp++;
    auto _6 = *sp++;
    state->X0 = sscanf(match, pattern, _0, _1, _2, _3, _4, _5, _6);
}

void wrap_open(CpuState* state) {
    // This only exists because open() is fucking stupid and overloaded.
    log("In wrapper for open");
    auto pathname = (const char*) state->X0;
    auto flags = (int) state->X1;
    if(flags & O_CREAT)
        state->X0 = open(pathname, flags, (int) state->X2);
    else
        state->X0 = open(pathname, flags);
}

void wrap_openat(CpuState* state) {
    // This only exists because openat() is fucking stupid and overloaded.
    log("In wrapper for openat");
    auto dirfd = (int) state->X0;
    auto pathname = (const char*) state->X1;
    auto flags = (int) state->X2;
    if(flags & O_CREAT)
        state->X0 = openat(dirfd, pathname, flags, (int) state->X3);
    else
        state->X0 = openat(dirfd, pathname, flags);
}

void wrap_open_dprotected_np(CpuState* state) {
    // This only exists because open_dprotected_np() is fucking stupid and overloaded.
    log("In wrapper for open_dprotected_np");
    auto pathname = (const char*) state->X0;
    auto flags = (int) state->X1;
    auto _class = state->X2;
    auto dpflags = state->X3;
    if(flags & O_CREAT)
        state->X0 = open_dprotected_np(pathname, flags, _class, dpflags, (int) state->X4);
    else
        state->X0 = open_dprotected_np(pathname, flags, _class, dpflags);
}

void wrap_task_set_exception_ports(CpuState* state) {
    log("App attempted to set exception ports; silently ignoring.");
    state->X0 = KERN_SUCCESS;
}

void wrap_NSSetUncaughtExceptionHandler(CpuState* state) {
    log("App attempted to set NSException handler; silently ignoring.");
}

void wrap___ZSt13set_terminatePFvvE(CpuState* state) {
    log("App attempted to set C++ exception handler; silently ignoring.");
    state->X0 = 0;
}

void wrap_malloc(CpuState* state) {
    state->X0 = (uint64_t) malloc(state->X0);
}

void wrap_calloc(CpuState* state) {
    state->X0 = (uint64_t) calloc(state->X0, state->X1);
}

void wrap_free(CpuState* state) {
    free((void*) state->X0);
}

void wrap_strlen(CpuState* state) {
    state->X0 = strlen((const char*) state->X0);
}

void wrap_memcpy(CpuState* state) {
    state->X0 = (uint64_t) memcpy((void*) state->X0, (void*) state->X1, (size_t) state->X2);
}

void wrap_memcmp(CpuState* state) {
    state->X0 = (uint64_t) (int64_t) memcmp((void*) state->X0, (void*) state->X1, (size_t) state->X2);
}

void wrap_strcmp(CpuState* state) {
    state->X0 = (uint64_t) (int64_t) strcmp((const char*) state->X0, (const char*) state->X1);
}

void wrap_strncmp(CpuState* state) {
    state->X0 = (uint64_t) (int64_t) strncmp((const char*) state->X0, (const char*) state->X1, (size_t) state->X2);
}

void wrap_bzero(CpuState* state) {
    bzero((void*) state->X0, (size_t) state->X1);
}

void wrap_write(CpuState* state) {
    state->X0 = (uint64_t) write((int) state->X0, (const void*) state->X1, (size_t) state->X2);
}

void wrap___chkstk_darwin(CpuState* state) {
    log("Got call to ___chkstk_darwin");
}

void wrap__tlv_bootstrap(CpuState* state) {
    log("Caught and ignored tlv_bootstrap");
}

void wrap_fread(CpuState* state) {
    state->X0 = (uint64_t) fread((void*) state->X0, (size_t) state->X1, (size_t) state->X2, (FILE*) state->X3);
}

static inline unsigned char itoh(int i) {
    if (i > 9) return 'A' + (i - 10);
    return '0' + i;
}

string bufferToHex(const void* data, const size_t len) {
    string ret;
    for(auto i = 0; i < len; ++i) {
        ret += itoh((((uint8_t*) data)[i] >> 4) & 0xF);
        ret += itoh(((uint8_t*) data)[i] & 0xF);
    }
    return ret;
}

void wrap_CCHmac(CpuState* state) {
    auto algo = (CCHmacAlgorithm) state->X0;
    auto key = (const void*) state->X1;
    auto keyLength = (size_t) state->X2;
    auto data = (const void*) state->X3;
    auto dataLength = (size_t) state->X4;
    auto macOut = (void*) state->X5;
    log("Got call to CCHmac. Key {} Data {}", bufferToHex(key, keyLength), bufferToHex(data, dataLength));
    CCHmac(algo, key, keyLength, data, dataLength, macOut);
    log("CCHmac finished. MAC {}", bufferToHex(macOut, 20));
}

void wrap_fesetenv(CpuState* state) {
    log("Got call to fesetenv; dropping.");
}

void wrap___assert_rtn(CpuState* state) {
    CpuInstance.dumpRegs();
    log("Failed assertion (assert_rtn): function {} in {} (line {}) -- {}", (const char*) state->X0, (const char*) state->X1, state->X2, (const char*) state->X3);
    BAILOUT();
}

vector<std::tuple<string, string, void(*)(CpuState*)>> allWrappers = {
    {"libobjc.dylib", "_objc_msgSend", wrap_objc_msgSend},
    {"libobjc.A.dylib", "_objc_msgSend", wrap_objc_msgSend},
    {"libobjc.dylib", "_objc_msgSendSuper2", wrap_objc_msgSendSuper2},
    {"libobjc.A.dylib", "_objc_msgSendSuper2", wrap_objc_msgSendSuper2},
    {"libobjc.dylib", "_class_getInstanceMethod", wrap_class_getInstanceMethod},
    {"libobjc.A.dylib", "_class_getInstanceMethod", wrap_class_getInstanceMethod},
    {"libobjc.dylib", "_class_addMethod", wrap_class_addMethod},
    {"libobjc.A.dylib", "_class_addMethod", wrap_class_addMethod},
    {"libobjc.dylib", "_class_replaceMethod", wrap_class_replaceMethod},
    {"libobjc.A.dylib", "_class_replaceMethod", wrap_class_replaceMethod},
    {"libobjc.dylib", "_method_getImplementation", wrap_method_getImplementation},
    {"libobjc.A.dylib", "_method_getImplementation", wrap_method_getImplementation},
    {"libobjc.dylib", "_method_setImplementation", wrap_method_setImplementation},
    {"libobjc.A.dylib", "_method_setImplementation", wrap_method_setImplementation},
    {"libobjc.dylib", "_imp_implementationWithBlock", wrap_imp_implementationWithBlock},
    {"libobjc.A.dylib", "_imp_implementationWithBlock", wrap_imp_implementationWithBlock},
    {"libSystem.B.dylib", "_sscanf", wrap_sscanf},
    {"libSystem.B.dylib", "_free", wrap_free},
    {"libSystem.B.dylib", "_malloc", wrap_malloc},
    {"libSystem.B.dylib", "_calloc", wrap_calloc},
    {"libSystem.B.dylib", "_strlen", wrap_strlen},
    {"libSystem.B.dylib", "_memcpy", wrap_memcpy},
    {"libSystem.B.dylib", "_memcmp", wrap_memcmp},
    {"libSystem.B.dylib", "_strcmp", wrap_strcmp},
    {"libSystem.B.dylib", "_strncmp", wrap_strncmp},
    {"libSystem.B.dylib", "_open", wrap_open},
    {"libSystem.B.dylib", "_openat", wrap_openat},
    {"libSystem.B.dylib", "_bzero", wrap_bzero},
    {"libSystem.B.dylib", "_open_dprotected_np", wrap_open_dprotected_np},
    {"libSystem.B.dylib", "_task_set_exception_ports", wrap_task_set_exception_ports},
    {"libSystem.B.dylib", "___chkstk_darwin", wrap___chkstk_darwin},
    {"libSystem.B.dylib", "__tlv_bootstrap", wrap__tlv_bootstrap},
    {"libSystem.B.dylib", "_fread", wrap_fread},
    {"libSystem.B.dylib", "_write", wrap_write},
    {"libSystem.B.dylib", "_setjmp", wrap_setjmp},
    {"libSystem.B.dylib", "_sigsetjmp", wrap_sigsetjmp},
    {"libSystem.B.dylib", "_longjmp", wrap_longjmp},
    {"libSystem.B.dylib", "_siglongjmp", wrap_siglongjmp},
    {"libSystem.B.dylib", "_CCHmac", wrap_CCHmac},
    {"libSystem.B.dylib", "_fesetenv", wrap_fesetenv},
    {"libSystem.B.dylib", "___assert_rtn", wrap___assert_rtn},
    {"Foundation", "_NSSetUncaughtExceptionHandler", wrap_NSSetUncaughtExceptionHandler},
    {"libc++.1.dylib", "__ZSt13set_terminatePFvvE", wrap___ZSt13set_terminatePFvvE},
    {"Security", "_SecItemAdd", wrap_SecItemAdd},
    {"Security", "_SecItemCopyMatching", wrap_SecItemCopyMatching},
};
