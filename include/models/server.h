#pragma once

#include <stdint.h>
#include <time.h>

typedef struct {
  char server_id[128];
  time_t started_at;
} Server;
