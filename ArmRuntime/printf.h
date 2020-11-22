#pragma once

extern "C" {
int printf_objc(const char* format, ...);
int vprintf_objc(const char* format, va_list va);
int fctprintf(void (* out)(char character, void* arg), void* arg, bool isObjC, const char* format, ...);
int fctprintfv(void (* out)(char character, void* arg), void* arg, bool isObjC, const char* format, va_list argList);
}
