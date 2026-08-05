#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/system_properties.h>
#include <unistd.h>

#include "expat.h"
#include <zlib.h>

#include "decode_tables.h"
#include "secnativefeature.h"

#define SECNATIVE_FEATURE_BUFFER_SIZE 92
#define SECNATIVE_FEATURE_PARSE_BUFFER_SIZE 1024
#define SECNATIVE_FEATURE_UNZIP_BUFFER_SIZE 204800
#define SECNATIVE_FEATURE_PROPERTY_DEBUG_LEVEL "ro.vendor.boot.debug_level"
#define SECNATIVE_FEATURE_PROPERTY_SHIP "ro.vendor.product_ship"
#define SECNATIVE_FEATURE_PROPERTY_COUNTRY "ro.csc.countryiso_code"
#define SECNATIVE_FEATURE_PROPERTY_UPDATE "mdc.omc.update_version"
#define SECNATIVE_FEATURE_PROPERTY_PATH "mdc.vendor.path"
#define SECNATIVE_FEATURE_PROPERTY_PATH_DEFAULT "UKN"
#define SECNATIVE_FEATURE_STRING_FEATURE_SET "FeatureSet"
#define SECNATIVE_FEATURE_STRING_CSCFEATURE_FILE "/cscfeature.xml"
#define SECNATIVE_FEATURE_STRING_DEBUG_LEVEL "0x4948"

/* INFO: A growable character buffer standing in for std::string. */
struct secnativefeature_string {
  char *data;
  size_t len;
  size_t cap;
};

/* INFO: Map node; the value pointer stays stable across later inserts
         (getters return pointers into the map, like the original). */
struct secnativefeature_entry {
  struct secnativefeature_entry *next;
  char *key;
  char *value;
};

struct secnativefeature {
  struct secnativefeature_entry *map;
  int is_debug_enabled; /* Written at construction, never read (dead, like the original). */
};

/* INFO: Expat handler user data; tag and attr are the running element
         state of the parse (attr keeps the last attribute value until
         the closing tag, which is how country keys propagate). */
struct secnativefeature_parse_state {
  struct secnativefeature_string tag;
  struct secnativefeature_string attr;
  struct secnativefeature *instance;
  int depth;
};

static char secnativefeature_country_iso[SECNATIVE_FEATURE_BUFFER_SIZE] = { 0 };
static char secnativefeature_omc_update_version[SECNATIVE_FEATURE_BUFFER_SIZE] = { 0 };
static struct secnativefeature_string secnativefeature_feature_set_attr = { 0 };
struct secnativefeature *_ZN16SecNativeFeature9_instanceE;

static void secnativefeature_string_reserve(struct secnativefeature_string *string, size_t len) {
  size_t cap = string->cap;
  if (cap >= len + 1) return;

  if (cap == 0) cap = 16;

  while (cap < len + 1) cap *= 2;

  char *data = realloc(string->data, cap);
  if (data == NULL) abort();

  string->data = data;
  string->cap = cap;
}

static void secnativefeature_string_clear(struct secnativefeature_string *string) {
  free(string->data);

  string->data = NULL;
  string->len = 0;
  string->cap = 0;
}

static void secnativefeature_string_set(struct secnativefeature_string *string, const char *text) {
  size_t len = strlen(text);

  secnativefeature_string_reserve(string, len);
  memcpy(string->data, text, len + 1);
  string->len = len;
}

static void secnativefeature_string_append(struct secnativefeature_string *string, const char *text, size_t len) {
  size_t old_len = string->len;

  secnativefeature_string_reserve(string, old_len + len);
  memcpy(string->data + old_len, text, len);
  string->data[old_len + len] = 0;
  string->len = old_len + len;
}

static int secnativefeature_string_empty(const struct secnativefeature_string *string) {
  return string->len == 0;
}

