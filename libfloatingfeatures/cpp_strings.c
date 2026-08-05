/* INFO: C implementation for reading C++ std::string and building it from a C string.
           Handles libc++ std::string layout with SSO (Small String Optimization).

         libc++ layout:
          - Short mode (LSB of first byte = 0): size = first_byte >> 1, data at byte 1
          - Long mode: capacity/size/pointer at platform-specific offsets

   SOURCE: https://github.com/PerformanC/ReZygisk/blob/main/loader/src/injector/cpp_strings.c
*/

#include <stdlib.h>
#include <string.h>

#include "cpp_strings.h"

/* INFO: libc++ basic_string short-string capacity for char on LP64:
         (sizeof(__long) - 1), where __long is 24 bytes. */
#define STD_STRING_MIN_CAP 23

#ifdef __LP64__
  #define LONG_SIZE_OFFSET 8
  #define LONG_DATA_OFFSET 16
#else
  #define LONG_SIZE_OFFSET 4
  #define LONG_DATA_OFFSET 8
#endif

/* INFO: In libc++ little-endian: LSB of first byte = 0 means short mode */
static inline bool is_short_string(const unsigned char *bytes) {
  return (bytes[0] & 1) == 0;
}

size_t get_std_string_length(const struct std_string *string) {
  if (string == NULL) return 0;

  const unsigned char *bytes = (const unsigned char *)string;
  if (is_short_string(bytes)) return bytes[0] >> 1;

  return *(const size_t *)((const char *)string + LONG_SIZE_OFFSET);
}

const char *read_std_string(const struct std_string *string) {
  if (string == NULL) return NULL;

  const unsigned char *bytes = (const unsigned char *)string;
  if (is_short_string(bytes)) return (const char *)(bytes + 1);

  return *(const char **)((const char *)string + LONG_DATA_OFFSET);
}

/* INFO: Build a std::string in the caller's storage from a C string.
         Matches what read_std_string/get_std_string_length expect, and
         is laid out exactly like libc++'s own representation so the
         object is also safe to hand to a real libc++ destructor. */
void init_std_string(struct std_string *string, const char *text) {
  unsigned char *bytes = (unsigned char *)string;
  size_t len = strlen(text);

  if (len < STD_STRING_MIN_CAP) {
    /* INFO: Short form: bit0 = 0 (not long), size in the upper seven
             bits of the first byte, payload from byte 1. */

    bytes[0] = (unsigned char)(len << 1);
    memcpy(bytes + 1, text, len);
    bytes[1 + len] = 0;
  } else {
    /* INFO: Long form: bit0 = 1, capacity in bits 1..63 of the first
             word, size at offset 8, heap pointer at offset 16. */

    char *data = malloc(len + 1);
    if (data == NULL) abort();

    memcpy(data, text, len + 1);

    size_t cap_word = ((len + 1) << 1) | 1;
    memcpy(bytes + 0, &cap_word, sizeof(cap_word));
    memcpy(bytes + 8, &len, sizeof(len));
    memcpy(bytes + 16, &data, sizeof(data));
  }
}
