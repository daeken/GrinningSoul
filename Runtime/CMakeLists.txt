cmake_minimum_required(VERSION 3.15)
project(Runtime)

set(CMAKE_CXX_STANDARD 20)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.72.0)
include_directories(${Boost_INCLUDE_DIRS})
find_package(llvm 9.0.1 REQUIRED CONFIG)

#set(USE_UNICORN ON)

#include_directories("/Users/cbrocious/projects/unicorn/include")
set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator13.2.sdk")
set(CMAKE_CXX_FLAGS "-Wno-deprecated-declarations -O2")
set(CMAKE_OSX_DEPLOYMENT_TARGET "" CACHE STRING "Empty for iOS" FORCE)
#set(CMAKE_SHARED_LINKER_FLAGS "-fsanitize=address")
#add_definitions(-fsanitize=address)
include_directories(/Users/daeken/projects/libmoonage/)
include_directories(/Users/daeken/projects/libmoonage/xbyak)
if(USE_UNICORN)
    add_definitions(-DUSE_UNICORN)
    include_directories(/Users/daeken/projects/unicorn/include)
    link_directories(../)
    link_libraries(unicorn)
else()
    link_directories(/Users/daeken/projects/libmoonage/cmake-build-simulator)
    link_libraries(moonage)
endif()
link_libraries(objc "-framework CoreFoundation" "-framework Foundation" "-framework Security")

get_target_property(ARMRUNTIME_SOURCES armruntime SOURCES)
list(TRANSFORM ARMRUNTIME_SOURCES PREPEND "../ArmRuntime/")
add_custom_command(
        OUTPUT ../Runtime/replacements.generated.h
        COMMAND python generateReplacements.py
        WORKING_DIRECTORY ../ArmRuntime
        DEPENDS ../ArmRuntime/generateReplacements.py ${ARMRUNTIME_SOURCES}
)
add_custom_target(replacementGeneration ALL DEPENDS ../ArmRuntime/generateReplacements.py ${ARMRUNTIME_SOURCES})
add_dependencies(armruntime replacementGeneration)

add_library(emuruntime SHARED
        kickoff.cpp cpu.cpp trampoliner.cpp trampoliner.h ../init.h objcWrappers.mm objcWrappers.h wrappers.h wrappers.cpp ../Runtime/replacements.generated.h swiftDemangler.cpp swiftDemangler.h swiftDemangler.cpp swiftDemangler.h
        repr.h record.h keychainTester.mm objcppWrappers.mm objcppWrappers.h logging.mm logging.h
        format.cc os.cc fmt/chrono.h fmt/color.h fmt/compile.h fmt/core.h fmt/format-inl.h fmt/format.h fmt/locale.h fmt/os.h fmt/ostream.h fmt/posix.h fmt/printf.h fmt/ranges.h
        exceptionHandlers.mm exceptionHandlers.h jmpWrappers.cpp jmpWrappers.h)
add_dependencies(emuruntime armruntime replacementGeneration)
llvm_config(emuruntime core)
add_custom_command(TARGET emuruntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:emuruntime> ../)
