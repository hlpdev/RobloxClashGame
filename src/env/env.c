#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "env/env.h"

static const char* require_env(const char* key) {
  const char* val = getenv(key);

  if (!val) {
    fprintf(stderr, "missing required environment variable: %s\n", key);
  }

  return val;
}

bool env_load(Environment* env) {
  env->database_connection  = require_env("DATABASE_CONNECTION");
  env->redis_connection     = require_env("REDIS_CONNECTION");
  env->roblox_server_secret = require_env("ROBLOX_SERVER_SECRET");
  env->admin_portal_secret  = require_env("ADMIN_PORTAL_SECRET");
  env->roblox_api_key       = require_env("ROBLOX_API_KEY");
  env->log_level            = require_env("LOG_LEVEL");
  env->log_file             = require_env("LOG_FILE");

  return env->database_connection && env->redis_connection &&
         env->roblox_server_secret && env->admin_portal_secret &&
         env->roblox_api_key && env->log_level && env->log_file;
}
