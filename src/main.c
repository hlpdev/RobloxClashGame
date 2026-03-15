#include "repository/server_repository.h"
#include "db/postgres.h"
#include "db/migrations.h"
#include "cache/redis.h"
#include "net/server.h"
#include "net/router.h"
#include "env/env.h"
#include "log/log.h"

int main(void) {
  // Load environment vars
  Environment env;
  if (!env_load(&env)) {
    return 1;
  }

  // Initialize logging
  LogLevel level = log_level_from_string(env.log_level);
  if (!log_init(level, env.log_file)) {
    return 1;
  }
  log_info("server starting...");

  // Initialize postgresql connection pool
  if (!pg_pool_init(env.database_connection)) {
    log_error("failed to initialize postgres pool");
    log_shutdown();
    return 1;
  }

  // Apply pending migrations to the database
  if (!migrations_run()) {
    log_error("failed to run database migrations");
    pg_pool_shutdown();
    log_shutdown();
    return 1;
  }

  // Initialize redis connection pool and other related services
  if (!redis_pool_init(env.redis_connection)) {
    log_error("failed to initialize redis pool");
    pg_pool_shutdown();
    log_shutdown();
    return 1;
  }
  if (!server_repository_start_expiry_listener()) {
    log_error("failed to start expiry listener");
    redis_pool_shutdown();
    pg_pool_shutdown();
    log_shutdown();
    return 1;
  }

  // Initialize the HTTP server
  if (!server_init()) {
    log_error("failed to initialize server");
    server_repository_stop_expiry_listener();
    redis_pool_shutdown();
    pg_pool_shutdown();
    log_shutdown();
    return 1;
  }

  // Initialize request routing
  router_init(env.roblox_server_secret, env.admin_portal_secret);

  // Run the HTTP server
  server_run();

  // Shutdown the HTTP server
  server_shutdown();

  // Shutdown and cleanup redis connection pool and related services
  server_repository_stop_expiry_listener();
  redis_pool_shutdown();

  // Shutdown and cleanup postgresql connection pool
  pg_pool_shutdown();

  // Shutdown logging
  log_info("server shutting down...");
  log_shutdown();

  return 0;
}
