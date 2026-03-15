#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "models/online_player.h"

bool online_player_repository_add(const OnlinePlayer* player);
bool online_player_repository_remove(int64_t player_id);
bool online_player_repository_remove_by_server(const char* server_id);

bool online_player_repository_exists(int64_t player_id);

OnlinePlayer* online_player_repository_find(int64_t player_id);
void online_player_repository_free(OnlinePlayer* player);

