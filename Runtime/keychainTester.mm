#import <Foundation/Foundation.h>

#include <ios>
#include <iostream>
#include <string>
#include "gs.h"
using namespace std;

const char* canaryKey = "GS_KeychainCanaryUsername";
const char* canaryValue = "GS_KeychainCanaryPassword";

NSString* toNSS(const string& str) {
    return [NSString stringWithCString:str.c_str()];
}

tuple<bool, string> getKeychainItem(const string& key) {
    auto query = @{
            (id) kSecAttrAccount: (id) toNSS(key),
            (id) kSecClass: (id) kSecClassGenericPassword,
            (id) kSecAttrService: (id) @"GrinningSoul",
            (id) kSecAttrAccessible: (id) kSecAttrAccessibleWhenUnlocked,
            (id) kSecReturnRef: @YES
    };
    CFTypeRef res;
    auto ret = SecItemCopyMatching((__bridge CFDictionaryRef) query, &res);
    if(ret != errSecSuccess) {
        log("SecItemCopyMatching failed with status code {}", ret);
        return {false, ""};
    }

    auto data = (__bridge NSData*) res;
    log("SecItemCopyMatching succeeded.");
    return {true, ""};
}

bool putKeychainItem(const string& key, const string& value) {
    auto query = @{
        (id) kSecAttrAccount: (id) toNSS(key),
        (id) kSecValueData: (id) [NSData dataWithBytes: (const void*) value.c_str() length: value.length()],
        (id) kSecClass: (id) kSecClassGenericPassword,
        (id) kSecAttrService: (id) @"GrinningSoul",
        (id) kSecAttrAccessible: (id) kSecAttrAccessibleWhenUnlocked
    };
    auto ret = SecItemAdd((__bridge CFDictionaryRef) query, nullptr);
    if(ret == errSecSuccess) {
        log("SecItemAdd succeeded!");
        return true;
    }
    log("SecItemAdd failed with status code {}", ret);
    return false;
}

bool isCanaryValueInKeychain() {
    auto [success, value] = getKeychainItem(canaryKey);
    return success;// && value == canaryValue;
}

bool isKeychainUsable() {
    if(isCanaryValueInKeychain())
        return true;
    if(!putKeychainItem(canaryKey, canaryValue))
        return false;
    return isCanaryValueInKeychain();
}
