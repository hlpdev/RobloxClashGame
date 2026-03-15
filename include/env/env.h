#pragma once

#include <stdbool.h>

typedef struct {
  const char* database_connection;
  const char* redis_connection;

  const char* roblox_server_secret;
  const char* admin_portal_secret;

  const char* roblox_api_key;

  const char* log_level;
  const char* log_file;
} Environment;

bool env_load(Environment* env);
