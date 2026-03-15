#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "repository/online_player_repository.h"
#include "cache/redis.h"
#include "db/postgres.h"
#include "log/log.h"

static void session_key(int64_t player_id, char* out, size_t out_len) {
  snprintf(out, out_len, "player:%ld:session", player_id);
}

static void server_players_key(const char* server_id, char* out, size_t out_len) {
  snprintf(out, out_len, "server:%s:players", server_id);
}

static bool set_player_online(int64_t player_id, bool online) {
  PGconn* conn = pg_acquire();

  const char* value = online ? "true" : "false";
  char id_str[32];
  snprintf(id_str, sizeof(id_str), "%ld", player_id);

  const char* params[2] = { value, id_str };
  PGresult* result = PQexecParams(conn,
      "UPDATE players SET is_online = $1, updated_at = NOW() WHERE id = $2",
      2, NULL, params, NULL, NULL, 0);

  if (PQresultStatus(result) != PGRES_COMMAND_OK) {
    log_error("set_player_online: UPDATE failed: %s", PQerrorMessage(conn));
    PQclear(result);
    pg_release(conn);
    return false;
  }

  PQclear(result);
  pg_release(conn);
  return true;
}

bool online_player_repository_add(const OnlinePlayer* player) {
  redisContext* redis = redis_acquire();

  char key[256];
  session_key(player->player_id, key, sizeof(key));

  char player_id_str[32];
  snprintf(player_id_str, sizeof(player_id_str), "%ld", player->player_id);

  redisReply* reply = redisCommand(redis,
      "HSET %s player_id %s server_id %s joined_at %ld",
      key, player_id_str, player->server_id, (long)player->joined_at);

  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("online_player_repository_add: HSET failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return false;
  }
  freeReplyObject(reply);

  char sp_key[256];
  server_players_key(player->server_id, sp_key, sizeof(sp_key));

  reply = redisCommand(redis, "SAAD %s %s", sp_key, player_id_str);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("online_player_repository_add: SADD failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return false;
  }

  freeReplyObject(reply);
  redis_release(redis);

  if (!set_player_online(player->player_id, true)) {
    log_error("online_player_repository_add: failed to set is_online for player %ld",
        player->player_id);
    return false;
  }

  log_info("player %ld joined server %s", player->player_id, player->server_id);
  return true;
}

bool online_player_repository_remove(int64_t player_id) {
  OnlinePlayer* player = online_player_repository_find(player_id);
  if (!player) {
    log_error("online_player_repository_remove: session not found for player %ld", 
        player_id);
    return false;
  }

  redisContext* redis = redis_acquire();

  char key[256];
  session_key(player_id, key, sizeof(key));

  char sp_key[256];
  server_players_key(player->server_id, sp_key, sizeof(sp_key));

  char player_id_str[32];
  snprintf(player_id_str, sizeof(player_id_str), "%ld", player_id);

  redisReply* reply = redisCommand(redis, "DEL %s", key);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("online_player_repository_remove: DEL session failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    online_player_repository_free(player);
    return false;
  }
  freeReplyObject(reply);

  reply = redisCommand(redis, "SREM %s %s", sp_key, player_id_str);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("online_player_repository_remove: SREM failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    online_player_repository_free(player);
    return false;
  }

  freeReplyObject(reply);
  redis_release(redis);
  online_player_repository_free(player);

  if (!set_player_online(player_id, false)) {
    log_error("online_player_repository_remove: failed to clear is_online for player %ld",
        player_id);
    return false;
  }

  log_info("player %ld left their server", player_id);
  return true;
}

bool online_player_repository_remove_by_server(const char* server_id) {
  redisContext* redis = redis_acquire();

  char sp_key[256];
  server_players_key(server_id, sp_key, sizeof(sp_key));

  redisReply* members = redisCommand(redis, "SMEMBERS %s", sp_key);
  if (!members || members->type == REDIS_REPLY_ERROR) {
    log_error("online_player_repository_remove_by_server: SMEMBERS failed: %s",
        members ? members->str : "null reply");
    if (members) {
      freeReplyObject(members);
    }
    redis_release(redis);
    return false;
  }

  bool ok = true;
  for (size_t i = 0; i < members->elements; i++) {
    const char* id_str = members->element[i]->str;
    int64_t player_id = atol(id_str);

    char key[256];
    session_key(player_id, key, sizeof(key));

    redisReply* reply = redisCommand(redis, "DEL %s", key);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
      log_error("online_player_repository_remove_by_server: DEL failed for player %s",
          id_str);
      ok = false;
    }

    if (reply) {
      freeReplyObject(reply);
    }

    if (!set_player_online(player_id, false)) {
      log_error("online_player_repository_remove_by_server: failed to clear is_online for player %s",
          id_str);
      ok = false;
    }
  }

  freeReplyObject(members);
  redis_release(redis);

  log_info("cleaned up %zu player sessions for expired server %s",
      members->elements, server_id);
  return ok;
}

OnlinePlayer* online_player_repository_find(int64_t player_id) {
  redisContext* redis = redis_acquire();

  char key[256];
  session_key(player_id, key, sizeof(key));

  redisReply* reply = redisCommand(redis, "HGETALL %s", key);
  if (!reply || reply->type == REDIS_REPLY_ERROR || reply->elements == 0) {
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return NULL;
  }

  OnlinePlayer* player = calloc(1, sizeof(OnlinePlayer));
  if (!player) {
    log_error("online_player_repository_find: calloc failed");
    freeReplyObject(reply);
    redis_release(redis);
    return NULL;
  }

  for (size_t i = 0; i + 1 < reply->elements; i += 2) {
    const char* field = reply->element[i]->str;
    const char* value = reply->element[i + 1]->str;

    if (strcmp(field, "player_id") == 0) {
      player->player_id = atol(value);
      continue;
    }

    if (strcmp(field, "server_id") == 0) {
      strncpy(player->server_id, value, sizeof(player->server_id) - 1);
      continue;
    }

    if (strcmp(field, "joined_at") == 0) {
      player->joined_at = (time_t)atol(value);
    }
  }

  freeReplyObject(reply);
  redis_release(redis);
  return player;
}

bool online_player_repository_exists(int64_t player_id) {
  redisContext* redis = redis_acquire();

  char key[256];
  session_key(player_id, key, sizeof(key));

  redisReply* reply = redisCommand(redis, "EXISTS %s", key);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("online_player_repository_exists: EXISTS failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return false;
  }

  bool exists = reply->integer == 1;
  freeReplyObject(reply);
  redis_release(redis);
  return exists;
}

void online_player_repository_free(OnlinePlayer* player) {
  free(player);
}
