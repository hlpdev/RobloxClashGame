#pragma once

#include <stdbool.h>

#include "models/server.h"

bool server_repository_register(const Server* server);
bool server_repository_unregister(const char* server_id);

bool server_repository_heartbeat(const char* server_id);

bool server_repository_exists(const char* server_id);

Server* server_repository_find(const char* server_id);
void server_repository_free(Server* server);

bool server_repository_start_expiry_listener(void);
void server_repository_stop_expiry_listener(void);
