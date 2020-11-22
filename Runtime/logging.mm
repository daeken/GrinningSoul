#include "logging.h"
#import <Foundation/Foundation.h>
#include <mutex>
#include <os/log.h>
using namespace std;

void _log(const string& str) {
    os_log(OS_LOG_DEFAULT, "%{public}@", [NSString stringWithUTF8String:str.c_str()]);
}
