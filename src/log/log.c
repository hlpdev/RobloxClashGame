#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#include "log/log.h"

static LogLevel current_level;
static FILE* log_file = NULL;
static char filepath_fmt[256];
static int current_hour = -1;

LogLevel log_level_from_string(const char* str) {
  if (strcmp(str, "debug") == 0) return LOG_DEBUG;
  if (strcmp(str, "error") == 0) return LOG_ERROR;
  return LOG_INFO;
}

static bool open_log_for_time(struct tm* time) {
  char filepath[512];
  strftime(filepath, sizeof(filepath), filepath_fmt, time);

  if (log_file) {
    fclose(log_file);
    log_file = NULL;
  }

  log_file = fopen(filepath, "a");
  if (!log_file) {
    fprintf(stderr, "failed to open log file: %s\n", filepath);
    return false;
  }

  current_hour = time->tm_hour;
  return true;
}

bool log_init(LogLevel level, const char* filepath) {
  setbuf(stdout, NULL);
  current_level = level;
  strncpy(filepath_fmt, filepath, sizeof(filepath_fmt) - 1);

  time_t now = time(NULL);
  struct tm* t = gmtime(&now);
  return open_log_for_time(t);
}

void log_rotate(void) {
  time_t now = time(NULL);
  struct tm* t = gmtime(&now);

  if (t->tm_hour != current_hour) {
    open_log_for_time(t);
  }
}

void log_shutdown(void) {
  if (log_file) {
    fclose(log_file);
    log_file = NULL;
  }
}

static void write_log(const char* label, const char* fmt, va_list args) {
  log_rotate();

  time_t now = time(NULL);
  struct tm* time = gmtime(&now);
  char timestamp[32];

  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", time);

  char message[2048];
  vsnprintf(message, sizeof(message), fmt, args);

  fprintf(stdout, "[%s] %s %s\n", label, timestamp, message);
  if (log_file) {
    fprintf(log_file, "[%s] %s %s\n", label, timestamp, message);
  }
}

void log_debug(const char* fmt, ...) {
  if (current_level > LOG_DEBUG) {
    return;
  }

  va_list args;
  va_start(args, fmt);

  write_log("DEBUG", fmt, args);
  va_end(args);
}

void log_info(const char* fmt, ...) {
  if (current_level > LOG_INFO) {
    return;
  }

  va_list args;
  va_start(args, fmt);

  write_log("INFO", fmt, args);
  va_end(args);
}

void log_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  write_log("ERROR", fmt, args);
  va_end(args);
}
