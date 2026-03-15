#pragma once

#include <stdbool.h>

typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_ERROR
} LogLevel;

bool log_init(LogLevel level, const char* filepath);
void log_rotate(void);
void log_shutdown(void);

void log_debug(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_error(const char* fmt, ...);

LogLevel log_level_from_string(const char* str);
