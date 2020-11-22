#pragma once

#include <string>
#include <pthread.h>
#include "fmt/format.h"

void _log(const std::string& str);

#define log(fmtstr, ...) do { _log(fmt::format("T#{:x}  " fmtstr, (uint64_t) pthread_self(), ##__VA_ARGS__)); } while(0)
