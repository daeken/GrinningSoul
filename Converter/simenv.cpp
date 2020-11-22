//
// Created by Cody Brocious on 2/12/20.
//

#define SIMDEF
#include "simenv.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <boost/algorithm/string/trim.hpp>

std::string exec(const char* cmd) {
    std::array<char, 128> buffer{};
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

SimEnv::SimEnv() {
    auto path = exec("xcodebuild -version -sdk iphonesimulator Path");
    boost::algorithm::trim(path);
    SdkPath = strdup(path.c_str());
    RuntimeRoot = "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Library/Developer/CoreSimulator/Profiles/Runtimes/iOS.simruntime/Contents/Resources/RuntimeRoot";
}
