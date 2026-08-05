#ifndef SNF_SHIM_H
#define SNF_SHIM_H

#include <stddef.h>

void shim_reset(void);

void shim_set_property(const char *name, const char *value);

int shim_register_file(const char *path, const char *data, size_t size);

void shim_clear_files(void);

#endif /* SNF_SHIM_H */
