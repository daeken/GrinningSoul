#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#import <Foundation/Foundation.h>
#include <os/log.h>

int main() {
    /*{
        void* libdyld = dlopen("/usr/lib/system/libdyld.dylib", RTLD_NOW);
        if(libdyld == nullptr) {
            os_log(OS_LOG_DEFAULT, "Failed to get libdyld");
            return 4;
        }
        auto icf = (uint32_t(*)()) dlsym(libdyld, "_dyld_image_count");
        auto ic = icf();
        if(ic == 0) {
            os_log(OS_LOG_DEFAULT, "No images ... ?");
            return 5;
        }
        auto ginf = (const char*(*)(uint32_t)) dlsym(libdyld, "_dyld_get_image_name");
        auto gihf = (uint8_t*(*)(uint32_t)) dlsym(libdyld, "_dyld_get_image_header");
        for(auto i = 0; i < ic; ++i) {
            auto path = ginf(i);
            auto fn = strrchr(path, '/') + 1;
            if(strcmp(fn, "dyld_sim") != 0) continue;
            fflush(stdout);
            gihf(i)[0x7c9b0] = 1; // 12.1
            gihf(i)[0x89280] = 1; // 13.3
            break;
        }
    }*/
	auto lib = dlopen("%PATH%/libemuruntime.dylib", RTLD_NOW);
	if(lib == nullptr) {
        os_log(OS_LOG_DEFAULT, "Could not load libemuruntime; ensure that GrinningSoul has not moved. %{public}@", [NSString stringWithUTF8String: dlerror()]);
	    return 2;
	}
	auto kickoff = (void(*)(const char*)) dlsym(lib, "_Z7kickoffPKc");
    if(kickoff == nullptr) {
        os_log(OS_LOG_DEFAULT, "Could not get kickoff symbol from libemuruntime. %{public}@", [NSString stringWithUTF8String: dlerror()]);
        return 3;
    }
	kickoff("%PATH%");
}
