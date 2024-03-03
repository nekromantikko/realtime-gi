#include "system.h"
#include <cstdio>
#include <stdarg.h>
#include <windows.h>

void Print(const char* fmt, ...) {
	char s[1025];
	va_list args;
	va_start(args, fmt);
	vsprintf_s(s, fmt, args);
	va_end(args);
	OutputDebugString(s);
}