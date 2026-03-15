#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <hiredis/hiredis.h>

#include "cache/redis.h"
#include "log/log.h"

typedef struct {
  redisContext* connections[REDIS_POOL_SIZE];
  bool available[REDIS_POOL_SIZE];
} RedisPool;

static RedisPool pool;
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pool_cond = PTHREAD_COND_INITIALIZER;

bool redis_pool_init(const char* conn_str) {
  log_info("initializing redis connection pool...");
  char host[256];
  int port = 6379;

  char* colon = strchr(conn_str, ':');
  if (colon) {
    size_t host_len = colon - conn_str;
    strncpy(host, conn_str, host_len);
    host[host_len] = '\0';
    port = atoi(colon + 1);
  } else {
    strncpy(host, conn_str, sizeof(host) - 1);
  }

  for (int i = 0; i < REDIS_POOL_SIZE; i++) {
    pool.connections[i] = redisConnect(host, port);
    if (!pool.connections[i] || pool.connections[i]->err) {
      log_error("redis_pool_init: connection %d failed: %s", i, pool.connections[i] ? pool.connections[i]->errstr : "null context");
      for (int j = 0; j < i; j++) {
        redisFree(pool.connections[j]);
        pool.connections[j] = NULL;
      }
      if (pool.connections[i]) {
        redisFree(pool.connections[i]);
        pool.connections[i] = NULL;
      }
      return false;
    }
    pool.available[i] = true;
  }

  log_info("redis pool initialized with %d connections", REDIS_POOL_SIZE);
  return true;
}

redisContext* redis_acquire(void) {
  pthread_mutex_lock(&pool_mutex);

  while (true) {
    for (int i = 0; i < REDIS_POOL_SIZE; i++) {
      if (pool.available[i]) {
        pool.available[i] = false;
        pthread_mutex_unlock(&pool_mutex);
        return pool.connections[i];
      }
    }

    pthread_cond_wait(&pool_cond, &pool_mutex);
  }
}

void redis_release(redisContext* ctx) {
  pthread_mutex_lock(&pool_mutex);

  for (int i = 0; i < REDIS_POOL_SIZE; i++) {
    if (pool.connections[i] == ctx) {
      pool.available[i] = true;
      pthread_cond_signal(&pool_cond);
      break;
    }
  }

  pthread_mutex_unlock(&pool_mutex);
}

void redis_pool_shutdown(void) {
  log_info("shutting down redis connection pool...");
  for (int i = 0; i < REDIS_POOL_SIZE; i++) {
    redisFree(pool.connections[i]);
  }

  log_info("redis pool shut down");
}
