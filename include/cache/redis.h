#pragma once

#include <stdbool.h>
#include <hiredis/hiredis.h>

#define REDIS_POOL_SIZE 25

redisContext* redis_acquire(void);
void redis_release(redisContext* ctx);
bool redis_pool_init(const char* conn_str);
void redis_pool_shutdown(void);