static struct secnativefeature_entry *secnativefeature_map_find(struct secnativefeature_entry *head, const char *key) {
  for (struct secnativefeature_entry *entry = head; entry != NULL; entry = entry->next) {
    if (strcmp(entry->key, key) == 0) return entry;
  }

  return NULL;
}

static void secnativefeature_map_insert(struct secnativefeature_entry **head, const char *key,
                                        const char *value, size_t value_len) {
  struct secnativefeature_entry *entry = malloc(sizeof(*entry));
  if (!entry) abort();

  /* INFO: Perform copy with malloc + memcpy as value is NOT NULL-terminated */
  char *value_copy = malloc(value_len + 1);
  if (!value_copy) abort();

  memcpy(value_copy, value, value_len);
  value_copy[value_len] = 0;

  entry->key = strdup(key);
  if (!entry->key) abort();

  entry->value = value_copy;
  entry->next = *head;
  *head = entry;
}

static void secnativefeature_map_concat(struct secnativefeature_entry *entry, const char *value, size_t value_len) {
  size_t old_len = strlen(entry->value);

  entry->value = realloc(entry->value, old_len + value_len + 1);
  memcpy(entry->value + old_len, value, value_len);
  entry->value[old_len + value_len] = 0;
}

static void secnativefeature_map_clear(struct secnativefeature_entry **head) {
  struct secnativefeature_entry *entry = *head;
  while (entry != NULL) {
    struct secnativefeature_entry *next = entry->next;
    free(entry->key);
    free(entry->value);
    free(entry);

    entry = next;
  }

  *head = NULL;
}

/* INFO: libcutils-compatible property read */
static int secnativefeature_property_get(const char *name, char *value, const char *default_value) {
  int len = __system_property_get(name, value);
  if (len >= 0) return len;

  size_t default_len = strlen(default_value);
  if (default_len >= SECNATIVE_FEATURE_BUFFER_SIZE) default_len = SECNATIVE_FEATURE_BUFFER_SIZE - 1;

  memcpy(value, default_value, default_len);
  value[default_len] = 0;

  return (int)default_len;
}

/* INFO: Per-byte position-dependent transform; the inverse (encode) is
           ROR8(b ^ x, r), verified to round-trip for all lengths. */
static void secnativefeature_decode(uint8_t *buffer, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t shift = snf_decode_shift[i & 0xFF] & 7;

    buffer[i] = (uint8_t)((buffer[i] << shift) | (buffer[i] >> (8 - shift))) ^ snf_decode_xor[i & 0xFF];
  }
}

/* INFO: Read the whole file, decode it and inflate the gzip stream
           into the caller's buffer.  Mirrors getUnzipData, including the
           NUL terminator written at total_out and the >= 1 byte gate
           around decode. */
static int secnativefeature_get_unzip_data(FILE *file, char *out) {
  if (fseek(file, 0, SEEK_END) != 0) {
    rewind(file);

    return 0;
  }

  long size = ftell(file);

  rewind(file);

  void *buffer = malloc((size_t)size);
  if (buffer == NULL) return 0;

  if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
    free(buffer);

    return 0;
  }

  if (size >= 1) secnativefeature_decode(buffer, (size_t)size);

  z_stream stream = {
    .next_in = buffer,
    .avail_in = (uInt)size,
    .next_out = (Bytef *)out,
    .avail_out = SECNATIVE_FEATURE_UNZIP_BUFFER_SIZE
  };
  int ok = 0;
  if (inflateInit2_(&stream, 31, "1.2.13", 112) == Z_OK && inflate(&stream, Z_FINISH) == Z_STREAM_END && stream.total_out > 0) {
    out[stream.total_out] = 0;
    ok = 1;
  }

  inflateEnd(&stream);
  free(buffer);

  return ok;
}

