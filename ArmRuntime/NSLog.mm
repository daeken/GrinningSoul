#include <Foundation/Foundation.h>
#include "printf.h"

void log(const char* format, ...);

extern "C" {

/// REPLACE NSLogv
__attribute__((visibility("default")))
void replace_NSLogv(NSString* format, va_list args) {
    log("NSLogv! '%@'", format);
    vprintf_objc([format UTF8String], args);
    printf_objc("\n");
}

/// REPLACE NSLog
__attribute__((visibility("default")))
void replace_NSLog(NSString* format, ...) {
    log("NSLog! '%@'", format);
    va_list ap;
    va_start(ap, format);
    replace_NSLogv(format, ap);
    va_end(ap);
}

}
