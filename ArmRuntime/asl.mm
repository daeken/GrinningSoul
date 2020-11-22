#include <cstdarg>
#include "printf.h"
#include <asl.h>

extern "C" {

/// REPLACE asl_log
__attribute__((visibility("default")))
int replace_asl_log(aslclient asl, aslmsg msg, int level, const char* format, ...) {
    printf_objc("Got call to asl_log\n");
    va_list ap;
    va_start(ap, format);
    vprintf_objc(format, ap);
    va_end(ap);
    printf_objc("\n----asl_log done\n");
    return 0;
}

/// REPLACE asl_vlog
__attribute__((visibility("default")))
int replace_asl_vlog(aslclient asl, aslmsg msg, int level, const char* format, va_list ap) {
    printf_objc("Got call to asl_vlog\n");
    vprintf_objc(format, ap);
    printf_objc("\n----asl_vlog done\n");
    return 0;
}

}
