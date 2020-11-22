#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#import <Foundation/Foundation.h>
#import "printf.h"
#include <vector>
using namespace std;

bool inheritsFrom(Class tcls, Class scls) {
    while(true) {
        if(tcls == scls)
            return true;
        auto parent = class_getSuperclass(tcls);
        if(parent == tcls)
            return false;
        tcls = parent;
    }
}

id NSMutableSet_initWithObjects_(NSMutableSet* set, id firstObj, va_list ap) {
    vector<id> objects;
    if(firstObj != nil) {
        objects.push_back(firstObj);
        while(true) {
            id nobj = va_arg(ap, id);
            if(nobj == nil)
                break;
            objects.push_back(nobj);
        }
    }

    id* objarr = new id[objects.size()];
    copy(objects.begin(), objects.end(), objarr);
    set = [set initWithObjects: objarr count: objects.size()];
    delete[] objarr;
    return set;
}

id NSMutableArray_initWithObjects_(NSMutableArray* array, id firstObj, va_list ap) {
    vector<id> objects;
    if(firstObj != nil) {
        objects.push_back(firstObj);
        while(true) {
            id nobj = va_arg(ap, id);
            if(nobj == nil)
                break;
            objects.push_back(nobj);
        }
    }

    id* objarr = new id[objects.size()];
    copy(objects.begin(), objects.end(), objarr);
    array = [array initWithObjects: objarr count: objects.size()];
    delete[] objarr;
    return array;
}

extern "C" {

/// REPLACE arrayWithObjects:
__attribute__((visibility("default")))
id replace_arrayWithObjects_(id self, SEL _cmd, id firstObj, ...) {
    va_list ap;
    va_start(ap, firstObj);
    vector<id> objects;
    if(firstObj != nil) {
        objects.push_back(firstObj);
        while(true) {
            id nobj = va_arg(ap, id);
            if(nobj == nil)
                break;
            objects.push_back(nobj);
        }
    }
    va_end(ap);
    id* objarr = new id[objects.size()];
    copy(objects.begin(), objects.end(), objarr);
    id ret = [self arrayWithObjects:objarr count:objects.size()];
    delete[] objarr;
    return ret;
}

/// REPLACE setWithObjects:
__attribute__((visibility("default")))
id replace_setWithObjects_(id self, SEL _cmd, id firstObj, ...) {
    va_list ap;
    va_start(ap, firstObj);
    vector<id> objects;
    if(firstObj != nil) {
        objects.push_back(firstObj);
        while(true) {
            id nobj = va_arg(ap, id);
            if(nobj == nil)
                break;
            objects.push_back(nobj);
        }
    }
    va_end(ap);
    id* objarr = new id[objects.size()];
    copy(objects.begin(), objects.end(), objarr);
    id ret = [self setWithObjects:objarr count:objects.size()];
    delete[] objarr;
    return ret;
}

/// REPLACE dictionaryWithObjectsAndKeys:
__attribute__((visibility("default")))
id replace_dictionaryWithObjectsAndKeys_(id self, SEL _cmd, id firstObj, ...) {
    va_list ap;
    va_start(ap, firstObj);
    vector<id> objects, keys;
    if(firstObj != nil) {
        objects.push_back(firstObj);
        keys.push_back(va_arg(ap, id));
        while(true) {
            id nobj = va_arg(ap, id);
            if(nobj == nil)
                break;
            objects.push_back(nobj);
            keys.push_back(va_arg(ap, id));
        }
    }
    va_end(ap);
    id* objarr = new id[objects.size()];
    copy(objects.begin(), objects.end(), objarr);
    id* keyarr = new id[keys.size()];
    copy(keys.begin(), keys.end(), keyarr);
    id ret = [self dictionaryWithObjects: objarr forKeys: keyarr count: objects.size()];
    delete[] objarr;
    delete[] keyarr;
    return ret;
}

/// REPLACE initWithObjectsAndKeys:
__attribute__((visibility("default")))
id replace_initWithObjectsAndKeys_(id self, SEL _cmd, id firstObj, ...) {
    va_list ap;
    va_start(ap, firstObj);
    vector<id> objects, keys;
    if(firstObj != nil) {
        objects.push_back(firstObj);
        keys.push_back(va_arg(ap, id));
        while(true) {
            id nobj = va_arg(ap, id);
            if(nobj == nil)
                break;
            objects.push_back(nobj);
            keys.push_back(va_arg(ap, id));
        }
    }
    va_end(ap);
    id* objarr = new id[objects.size()];
    copy(objects.begin(), objects.end(), objarr);
    id* keyarr = new id[keys.size()];
    copy(keys.begin(), keys.end(), keyarr);
    id ret = [self initWithObjects: objarr forKeys: keyarr count: objects.size()];
    delete[] objarr;
    delete[] keyarr;
    return ret;
}

/// REPLACE initWithObjects:
__attribute__((visibility("default")))
id replace_initWithObjects_(id self, SEL _cmd, id firstObj, ...) {
    Class cls = object_getClass(self);
    va_list ap;
    va_start(ap, firstObj);
    if(inheritsFrom(cls, [NSMutableSet class])) {
        id ret = NSMutableSet_initWithObjects_((NSMutableSet*) self, firstObj, ap);
        va_end(ap);
        return ret;
    } else if(inheritsFrom(cls, [NSMutableArray class])) {
        id ret = NSMutableArray_initWithObjects_((NSMutableArray*) self, firstObj, ap);
        va_end(ap);
        return ret;
    } else {
        exit(1);
    }
}

}
