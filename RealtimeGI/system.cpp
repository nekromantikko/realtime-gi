#include "system.h"
#include <cstdio>
#include <stdarg.h>
#include <windows.h>
#include <iostream>
#include <fstream>

void Print(const char* fmt, ...) {
	char s[1025];
	va_list args;
	va_start(args, fmt);
	vsprintf_s(s, fmt, args);
	va_end(args);
	OutputDebugString(s);
}

// Memory is owned by caller
char* AllocFileBytes(const char* fname, u32& outLength) {
	std::ifstream file(fname, std::ios::ate | std::ios::binary);
	auto fileSize = file.tellg();
	char* buffer = (char*)calloc(1, fileSize);
	file.seekg(0);
	file.read(buffer, fileSize);
	file.close();

	outLength = (u32)fileSize;
	return buffer;
}