static void secnativefeature_start_element(void *user_data, const XML_Char *name, const XML_Char **atts) {
  struct secnativefeature_parse_state *state = user_data;
  secnativefeature_string_set(&state->tag, name);

  if (atts != NULL) {
    const XML_Char **pair = atts;

    while (pair[0] != NULL) {
      secnativefeature_string_set(&state->attr, pair[1]);
      pair += 2;
    }
  }

  if (secnativefeature_string_empty(&state->attr) && !secnativefeature_string_empty(&secnativefeature_feature_set_attr)) {
    secnativefeature_string_set(&state->attr, secnativefeature_feature_set_attr.data);
  }

  if (strcmp(state->tag.data, SECNATIVE_FEATURE_STRING_FEATURE_SET) == 0) {
    secnativefeature_string_set(&secnativefeature_feature_set_attr, state->attr.len != 0 ? state->attr.data : "");
  }

  state->depth += 2;
}

static void secnativefeature_end_element(void *user_data, const XML_Char *name) {
  struct secnativefeature_parse_state *state = user_data;

  if (strcmp(name, SECNATIVE_FEATURE_STRING_FEATURE_SET) == 0) {
    secnativefeature_string_set(&secnativefeature_feature_set_attr, "");
  }

  secnativefeature_string_set(&state->tag, "");
  secnativefeature_string_set(&state->attr, "");
  state->depth -= 2;
}

static void secnativefeature_char_data(void *user_data, const XML_Char *s, int len) {
  struct secnativefeature_parse_state *state = user_data;

  if (secnativefeature_string_empty(&state->tag)) return;

  struct secnativefeature_string key = { 0 };
  secnativefeature_string_set(&key, state->tag.data);

  if (!secnativefeature_string_empty(&state->attr)) {
    secnativefeature_string_append(&key, ",", 1);
    secnativefeature_string_append(&key, state->attr.data, state->attr.len);
  }

  struct secnativefeature_entry *entry = secnativefeature_map_find(state->instance->map, key.data);

  if (entry != NULL) {
    secnativefeature_map_concat(entry, s, (size_t)len);
  } else {
    secnativefeature_map_insert(&state->instance->map, key.data, s, (size_t)len);
  }

  secnativefeature_string_clear(&key);
}

/* INFO: Expat-driven parse of one cscfeature file. */
int secnativefeature_load_feature(const char *path, struct secnativefeature *instance) {
  struct secnativefeature_parse_state state = { .instance = instance };

  if (path == NULL) return -1;

  FILE *file = fopen(path, "r");
  if (file == NULL) return -1;

  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, secnativefeature_start_element, secnativefeature_end_element);
  XML_SetCharacterDataHandler(parser, secnativefeature_char_data);

  char haystack[16] = { 0 };
  fgets(haystack, 16, file);
  rewind(file);

  int result = -1;
  if (strstr(haystack, "<?xml") != NULL) {
    for (;;) {
      char buffer[SECNATIVE_FEATURE_PARSE_BUFFER_SIZE] = { 0 };
      size_t n = fread(buffer, 1, sizeof(buffer), file);

      if (n != sizeof(buffer) && ferror(file) != 0) break;
      if (XML_Parse(parser, buffer, (int)n, n < sizeof(buffer)) == 0) break;
      if (n <= sizeof(buffer) - 1) {
        result = 0;
        break;
      }
    }

    fclose(file);
    XML_ParserFree(parser);

    goto done;
  }

  char *unzipped = malloc(SECNATIVE_FEATURE_UNZIP_BUFFER_SIZE);
  if (unzipped == NULL) {
    XML_ParserFree(parser);
    fclose(file);

    goto done;
  }

  if (secnativefeature_get_unzip_data(file, unzipped) == 0) {
    XML_ParserFree(parser);
    fclose(file);
    free(unzipped);

    goto done;
  }

  size_t xml_len = strlen(unzipped);

  if (XML_Parse(parser, unzipped, (int)xml_len, 1) == 0) {
    fclose(file);
    XML_ParserFree(parser);
    free(unzipped);

    goto done;
  }

  free(unzipped);
  XML_ParserFree(parser);
  fclose(file);

  result = 0;

  done:
    secnativefeature_string_clear(&state.tag);
    secnativefeature_string_clear(&state.attr);

    return result;
}

