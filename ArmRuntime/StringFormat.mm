#import <Foundation/Foundation.h>
#include "printf.h"

#include <string>
using namespace std;

void lambdaCaller(char character, void* arg) {
    auto lambda = (function<void(char)>*) arg;
    (*lambda)(character);
}

string formatString(const char* format, va_list argList) {
    string ret;
    function<void(char)> lambda = [&](auto c) {
        ret += c;
    };
    fctprintfv(lambdaCaller, &lambda, true, format, argList);
    return ret;
}

static auto bdLogCore = (void(*)(const char*)) 0xDEADBEEFCAFEBAB0;

void log(const char* format, ...) {
    va_list argList;
    va_start(argList, format);
    bdLogCore(formatString(format, argList).c_str());
    va_end(argList);
}

extern "C" {

/// REPLACE initWithFormat:
__attribute__((visibility("default")))
NSString* replace_initWithFormat_(id self, SEL _cmd, NSString* format, ...) {
    //log("Got call to initWithFormat:! '%@'", format);
    va_list argList;
    va_start(argList, format);
    auto str = formatString([format UTF8String], argList);
    va_end(argList);
    return [self initWithUTF8String: str.c_str()];
}

/// REPLACE initWithFormat:arguments:
__attribute__((visibility("default")))
NSString* replace_initWithFormat_arguments_(id self, SEL _cmd, NSString* format, va_list argList) {
    //log("Got call to initWithFormat:arguments:! '%@'", format);
    auto str = formatString([format UTF8String], argList);
    return [self initWithUTF8String:str.c_str()];
}

/// REPLACE deferredLocalizedIntentsStringWithFormat:
__attribute__((visibility("default")))
NSString* replace_deferredLocalizedIntentsStringWithFormat_(id self, SEL _cmd, NSString* format, ...) {
    //log("Got call to deferredLocalizedIntentsStringWithFormat:! '%@'", format);
    va_list argList;
    va_start(argList, format);
    auto str = formatString([format UTF8String], argList);
    va_end(argList);
    return [NSString stringWithUTF8String:str.c_str()];
}

/// REPLACE initWithFormat:locale:
__attribute__((visibility("default")))
NSString* replace_initWithFormat_locale_(id self, SEL _cmd, NSString* format, id locale, ...) {
    //log("Got call to initWithFormat:locale:! '%@'", format);
    va_list argList;
    va_start(argList, locale);
    auto str = formatString([format UTF8String], argList);
    va_end(argList);
    return [self initWithUTF8String:str.c_str()];
}

/// REPLACE initWithFormat:locale:arguments:
__attribute__((visibility("default")))
NSString* replace_initWithFormat_locale_arguments_(id self, SEL _cmd, NSString* format, id locale, va_list argList) {
    //log("Got call to initWithFormat:locale:arguments:! '%@'", format);
    auto str = formatString([format UTF8String], argList);
    return [self initWithUTF8String:str.c_str()];
}

/// REPLACE stringByAppendingFormat:
__attribute__((visibility("default")))
NSString* replace_stringByAppendingFormat_(id self, SEL _cmd, NSString* format, ...) {
    //log("Got call to stringByAppendingFormat:! '%@'", format);
    va_list argList;
    va_start(argList, format);
    auto str = formatString([format UTF8String], argList);
    auto nstr = [[NSString stringWithUTF8String:str.c_str()] retain];
    va_end(argList);
    auto ret = [self stringByAppendingString:nstr];
    [nstr release];
    return ret;
}

/// REPLACE appendFormat:
__attribute__((visibility("default")))
id replace_appendFormat_(id self, SEL _cmd, NSString* format, ...) {
    //log("Got call to appendFormat:! '%@'", format);
    va_list argList;
    va_start(argList, format);
    auto str = formatString([format UTF8String], argList);
    //printf_objc("Formatted string to append: '%s' (%i bytes)\n", str, strlen(str));
    auto nstr = [[[NSString alloc] initWithBytes:str.c_str() length:str.length() encoding:NSASCIIStringEncoding] retain];
    va_end(argList);
    //printf_objc("NSString: '%@'\n", nstr);
    [self appendString:nstr];
    [nstr release];
    return self;
}

/// REPLACE stringWithFormat:
__attribute__((visibility("default")))
NSString* replace_stringWithFormat_(id self, SEL _cmd, NSString* format, ...) {
    //log("Got call to stringWithFormat:! '%@'", format);
    va_list argList;
    va_start(argList, format);
    auto str = formatString([format UTF8String], argList);
    id nstr = [self stringWithUTF8String:str.c_str()];
    va_end(argList);
    return nstr;
}

/// REPLACE CFStringCreateWithFormatAndArguments
__attribute__((visibility("default")))
CFStringRef replace_CFStringCreateWithFormatAndArguments(CFAllocatorRef alloc, CFDictionaryRef formatOptions, CFStringRef format, va_list arguments) {
    auto flen = CFStringGetLength(format);
    auto buf = new char[flen + 1];
    CFStringGetCString(format, buf, flen + 1, kCFStringEncodingUTF8);
    //log("Got call to CFStringCreateWithFormatAndArguments! '%s'", buf);
    auto str = formatString(buf, arguments);
    return CFStringCreateWithBytes(alloc, (const UInt8*) str.c_str(), str.length(), kCFStringEncodingUTF8, false);
}

/// REPLACE predicateWithFormat:arguments:
__attribute__((visibility("default")))
NSPredicate* replace_predicateWithFormat_arguments_(id self, SEL _cmd, NSString* format, va_list args) {
    auto arr = [[NSMutableArray alloc] init];
    auto ptr = [format UTF8String];
    while(*ptr != 0) {
        char c = *ptr++;
        if(c == '%') {
            c = *ptr;
            switch(c) {
                case '%':
                    ptr++;
                    break;

                case 'K':
                case '@':
                    ptr++;
                    [arr addObject:va_arg(args, id)];
                    break;

                case 'c':
                    ptr++;
                    [arr addObject:[NSNumber numberWithChar: (char) va_arg(args, NSInteger)]];
                    break;

                case 'C':
                    ptr++;
                    [arr addObject:[NSNumber numberWithShort: (short) va_arg(args, NSInteger)]];
                    break;

                case 'l':
                    ptr++;
                    if(*ptr == 'l') {
                        switch(*++ptr) {
                            case 'd':
                                [arr addObject:[NSNumber numberWithLongLong: va_arg(args, int64_t)]];
                                break;
                        }
                    } else
                        switch(*ptr++) {
                            case 'd':
                                [arr addObject:[NSNumber numberWithInt: va_arg(args, int)]];
                                break;
                        }
                    break;

                case 'd':
                case 'D':
                case 'i':
                    ptr++;
                    [arr addObject:[NSNumber numberWithInt: va_arg(args, int)]];
                    break;

                case 'o':
                case 'O':
                case 'u':
                case 'U':
                case 'x':
                case 'X':
                    ptr++;
                    [arr addObject:[NSNumber numberWithUnsignedInt: va_arg(args, unsigned)]];
                    break;

                case 'e':
                case 'E':
                case 'f':
                case 'g':
                case 'G':
                    ptr++;
                    [arr addObject:[NSNumber numberWithDouble: va_arg(args, double)]];
                    break;

                case 'h':
                    ptr++;
                    if(*ptr != 0) {
                        c = *ptr;
                        if(c == 'i')
                            [arr addObject:[NSNumber numberWithShort: (short) va_arg(args, NSInteger)]];
                        if(c == 'u')
                            [arr addObject:[NSNumber numberWithUnsignedShort: (unsigned short) va_arg(args, NSInteger)]];
                    }
                    break;

                case 'q':
                    ptr++;
                    if(*ptr != 0) {
                        c = *ptr;
                        if(c == 'i')
                            [arr addObject:[NSNumber numberWithLongLong: va_arg(args, long long)]];
                        if(c == 'u' || c == 'x' || c == 'X')
                            [arr addObject:[NSNumber numberWithUnsignedLongLong: va_arg(args, unsigned long long)]];
                    }
                    break;
            }
        } else if(c == '\'') {
            while(*ptr != 0)
                if(*ptr++ == '\'')
                    break;
        } else if(c == '"') {
            while(*ptr != 0)
                if(*ptr++ == '"')
                    break;
        }
    }
    //printf_objc("predicateWithFormat '%@' args %i\n", format, [arr count]);
    return [NSPredicate predicateWithFormat: format argumentArray: arr];
}

/// REPLACE predicateWithFormat:
__attribute__((visibility("default")))
NSPredicate* replace_predicateWithFormat_(id self, SEL _cmd, NSString* format, ...) {
    va_list args;
    va_start(args, format);
    auto p = replace_predicateWithFormat_arguments_(self, _cmd, format, args);
    va_end(args);
    return p;
}

}
