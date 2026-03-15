#include <pthread.h>
#include <libpq-fe.h>

#include "db/postgres.h"
#include "log/log.h"

static PGPool pool;
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pool_cond = PTHREAD_COND_INITIALIZER;

bool pg_pool_init(const char *conn_str) {
  log_info("initializing postgres connection pool...");
  for (int i = 0; i < PG_POOL_SIZE; i++) {
    pool.connections[i] = PQconnectdb(conn_str);
    if (PQstatus(pool.connections[i]) != CONNECTION_OK) {
      log_error("pg_pool_init: connection %d failed: %s", i, PQerrorMessage(pool.connections[i]));
      for (int j = 0; j <= i; j++) {
        PQfinish(pool.connections[j]);
        pool.connections[j] = NULL;
      }
      return false;
    }

    pool.available[i] = true;
  }

  log_info("postgres pool initialized with %d connections", PG_POOL_SIZE);
  return true;
}

PGconn* pg_acquire(void) {
  pthread_mutex_lock(&pool_mutex);

  while (true) {
    for (int i = 0; i < PG_POOL_SIZE; i++) {
      if (pool.available[i]) {
        pool.available[i] = false;
        pthread_mutex_unlock(&pool_mutex);
        return pool.connections[i];
      }
    }

    pthread_cond_wait(&pool_cond, &pool_mutex);
  }
}

void pg_release(PGconn* conn) {
  pthread_mutex_lock(&pool_mutex);

  for (int i = 0; i < PG_POOL_SIZE; i++) {
    if (pool.connections[i] == conn) {
      pool.available[i] = true;
      pthread_cond_signal(&pool_cond);
      break;
    }
  }

  pthread_mutex_unlock(&pool_mutex);
}

void pg_pool_shutdown(void) {
  log_info("shutting down postgres connection pool...");
  for (int i = 0; i < PG_POOL_SIZE; i++) {
    PQfinish(pool.connections[i]);
  }

  log_info("postgres pool shut down");
}
