#pragma once

#include <stdbool.h>
#include <libpq-fe.h>

#define PG_POOL_SIZE 25

typedef struct {
  PGconn* connections[PG_POOL_SIZE];
  bool available[PG_POOL_SIZE];
} PGPool;

bool pg_pool_init(const char* conn_str);
PGconn* pg_acquire(void);
void pg_release(PGconn* conn);
void pg_pool_shutdown(void);
