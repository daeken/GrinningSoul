#pragma once

#include "cpu.h"
#include <state.h>

void wrap_setjmp(CpuState* state);
void wrap_sigsetjmp(CpuState* state);
void wrap_longjmp(CpuState* state);
void wrap_siglongjmp(CpuState* state);
