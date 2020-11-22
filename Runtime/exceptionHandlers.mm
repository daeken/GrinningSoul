#include <Foundation/Foundation.h>
#import <pthread.h>
#include "exceptionHandlers.h"
#include "gs.h"

const char* asStr(NSString* str) {
    return [str UTF8String];
}

void globalNSExceptionHandler(NSException* exc) {
    log("Unhandled NSException!\nName: {}\nReason: {}\nUserInfo: {}\nTop Symbol On Call Stack: {}",
            asStr([exc name]), asStr([exc reason]),
            [exc userInfo] != nil ? asStr([[exc userInfo] description]) : "nil",
            [exc callStackSymbols] != nil && [[exc callStackSymbols] count] != 0 ? asStr([[[exc callStackSymbols] firstObject] description]) : "nil");
    BAILOUT();
}

void registerExceptionHandlers() {
    NSSetUncaughtExceptionHandler(globalNSExceptionHandler);
}