/* INFO: Clears the map, resolves mdc.vendor.path and loads the
           cscfeature file if it exists. */
void secnativefeature_load_feature_file(struct secnativefeature *instance) {
  char path[SECNATIVE_FEATURE_BUFFER_SIZE] = { 0 };

  secnativefeature_map_clear(&instance->map);
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_PATH, path, SECNATIVE_FEATURE_PROPERTY_PATH_DEFAULT);

  char *full_path = malloc(SECNATIVE_FEATURE_BUFFER_SIZE);
  if (full_path == NULL) return;

  snprintf(full_path, SECNATIVE_FEATURE_BUFFER_SIZE, "%s%s", path, SECNATIVE_FEATURE_STRING_CSCFEATURE_FILE);

  if (access(full_path, F_OK) == 0) secnativefeature_load_feature(full_path, instance);

  free(full_path);
}

const char *secnativefeature_get(struct secnativefeature *instance, const char *feature_name) {
  char property[96] = { 0 };
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_UPDATE, property, "");

  if (strcmp(property, secnativefeature_omc_update_version) != 0) {
    snprintf(secnativefeature_omc_update_version, SECNATIVE_FEATURE_BUFFER_SIZE, "%s", property);
    secnativefeature_load_feature_file(instance);
  }

  struct secnativefeature_string key = { 0 };
  const char *value = NULL;
  if (instance->map != NULL) {
    secnativefeature_string_set(&key, feature_name);
    secnativefeature_string_append(&key, ",", 1);
    secnativefeature_string_append(&key, secnativefeature_country_iso, strlen(secnativefeature_country_iso));

    struct secnativefeature_entry *entry = secnativefeature_map_find(instance->map, key.data);

    if (entry != NULL) {
      value = entry->value;
    } else {
      secnativefeature_string_set(&key, feature_name);
      entry = secnativefeature_map_find(instance->map, key.data);

      if (entry != NULL) value = entry->value;
    }
  }

  secnativefeature_string_clear(&key);

  return value;
}

/* INFO: Re-reads the debug-level and ship properties directly, like the
           original isDebugEnabled (it does not consult the cached
           constructor-time bool). */
int secnativefeature_is_debug_enabled(void) {
  char debug_level[SECNATIVE_FEATURE_BUFFER_SIZE] = { 0 };
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_DEBUG_LEVEL, debug_level, "");

  char product_ship[SECNATIVE_FEATURE_BUFFER_SIZE] = { 0 };
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_SHIP, product_ship, "false");

  return strcmp(debug_level, SECNATIVE_FEATURE_STRING_DEBUG_LEVEL) == 0 &&
         strcmp(product_ship, "true") != 0;
}

/* INFO: Compares mdc.omc.update_version against the cached value,
           updating the cache and reporting whether it changed. Unlike
           get, it does not trigger a reload. */
int secnativefeature_is_feature_changed(void) {
  char property[96] = { 0 };
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_UPDATE, property, "");

  if (strcmp(property, secnativefeature_omc_update_version) == 0) return 0;

  snprintf(secnativefeature_omc_update_version, SECNATIVE_FEATURE_BUFFER_SIZE, "%s", property);

  return 1;
}

static void secnativefeature_instance_init(struct secnativefeature *instance) {
  instance->map = NULL;
  instance->is_debug_enabled = 0;

  char debug_level[SECNATIVE_FEATURE_BUFFER_SIZE] = { 0 };
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_DEBUG_LEVEL, debug_level, "");

  char product_ship[SECNATIVE_FEATURE_BUFFER_SIZE] = { 0 };
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_SHIP, product_ship, "false");

  int debug_enabled = strcmp(debug_level, SECNATIVE_FEATURE_STRING_DEBUG_LEVEL) == 0;
  int not_shipped = strcmp(product_ship, "true") != 0;

  instance->is_debug_enabled = debug_enabled && not_shipped;

  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_COUNTRY, secnativefeature_country_iso, "");
  secnativefeature_property_get(SECNATIVE_FEATURE_PROPERTY_UPDATE, secnativefeature_omc_update_version, "");

  secnativefeature_load_feature_file(instance);
}

