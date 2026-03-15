#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
  int64_t id;
  
  bool is_online;

  time_t created_at;
  time_t updated_at;
} Player;
