#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "net/router.h"
#include "log/log.h"

const char* roblox_secret;
const char* admin_secret;

static Route routes[MAX_ROUTES];
static int route_count;

void router_init(const char* rbx_secret, const char* adm_secret) {
  roblox_secret = rbx_secret;
  admin_secret = adm_secret;

  route_count = 0;
  log_info("router initialized");
}

void router_register(const char* method, const char* path, Realm realm, RouteHandler handler) {
  if (route_count >= MAX_ROUTES) {
    log_error("router_register: max routes reached");
    return;
  }

  strncpy(routes[route_count].method, method, sizeof(routes[0].method) - 1);
  strncpy(routes[route_count].path, path, sizeof(routes[0].path) - 1);
  routes[route_count].realm = realm;
  routes[route_count].handler = handler;

  route_count++;
}

static void send_response(int fd, int status, const char* body) {
  const char* status_str;
  switch (status) {
    case 200: status_str = "200 OK"; break;
    case 401: status_str = "401 Unauthorized"; break;
    case 403: status_str = "403 Forbidden"; break;
    case 404: status_str = "404 Not Found"; break;
    case 405: status_str = "405 Method Not Allowed"; break;
    default: status_str = "500 Internal Server Error"; break;
  }

  int body_len = body ? strlen(body) : 0;
  char header[512];
  int header_len = snprintf(header, sizeof(header),
      "HTTP/1.1 %s\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %d\r\n"
      "\r\n",
      status_str, body_len);

  write(fd, header, header_len);
  if (body && body_len > 0) {
    write(fd, body, body_len);
  }
}

static char* extract_header(const char* raw, const char* name, char* out, int out_len) {
  char search[128];
  snprintf(search, sizeof(search), "%s: ", name);
  const char* p = strstr(raw, search);
  if (!p) {
    return NULL;
  }

  p += strlen(search);
  const char* end = strstr(p, "\r\n");
  if (!end) {
    return NULL;
  }

  int len = end - p;
  if (len >= out_len) {
    len = out_len - 1;
  }

  strncpy(out, p, len);
  out[len] = '\0';

  return out;
}

static const char* extract_body(const char* raw) {
  const char* p = strstr(raw, "\r\n\r\n");
  if (!p) {
    return NULL;
  }

  return p + 4;
}

void router_dispatch(int fd, const char* raw, int raw_len) {
  (void)raw_len;

  char method[8] = { 0 };
  char path[256] = { 0 };
  sscanf(raw, "%7s %255s", method, path);

  Request req = { 0 };
  if (!extract_header(raw, "secret", req.secret, sizeof(req.secret))) {
    send_response(fd, 401, "{\"error\":\"missing secret\"}");
    return;
  }

  extract_header(raw, "serverId", req.server_id, sizeof(req.server_id));
  extract_header(raw, "playerId", req.player_id, sizeof(req.player_id));

  if (strcmp(req.secret, roblox_secret) == 0) {
    req.realm = REALM_ROBLOX;
  } else if (strcmp(req.secret, admin_secret) == 0) {
    req.realm = REALM_ADMIN;
  } else {
    log_info("rejected request: invalid secret");
    send_response(fd, 401, "{\"error\":\"invalid secret\"}");
    return;
  }

  const char* body = extract_body(raw);
  if (body) {
    req.body_len = strlen(body);
    strncpy(req.body, body, sizeof(req.body) - 1);
  }

  for (int i = 0; i < route_count; i++) {
    if (strcmp(routes[i].method, method) != 0) continue;
    if (strcmp(routes[i].path, path) != 0) continue;

    if (routes[i].realm != req.realm) {
      log_info("rejected request: realm mismatch on %s %s", method, path);
      send_response(fd, 403, "{\"error\":\"forbidden\"}");
      return;
    }

    Response res = { .status = 200 };
    routes[i].handler(&req, &res);
    send_response(fd, res.status, res.body_len > 0 ? res.body : NULL);
    return;
  }

  send_response(fd, 404, "{\"error\":\"not found\"}");
}
