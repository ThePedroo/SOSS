#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <pthread.h>

#include "expat.h"

#include "floating_feature.h"

/* INFO: Allow to override the default XML path for testing reasons */
#ifndef FLOATING_FEATURE_XML_PATH
  #define FLOATING_FEATURE_XML_PATH "/vendor/etc/floating_feature.xml"
#endif

struct floatingfeature_entry {
  char *key;
  char *value;
};

struct floatingfeature_map {
  struct floatingfeature_entry *entries;
  size_t length;
  size_t capacity;
};

struct floatingfeature_string {
  char *data;
  size_t len;
  size_t cap;
};

struct floatingfeature_state {
  struct floatingfeature_map map;
  int loaded;
  struct floatingfeature_string *texts;
  size_t text_count;
  size_t text_capacity;
};

static char *floatingfeature_strdup(const char *text) {
  size_t len = strlen(text);
  char *copy = malloc(len + 1);
  if (copy == NULL) return NULL;

  memcpy(copy, text, len + 1);

  return copy;
}

static const char *floatingfeature_map_find(const struct floatingfeature_map *map, const char *key) {
  for (size_t i = 0; i < map->length; i++) {
    if (strcmp(map->entries[i].key, key) == 0) return map->entries[i].value;
  }

  return NULL;
}

static void floatingfeature_map_set(struct floatingfeature_map *map, const char *key, const char *value) {
  for (size_t i = 0; i < map->length; i++) {
    if (strcmp(map->entries[i].key, key) != 0) continue;

    free(map->entries[i].value);
    map->entries[i].value = floatingfeature_strdup(value);
    if (map->entries[i].value == NULL) return;

    return;
  }

  if (map->length == map->capacity) {
    size_t new_capacity = (map->capacity == 0) ? 8 : map->capacity * 2;
    struct floatingfeature_entry *entries = realloc(map->entries, new_capacity * sizeof(*entries));
    if (entries == NULL) return;

    map->entries = entries;
    map->capacity = new_capacity;
  }

  struct floatingfeature_entry *entry = &map->entries[map->length];
  entry->key = floatingfeature_strdup(key);
  if (entry->key == NULL) return;

  entry->value = floatingfeature_strdup(value);
  if (entry->value == NULL) {
    free(entry->key);

    return;
  }

  map->length += 1;
}

static void floatingfeature_string_append(struct floatingfeature_string *string, const char *text, size_t len) {
  size_t old_len = string->len;
  size_t cap = string->cap;
  if (cap < old_len + len + 1) {
    if (cap == 0) cap = 16;

    while (cap < old_len + len + 1) cap *= 2;

    char *data = realloc(string->data, cap);
    if (data == NULL) abort();

    string->data = data;
    string->cap = cap;
  }

  memcpy(string->data + old_len, text, len);
  string->data[old_len + len] = 0;
  string->len = old_len + len;
}

static void floatingfeature_start_element(void *user_data, const XML_Char *name, const XML_Char **atts) {
  (void) name; (void) atts;

  struct floatingfeature_state *state = user_data;

  if (state->text_count == state->text_capacity) {
    size_t new_capacity = (state->text_capacity == 0) ? 8 : state->text_capacity * 2;
    struct floatingfeature_string *texts = realloc(state->texts, new_capacity * sizeof(*texts));
    if (texts == NULL) abort();

    state->texts = texts;
    state->text_capacity = new_capacity;
  }

  struct floatingfeature_string *text = &state->texts[state->text_count];
  text->data = NULL;
  text->len = 0;
  text->cap = 0;
  state->text_count += 1;
}

static void floatingfeature_char_data(void *user_data, const XML_Char *s, int len) {
  struct floatingfeature_state *state = user_data;

  if (state->text_count == 0) return;

  floatingfeature_string_append(&state->texts[state->text_count - 1], s, (size_t)len);
}

