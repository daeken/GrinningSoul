#pragma once

#include "gs.h"
#include <string>
#include <vector>
#include <state.h>

extern std::vector<std::tuple<std::string, std::string, void(*)(CpuState*)>> allWrappers;
