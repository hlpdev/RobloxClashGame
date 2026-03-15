#pragma once

#define MAX_ROUTES 128
#define MAX_HEADER_LEN 512
#define MAX_BODY_LEN 65536

typedef enum {
  REALM_ROBLOX,
  REALM_ADMIN
} Realm;

typedef struct {
  char secret[MAX_HEADER_LEN];
  char server_id[MAX_HEADER_LEN];
  char player_id[MAX_HEADER_LEN];
  char body[MAX_BODY_LEN];
  int body_len;
  Realm realm;
} Request;

typedef struct {
  char body[MAX_BODY_LEN];
  int body_len;
  int status;
} Response;

typedef void (*RouteHandler)(const Request* request, Response* response);

typedef struct {
  char method[8];
  char path[256];
  Realm realm;
  RouteHandler handler;
} Route;

void router_init(const char* roblox_secret, const char* admin_secret);
void router_register(const char* method, const char* path, Realm realm, RouteHandler handler);
void router_dispatch(int fd, const char* raw, int raw_len);
