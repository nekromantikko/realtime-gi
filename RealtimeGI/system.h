#pragma once

#undef DEBUG_LOG
#define DEBUG_LOG(fmt, ...) Print("%s: " fmt " (%s, line %d)\n", \
    __func__, __VA_ARGS__, __FILE__, __LINE__)

#undef DEBUG_ERROR
#define DEBUG_ERROR(fmt, ...) DEBUG_LOG(fmt, __VA_ARGS__); exit(-1)

void Print(const char* fmt, ...);