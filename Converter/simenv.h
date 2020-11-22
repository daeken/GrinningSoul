//
// Created by Cody Brocious on 2/12/20.
//

#ifndef CONVERTER_SIMENV_H
#define CONVERTER_SIMENV_H

#include <string>

std::string exec(const char* cmd);

class SimEnv {
public:
    const char *SdkPath, *RuntimeRoot;
    SimEnv();
};

#ifdef SIMDEF
SimEnv* SimEnv = new class SimEnv();
#else
extern SimEnv* SimEnv;
#endif

#endif //CONVERTER_SIMENV_H
