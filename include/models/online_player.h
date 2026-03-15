#pragma once

#include <stdint.h>
#include <time.h>

typedef struct {
  int64_t player_id;
  char server_id[128];
  time_t joined_at;
} OnlinePlayer;
