#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/json.h"

bool json_get_String(const cJSON* obj, const char* key, char* out, size_t out_len) {
  cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsString(item) || !item->valuestring) {
    return false;
  }

  strncpy(out, item->valuestring, out_len - 1);
  out[out_len - 1] = '\0';
  return true;
}

bool json_get_int64(const cJSON* obj, const char* key, int64_t* out) {
  cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsNumber(item)) {
    return false;
  }

  *out = (int64_t)item->valuedouble;
  return true;
}

cJSON* json_error(const char* message) {
  cJSON* obj = cJSON_CreateObject();
  cJSON_AddStringToObject(obj, "error", message);
  return obj;
}

char* json_to_string(cJSON* obj) {
  return cJSON_PrintUnformatted(obj);
}
