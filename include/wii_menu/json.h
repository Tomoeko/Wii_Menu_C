#ifndef WII_MENU_JSON_H
#define WII_MENU_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum WmJsonType {
    WM_JSON_OBJECT,
    WM_JSON_ARRAY,
    WM_JSON_STRING,
    WM_JSON_NUMBER,
    WM_JSON_BOOLEAN,
    WM_JSON_NULL
} WmJsonType;

typedef struct WmJsonToken {
    WmJsonType type;
    size_t start;
    size_t end;
    size_t next;
    size_t children;
} WmJsonToken;

typedef struct WmJson {
    char *source;
    WmJsonToken *tokens;
    size_t length;
    size_t count;
    size_t capacity;
} WmJson;

bool wm_json_load(WmJson *json, const char *path, size_t max_bytes);
/* Parse a caller-owned byte range after making a private copy. */
bool wm_json_parse(WmJson *json, const char *source, size_t length);
void wm_json_free(WmJson *json);
size_t wm_json_member(const WmJson *json, size_t object, const char *key);
size_t wm_json_index(const WmJson *json, size_t array, size_t index);
bool wm_json_equals(const WmJson *json, size_t token, const char *value);
bool wm_json_copy(const WmJson *json, size_t token, char *destination, size_t capacity);
bool wm_json_integer(const WmJson *json, size_t token, int *value);

#define WM_JSON_INVALID ((size_t)-1)

#endif
