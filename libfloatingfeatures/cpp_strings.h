#ifndef CPP_STRINGS_H
#define CPP_STRINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* INFO: A libc++ std::string is 24 bytes on LP64, 12 bytes on 32-bit. */
#ifdef __LP64__
  #define STD_STRING_SIZE 24
#else
  #define STD_STRING_SIZE 12
#endif

/* INFO: Storage mirroring a libc++ std::string */
struct std_string {
  union {
    char bytes[STD_STRING_SIZE];
    uint64_t align;
  } u;
};

/* INFO: Read a C string pointer from a std::string object.
           The returned pointer is valid only as long as the std::string exists. */
const char *read_std_string(const struct std_string *string);

/* INFO: Get the length of a std::string object (not including null terminator). */
size_t get_std_string_length(const struct std_string *string);

/* INFO: Construct a std::string in the caller's storage from a C string. */
void init_std_string(struct std_string *string, const char *text);

#endif /* CPP_STRINGS_H */
