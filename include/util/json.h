#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cjson/cJSON.h>

bool json_get_string(const cJSON* object, const char* key, char* out, size_t out_len);
bool json_get_int64(const cJSON* object, const char* key, int64_t* out);

cJSON* json_error(const char* message);
char* json_to_string(cJSON* obj);