/* INFO: Lazy singleton, not thread-safe (like the original). */
struct secnativefeature *secnativefeature_get_instance(void) {
  if (_ZN16SecNativeFeature9_instanceE == NULL) {
    _ZN16SecNativeFeature9_instanceE = malloc(sizeof(*_ZN16SecNativeFeature9_instanceE));
    if (_ZN16SecNativeFeature9_instanceE != NULL) secnativefeature_instance_init(_ZN16SecNativeFeature9_instanceE);
  }

  return _ZN16SecNativeFeature9_instanceE;
}

static int secnativefeature_get_enable_status(struct secnativefeature *instance, const char *feature_name) {
  const char *value = secnativefeature_get(instance, feature_name);
  if (value != NULL && (strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0))
    return 1;

  return 0;
}

static int secnativefeature_get_enable_status_default(struct secnativefeature *instance,
                                                      const char *feature_name, int default_value) {
  const char *value = secnativefeature_get(instance, feature_name);
  if (value != NULL) {
    if (strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0)
      default_value = 1;
    else if (strcmp(value, "false") == 0 || strcmp(value, "FALSE") == 0)
      default_value = 0;
  }

  return default_value;
}

static const char *secnativefeature_get_string(struct secnativefeature *instance, const char *feature_name) {
  const char *value = secnativefeature_get(instance, feature_name);
  if (value == NULL) return "";

  return value;
}

static const char *secnativefeature_get_string_default(struct secnativefeature *instance,
                                                       const char *feature_name, const char *default_value) {
  const char *value = secnativefeature_get(instance, feature_name);
  if (value == NULL) return default_value;

  return value;
}

static int secnativefeature_get_integer(struct secnativefeature *instance, const char *feature_name) {
  const char *value = secnativefeature_get(instance, feature_name);
  if (value == NULL) return -1;

  return atoi(value);
}

static int secnativefeature_get_integer_default(struct secnativefeature *instance, const char *feature_name, int default_value) {
  const char *value = secnativefeature_get(instance, feature_name);
  if (value == NULL) return default_value;

  return atoi(value);
}

int SecNativeFeature_getEnableStatus(const char *feature_name) {
  struct secnativefeature *instance = secnativefeature_get_instance();

  if (instance == NULL) return 0;

  return secnativefeature_get_enable_status(instance, feature_name);
}

int SecNativeFeature_getEnableStatusWithDefault(const char *feature_name, int default_value) {
  struct secnativefeature *instance = secnativefeature_get_instance();
  if (instance == NULL) return default_value;

  return secnativefeature_get_enable_status_default(instance, feature_name, default_value != 0);
}

int SecNativeFeature_getInteger(const char *feature_name) {
  struct secnativefeature *instance = secnativefeature_get_instance();

  if (instance == NULL) return -1;

  return secnativefeature_get_integer(instance, feature_name);
}

int SecNativeFeature_getIntegerWithDefault(const char *feature_name, int default_value) {
  struct secnativefeature *instance = secnativefeature_get_instance();
  if (instance == NULL) return default_value;

  return secnativefeature_get_integer_default(instance, feature_name, default_value);
}

const char *SecNativeFeature_getString(const char *feature_name) {
  struct secnativefeature *instance = secnativefeature_get_instance();
  if (instance == NULL) return NULL;

  return secnativefeature_get_string(instance, feature_name);
}

const char *SecNativeFeature_getStringWithDefault(const char *feature_name, const char *default_value) {
  struct secnativefeature *instance = secnativefeature_get_instance();
  if (instance == NULL) return default_value;

  return secnativefeature_get_string_default(instance, feature_name, default_value);
}
