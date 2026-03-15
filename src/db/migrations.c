#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libpq-fe.h>

#include "db/migrations.h"
#include "db/postgres.h"
#include "log/log.h"

#define MIGRATIONS_DIR "./migrations"
#define MAX_MIGRATIONS 1024
#define MAX_SQL_LEN 65536

static int compare_migrations(const void* a, const void* b) {
  return strcmp(*(const char**)a, *(const char**)b);
}

static bool ensure_migrations_table(PGconn* conn) {
  PGresult* result = PQexec(conn, 
    "CREATE TABLE IF NOT EXISTS _migrations("
    "   id            SERIAL PRIMARY KEY,"
    "   filename      TEXT NOT NULL UNIQUE,"
    "   applied_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()"
    ");"
  );

  if (PQresultStatus(result) != PGRES_COMMAND_OK) {
    log_error("failed to create migrations table: %s", PQerrorMessage(conn));
    PQclear(result);
    return false;
  }

  PQclear(result);
  return true;
}

static bool is_applied(PGconn* conn, const char* filename) {
  const char* params[1] = { filename };
  PGresult* result = PQexecParams(conn,
      "SELECT 1 FROM _migrations WHERE filename = $1",
      1, NULL, params, NULL, NULL, 0);

  bool applied = PQntuples(result) > 0;
  PQclear(result);
  return applied;
}

static bool apply_migration(PGconn* conn, const char* filename) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", MIGRATIONS_DIR, filename);

  FILE* file = fopen(path, "r");
  if (!file) {
    log_error("failed to open migration: %s", path);
    return false;
  }

  char sql[MAX_SQL_LEN];
  size_t n = fread(sql, 1, sizeof(sql) - 1, file);
  sql[n] = '\0';
  fclose(file);

  PGresult* result = PQexec(conn, sql);
  if (PQresultStatus(result) != PGRES_COMMAND_OK) {
    log_error("migration failed (%s): %s", filename, PQerrorMessage(conn));
    PQclear(result);
    return false;
  }

  PQclear(result);

  const char* params[1] = { filename };
  result = PQexecParams(conn,
      "INSERT INTO _migrations (filename) VALUES ($1)",
      1, NULL, params, NULL, NULL, 0);

  if (PQresultStatus(result) != PGRES_COMMAND_OK) {
    log_error("failed to record migration (%s): %s", filename, PQerrorMessage(conn));
    PQclear(result);
    return false;
  }

  PQclear(result);
  return true;
}

bool migrations_run(void) {
  log_info("running database migrations...");
  PGconn* conn = pg_acquire();

  if (!ensure_migrations_table(conn)) {
    log_error("failed to ensure migrations table");
    pg_release(conn);
    return false;
  }

  DIR* dir = opendir(MIGRATIONS_DIR);
  if (!dir) {
    log_error("failed to open migrations dir: %s", MIGRATIONS_DIR);
    pg_release(conn);
    return false;
  }

  char* files[MAX_MIGRATIONS];
  int count = 0;
  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    size_t len = strlen(entry->d_name);
    if (len < 4 || strcmp(entry->d_name + len - 4, ".sql") != 0) { continue; }

    files[count++] = strdup(entry->d_name);
    if (count >= MAX_MIGRATIONS) { break; }
  }

  closedir(dir);

  qsort(files, count, sizeof(char*), compare_migrations);

  int applied = 0;
  bool ok = true;
  for (int i = 0; i < count; i++) {
    if (ok && !is_applied(conn, files[i])) {
      log_info("applying migration: %s", files[i]);
      if (!apply_migration(conn, files[i])) {
        ok = false;
      } else {
        applied++;
      }
    }
    free(files[i]);
  }

  if (!ok) {
    pg_release(conn);
    return false;
  }

  if (applied == 0) {
    log_info("migrations: nothing to apply");
  } else {
    log_info("migrations: applied %d migration(s)", applied);
  }

  pg_release(conn);
  return true;
}
