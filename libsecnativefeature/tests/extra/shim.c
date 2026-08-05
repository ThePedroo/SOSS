/* INFO: Property and stdio shim for the libsecnativefeature tests */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#define SNF_SHIM_MAX_PROPERTIES 16
#define SNF_SHIM_MAX_FILES 8
#define SNF_SHIM_PROPERTY_VALUE_MAX 92

struct snf_shim_property {
  char name[SNF_SHIM_PROPERTY_VALUE_MAX];
  char value[SNF_SHIM_PROPERTY_VALUE_MAX];
};

struct snf_shim_file {
  char *path;
  char *data;
  size_t size;
};

struct snf_shim_stream {
  const char *data;
  size_t size;
  size_t pos;
  int error;
};

static struct snf_shim_property snf_shim_properties[SNF_SHIM_MAX_PROPERTIES];
static struct snf_shim_file snf_shim_files[SNF_SHIM_MAX_FILES];

void shim_reset(void) {
  for (size_t i = 0; i < SNF_SHIM_MAX_PROPERTIES; i++) {
    snf_shim_properties[i].name[0] = 0;
    snf_shim_properties[i].value[0] = 0;
  }

  for (size_t i = 0; i < SNF_SHIM_MAX_FILES; i++) {
    free(snf_shim_files[i].path);
    free(snf_shim_files[i].data);
    snf_shim_files[i].path = NULL;
    snf_shim_files[i].data = NULL;
    snf_shim_files[i].size = 0;
  }
}

void shim_set_property(const char *name, const char *value) {
  for (size_t i = 0; i < SNF_SHIM_MAX_PROPERTIES; i++) {
    if (snf_shim_properties[i].name[0] != 0 && strcmp(snf_shim_properties[i].name, name) != 0)
      continue;

    strncpy(snf_shim_properties[i].name, name, SNF_SHIM_PROPERTY_VALUE_MAX - 1);
    strncpy(snf_shim_properties[i].value, value, SNF_SHIM_PROPERTY_VALUE_MAX - 1);

    return;
  }
}

int shim_register_file(const char *path, const char *data, size_t size) {
  for (size_t i = 0; i < SNF_SHIM_MAX_FILES; i++) {
    if (snf_shim_files[i].path)
      continue;

    size_t path_len = strlen(path);
    char *path_copy = malloc(path_len + 1);
    char *data_copy = malloc(size);

    memcpy(path_copy, path, path_len + 1);
    memcpy(data_copy, data, size);
    snf_shim_files[i].path = path_copy;
    snf_shim_files[i].data = data_copy;
    snf_shim_files[i].size = size;

    return 0;
  }

  return -1;
}

void shim_clear_files(void) {
  for (size_t i = 0; i < SNF_SHIM_MAX_FILES; i++) {
    free(snf_shim_files[i].path);
    free(snf_shim_files[i].data);
    snf_shim_files[i].path = NULL;
    snf_shim_files[i].data = NULL;
    snf_shim_files[i].size = 0;
  }
}

/* INFO: libcutils-compatible property_get for the original library. */
int property_get(const char *key, char *value, const char *default_value) {
  for (size_t i = 0; i < SNF_SHIM_MAX_PROPERTIES; i++) {
    if (snf_shim_properties[i].name[0] == 0) continue;
    if (strcmp(snf_shim_properties[i].name, key) != 0) continue;

    strncpy(value, snf_shim_properties[i].value, SNF_SHIM_PROPERTY_VALUE_MAX - 1);

    return (int)strlen(snf_shim_properties[i].value);
  }

  size_t default_len = strlen(default_value);

  if (default_len >= SNF_SHIM_PROPERTY_VALUE_MAX) default_len = SNF_SHIM_PROPERTY_VALUE_MAX - 1;

  memcpy(value, default_value, default_len);
  value[default_len] = 0;

  return (int)default_len;
}

/* INFO: bionic-compatible __system_property_get for the reimplementation;
         a missing property leaves the buffer untouched and returns -1. */
int __system_property_get(const char *key, char *value) {
  for (size_t i = 0; i < SNF_SHIM_MAX_PROPERTIES; i++) {
    if (snf_shim_properties[i].name[0] == 0) continue;
    if (strcmp(snf_shim_properties[i].name, key) != 0) continue;

    strncpy(value, snf_shim_properties[i].value, SNF_SHIM_PROPERTY_VALUE_MAX - 1);

    return (int)strlen(snf_shim_properties[i].value);
  }

  return -1;
}

int access(const char *path, int mode) {
  (void) mode;

  for (size_t i = 0; i < SNF_SHIM_MAX_FILES; i++) {
    if (snf_shim_files[i].path != NULL && strcmp(snf_shim_files[i].path, path) == 0)
      return 0;
  }

  errno = ENOENT;

  return -1;
}

FILE *fopen(const char *path, const char *mode) {
  (void) mode;

  for (size_t i = 0; i < SNF_SHIM_MAX_FILES; i++) {
    if (snf_shim_files[i].path == NULL) continue;
    if (strcmp(snf_shim_files[i].path, path) != 0) continue;

    struct snf_shim_stream *stream = malloc(sizeof(*stream));
    if (stream == NULL) return NULL;

    stream->data = snf_shim_files[i].data;
    stream->size = snf_shim_files[i].size;
    stream->pos = 0;
    stream->error = 0;

    return (FILE *)stream;
  }

  errno = ENOENT;

  return NULL;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream_ptr) {
  struct snf_shim_stream *stream = (struct snf_shim_stream *)stream_ptr;
  size_t total = size * nmemb;

  if (total == 0) return 0;
  if (stream->pos >= stream->size) return 0;

  size_t available = stream->size - stream->pos;
  if (available > total) available = total;

  memcpy(ptr, stream->data + stream->pos, available);
  stream->pos += available;

  return available / size;
}

char *fgets(char *s, int n, FILE *stream_ptr) {
  struct snf_shim_stream *stream = (struct snf_shim_stream *)stream_ptr;
  size_t i = 0;

  if (n <= 0) return NULL;
  if (stream->pos >= stream->size) return NULL;

  while (i < (size_t)n - 1 && stream->pos < stream->size) {
    s[i] = stream->data[stream->pos];
    stream->pos++;
    i++;

    if (s[i - 1] == '\n') break;
  }

  s[i] = 0;

  return s;
}

int fseek(FILE *stream_ptr, long offset, int whence) {
  struct snf_shim_stream *stream = (struct snf_shim_stream *)stream_ptr;
  long pos = 0;

  if (whence == SEEK_SET) pos = offset;
  else if (whence == SEEK_CUR) pos = (long)stream->pos + offset;
  else if (whence == SEEK_END) pos = (long)stream->size + offset;
  else {
    errno = EINVAL;

    return -1;
  }

  if (pos < 0) {
    errno = EINVAL;

    return -1;
  }

  stream->pos = (size_t)pos;

  return 0;
}

long ftell(FILE *stream_ptr) {
  struct snf_shim_stream *stream = (struct snf_shim_stream *)stream_ptr;

  return (long)stream->pos;
}

void rewind(FILE *stream_ptr) {
  struct snf_shim_stream *stream = (struct snf_shim_stream *)stream_ptr;

  stream->pos = 0;
  stream->error = 0;
}

int ferror(FILE *stream_ptr) {
  struct snf_shim_stream *stream = (struct snf_shim_stream *)stream_ptr;

  return stream->error;
}

int fclose(FILE *stream_ptr) {
  free(stream_ptr);

  return 0;
}
