cmake_minimum_required(VERSION 3.15)
project(ArmRuntime)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS13.2.sdk")
set(CMAKE_CXX_FLAGS "-Wno-deprecated-declarations -mios-simulator-version-min=10.0")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.0" CACHE STRING "Non-empty for iOS" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64")
link_libraries("-framework Foundation" "-framework CoreFoundation")

add_library(armruntime SHARED printf.mm scanf.cpp NSLog.mm StringFormat.mm asl.mm Collections.mm memory.cpp swiftStdlib.cpp)
add_custom_command(TARGET armruntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:armruntime> ../)
