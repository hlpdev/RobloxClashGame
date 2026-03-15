#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "repository/server_repository.h"
#include "repository/online_player_repository.h"
#include "cache/redis.h"
#include "log/log.h"

#define SERVER_TTL 90
#define SERVER_KEY_PREFIX "server:"

static pthread_t expiry_thread;
static volatile bool expiry_running = false;

static void server_key(const char* server_id, char* out, size_t out_len) {
  snprintf(out, out_len, "%s:%s", SERVER_KEY_PREFIX, server_id);
}

static void server_players_key(const char* server_id, char* out, size_t out_len) {
  snprintf(out, out_len, "%s:%s:players", SERVER_KEY_PREFIX, server_id);
}

static bool parse_server_key(const char* expired_key, char* server_id_out, size_t out_len) {
  if (strncmp(expired_key, SERVER_KEY_PREFIX, strlen(SERVER_KEY_PREFIX)) != 0) {
    return false;
  }

  const char* id_start = expired_key + strlen(SERVER_KEY_PREFIX);

  if (strchr(id_start, ':') != NULL) {
    return false;
  }

  strncpy(server_id_out, id_start, out_len - 1);
  server_id_out[out_len - 1] = '\0';
  return true;
}

static void on_server_expired(const char* server_id) {
  log_info("server expired, cleaning up: %s", server_id);

  redisContext* redis = redis_acquire();

  char players_key[256];
  server_players_key(server_id, players_key, sizeof(players_key));

  redisReply* reply = redisCommand(redis, "DEL %s", players_key);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("on_server_expired: DEL players key failed: %s",
        reply ? reply->str : "null reply");
  }

  if (reply) {
    freeReplyObject(reply);
  }

  redis_release(redis);
  
  online_player_repository_remove_by_server(server_id);
}

static void* expiry_listener_thread(void* arg) {
  (void)arg;

  redisContext* redis = redis_acquire();

  redisReply* reply = redisCommand(redis, "SUBSCRIBE __keyevent@0__:expired");
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("expiry_listener: SUBSCRIBE failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return NULL;
  }
  freeReplyObject(reply);

  log_info("expiry listener subscribed");

  while (expiry_running) {
    reply = NULL;

    if (redisGetReply(redis, (void**)&reply) != REDIS_OK) {
      log_error("expiry_listener: redisGetReply failed");
      break;
    }

    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
      const char* expired_key = reply->element[2]->str;
      char server_id[128];

      if (parse_server_key(expired_key, server_id, sizeof(server_id))) {
        on_server_expired(server_id);
      }
    }

    if (reply) {
      freeReplyObject(reply);
    }
  }

  redis_release(redis);
  return NULL;
}

bool server_repository_start_expiry_listener(void) {
  expiry_running = true;

  if (pthread_create(&expiry_thread, NULL, expiry_listener_thread, NULL) != 0) {
    log_error("server_repository_start_expiry_listener: pthread_create failed");
    expiry_running = false;
    return false;
  }

  return true;
}

void server_repository_stop_expiry_listener(void) {
  expiry_running = false;
  pthread_join(expiry_thread, NULL);
}

bool server_repository_register(const Server* server) {
  redisContext* redis = redis_acquire();

  char key[256];
  server_key(server->server_id, key, sizeof(key));

  redisReply* reply = redisCommand(redis,
      "HSET %s server_id %s started_at %ld",
      key, server->server_id, (long)server->started_at);

  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("server_repository_register: HSET failed %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return false;
  }
  freeReplyObject(reply);

  reply = redisCommand(redis, "EXPIRE %s %d", key, SERVER_TTL);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("server_repository_register: EXPIRE failed %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return false;
  }

  freeReplyObject(reply);
  redis_release(redis);

  log_info("server registered: %s", server->server_id);
  return true;
}

bool server_repository_unregister(const char* server_id) {
  online_player_repository_remove_by_server(server_id);

  redisContext* redis = redis_acquire();

  char key[256];
  char players_key[256];
  server_key(server_id, key, sizeof(key));
  server_players_key(server_id, players_key, sizeof(players_key));

  redisReply* reply = redisCommand(redis, "DEL %s %s", key, players_key);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("server_repository_unregister: DEL failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return false;
  }

  freeReplyObject(reply);
  redis_release(redis);

  log_info("server unregistered: %s", server_id);
  return true;
}

bool server_repository_heartbeat(const char* server_id) {
  redisContext* redis = redis_acquire();

  char key[256];
  server_key(server_id, key, sizeof(key));

  redisReply* reply = redisCommand(redis, "EXPIRE %s %d", key, SERVER_TTL);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("server_repository_heartbeat: EXPIRE failed: %s",
        reply ? reply->str : "null reply");
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return false;
  }

  bool alive = reply->integer == 1;
  freeReplyObject(reply);
  redis_release(redis);

  if (!alive) {
    log_error("server_repository_heartbeat: server expired before heartbeat: %s");
  }

  return alive;
}

Server* server_repository_find(const char* server_id) {
  redisContext* redis = redis_acquire();

  char key[256];
  server_key(server_id, key, sizeof(key));

  redisReply* reply = redisCommand(redis, "HGETALL %s", key);
  if (!reply || reply->type == REDIS_REPLY_ERROR || reply->elements == 0) {
    if (reply) {
      freeReplyObject(reply);
    }
    redis_release(redis);
    return NULL;
  }

  Server* server = calloc(1, sizeof(Server));
  if (!server) {
    log_error("server_repository_find: calloc failed");
    freeReplyObject(reply);
    redis_release(redis);
    return NULL;
  }

  for (size_t i = 0; i + 1 < reply->elements; i += 2) {
    const char* field = reply->element[i]->str;
    const char* value = reply->element[i + 1]->str;

    if (strcmp(field, "server_id") == 0) {
      strncpy(server->server_id, value, sizeof(server->server_id) - 1);
      continue;
    }

    if (strcmp(field, "started_at") == 0) {
      server->started_at = (time_t)atol(value);
    }
  }

  freeReplyObject(reply);
  redis_release(redis);
  return server;
}

bool server_repository_exists(const char* server_id) {
  redisContext* redis = redis_acquire();

  char key[256];
  server_key(server_id, key, sizeof(key));

  redisReply* reply = redisCommand(redis, "EXISTS %s", key);
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    log_error("server_repository_exists: EXISTS failed: %s",
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

void server_repository_free(Server* server) {
  free(server);
}