static void floatingfeature_end_element(void *user_data, const XML_Char *name) {
  struct floatingfeature_state *state = user_data;

  if (state->text_count == 0) return;

  struct floatingfeature_string *text = &state->texts[state->text_count - 1];
  char *value = text->data;
  size_t len = text->len;
  while (len > 0 && isspace((unsigned char)value[0])) {
    value += 1;
    len -= 1;
  }

  while (len > 0 && isspace((unsigned char)value[len - 1])) len -= 1;

  if (len > 0) {
    value[len] = 0;

    const char *existing = floatingfeature_map_find(&state->map, name);

    if (existing != NULL) {
      size_t old_len = strlen(existing);
      char *combined = malloc(old_len + len + 1);

      if (combined != NULL) {
        memcpy(combined, existing, old_len);
        memcpy(combined + old_len, value, len);
        combined[old_len + len] = 0;
        floatingfeature_map_set(&state->map, name, combined);
        free(combined);
      }
    } else {
      floatingfeature_map_set(&state->map, name, value);
    }
  }

  free(text->data);
  state->text_count -= 1;
}

static int floatingfeature_load_feature(struct floatingfeature_state *state) {
  FILE *file = fopen(FLOATING_FEATURE_XML_PATH, "r");
  if (file == NULL) return -1;

  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, state);
  XML_SetElementHandler(parser, floatingfeature_start_element, floatingfeature_end_element);
  XML_SetCharacterDataHandler(parser, floatingfeature_char_data);

  int result = -1;
  while (1) {
    char buffer[1024];
    size_t len = fread(buffer, 1, sizeof(buffer), file);
    if (len != sizeof(buffer) && ferror(file) != 0) break;
    if (XML_Parse(parser, buffer, (int)len, len < sizeof(buffer)) == 0) break;

    if (len <= sizeof(buffer) - 1) {
      result = 0;

      break;
    }
  }

  fclose(file);
  XML_ParserFree(parser);

  for (size_t i = 0; i < state->text_count; i++) free(state->texts[i].data);

  free(state->texts);

  if (result == 0) state->loaded = 1;

  return result;
}

static struct floatingfeature_state floatingfeature_instance;
static pthread_once_t floatingfeature_instance_guard = PTHREAD_ONCE_INIT;

static void floatingfeature_initialize(void) {
  floatingfeature_load_feature(&floatingfeature_instance);
}

static struct floatingfeature_state *floatingfeature_get_instance(void) {
  pthread_once(&floatingfeature_instance_guard, floatingfeature_initialize);

  return &floatingfeature_instance;
}

static int floatingfeature_get_enable_status(struct floatingfeature_state *state, const char *feature_name, int default_value) {
  if (!state->loaded) return default_value;

  const char *value = floatingfeature_map_find(&state->map, feature_name);

  if (value != NULL &&
      (strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0)) return 1;

  return default_value;
}

static char *floatingfeature_get_string(struct floatingfeature_state *state, const char *feature_name,
                                        const char *default_value) {
  if (!state->loaded) return (char *)default_value;

  const char *value = floatingfeature_map_find(&state->map, feature_name);

  if (value == NULL) return (char *)default_value;

  return (char *)value;
}

static int floatingfeature_get_integer(struct floatingfeature_state *state, const char *feature_name, int default_value) {
  if (!state->loaded) return default_value;

  const char *value = floatingfeature_map_find(&state->map, feature_name);

  if (value == NULL) return default_value;

  return atoi(value);
}

/* INFO: Functions below are the ones exposed (Public API) */

int FloatingFeature_getEnableStatus(const char *featureName) {
  return floatingfeature_get_enable_status(floatingfeature_get_instance(), featureName, 0) & 1;
}

int FloatingFeature_getEnableStatusWithDefault(const char *featureName, int defaultValue) {
  return floatingfeature_get_enable_status(floatingfeature_get_instance(), featureName, defaultValue != 0) & 1;
}

int FloatingFeature_getInteger(const char *featureName) {
  return floatingfeature_get_integer(floatingfeature_get_instance(), featureName, -1);
}

int FloatingFeature_getIntegerWithDefault(const char *featureName, int defaultValue) {
  return floatingfeature_get_integer(floatingfeature_get_instance(), featureName, defaultValue);
}

char *FloatingFeature_getString(const char *featureName) {
  return floatingfeature_get_string(floatingfeature_get_instance(), featureName, "TRUE");
}

char *FloatingFeature_getStringWithDefault(const char *featureName, const char *defaultValue) {
  return floatingfeature_get_string(floatingfeature_get_instance(), featureName, defaultValue);
}
