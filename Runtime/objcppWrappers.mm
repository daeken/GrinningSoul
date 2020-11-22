#import <Foundation/Foundation.h>
#include <state.h>
#include "gs.h"

#include <iostream>
#include <ios>
using namespace std;

void hack_printRecursiveDescription(id object) {
    log("Recursive description of object 0x{:x}", (uint64_t) object);
    log("Description: {}", [[object recursiveDescription] UTF8String]);
    BAILOUT();
}

static inline unsigned char itoh(int i) {
    if (i > 9) return 'A' + (i - 10);
    return '0' + i;
}

NSString * NSDataToHex(NSData *data) {
    NSUInteger i, len;
    unsigned char *buf, *bytes;

    len = data.length;
    bytes = (unsigned char*)data.bytes;
    buf = new unsigned char[len*2];

    for (i=0; i<len; i++) {
        buf[i*2] = itoh((bytes[i] >> 4) & 0xF);
        buf[i*2+1] = itoh(bytes[i] & 0xF);
    }

    return [[NSString alloc] initWithBytesNoCopy:buf
                                          length:len*2
                                        encoding:NSASCIIStringEncoding
                                    freeWhenDone:YES];
}

void wrap_SecItemAdd(CpuState* state) {
    log("In wrapper for SecItemAdd!");
    auto attributes = (CFDictionaryRef) state->X0;
    auto result = (CFTypeRef*) state->X1;
    auto count = CFDictionaryGetCount(attributes);
    log("Attributes contains {} kv pairs", count);
    auto keys = new void*[count];
    auto values = new void*[count];
    CFDictionaryGetKeysAndValues(attributes, (const void**) keys, (const void**) values);
    log("Got attributes");
    for(auto i = 0; i < count; ++i) {
        auto klen = CFStringGetLength((CFStringRef) keys[i]);
        auto key = new char[klen + 1];
        CFStringGetCString((CFStringRef) keys[i], key, klen + 1, kCFStringEncodingUTF8);
        log("\t {} -- {}: {}", i, key, [[(id) values[i] description] UTF8String]);
        delete[] key;
    }

    auto ret = SecItemAdd(attributes, result);
    log("SecItemAdd returned {}", ret);
    state->X0 = (uint64_t) ret;
}

void wrap_SecItemCopyMatching(CpuState* state) {
    log("In wrapper for SecItemCopyMatching!");
    auto attributes = (CFDictionaryRef) state->X0;
    auto result = (CFTypeRef*) state->X1;
    auto count = CFDictionaryGetCount(attributes);
    log("Attributes contains {} kv pairs", count);
    auto keys = new void*[count];
    auto values = new void*[count];
    CFDictionaryGetKeysAndValues(attributes, (const void**) keys, (const void**) values);
    log("Got attributes");
    for(auto i = 0; i < count; ++i) {
        auto klen = CFStringGetLength((CFStringRef) keys[i]);
        auto key = new char[klen + 1];
        CFStringGetCString((CFStringRef) keys[i], key, klen + 1, kCFStringEncodingUTF8);
        log("\t {} -- {}: {}", i, key, [[(id) values[i] description] UTF8String]);
        delete[] key;
    }

    auto ret = SecItemCopyMatching(attributes, result);
    log("SecItemCopyMatching returned {}", ret);
    if(result != nullptr && *result != nullptr) {
        id obj = (__bridge id) *result;
        if([obj isKindOfClass:[NSData class]])
            log("SecItemCopyMatching returned this NSData: {}", [NSDataToHex((__bridge NSData*) *result) UTF8String]);
        else
            log("SecItemCopyMatching returned this data: {}", [[obj description] UTF8String]);
    }
    state->X0 = (uint64_t) ret;
}
