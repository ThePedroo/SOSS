#ifndef GATEKEEPER_H
#define GATEKEEPER_H

#include <stddef.h>
#include <stdint.h>

#include "teec.h"

#define GATEKEEPER_MODULE_TAG 0x48574D54u
#define GATEKEEPER_DEVICE_TAG 0x48574454u

#define GATEKEEPER_ERROR_BAD_PARAMETERS (-22)
#define GATEKEEPER_ERROR_OUT_OF_MEMORY (-12)
#define GATEKEEPER_ERROR_TZ_OPEN (-70)
#define GATEKEEPER_ERROR_INVOKE (-901)

/* INFO: Shared-memory buffer sizes of the TEE commands, in bytes */
extern const uint64_t shmem_sz_cmd;
extern const uint64_t shmem_sz_pwd;
extern const uint64_t shmem_sz_pwd_handle;

struct gatekeeper_module;
struct gatekeeper_device;

struct gatekeeper_module_methods {
  int (*open)(const struct gatekeeper_module *module, const char *id, struct gatekeeper_device **device);
};

struct gatekeeper_module {
  uint32_t tag;
  uint16_t module_api_version;
  uint16_t hal_api_version;
  const char *id;
  const char *name;
  const char *author;
  struct gatekeeper_module_methods *methods;
  void *dso;
  uint64_t reserved[25];
};

struct gatekeeper_device {
  uint32_t tag;
  uint32_t version;
  void *module;
  uint8_t reserved[0x60];
  int (*close)(struct gatekeeper_device *device);
  int (*enroll)(struct gatekeeper_device *device, uint32_t uid,
                const uint8_t *current_password_handle,
                uint32_t current_password_handle_length,
                const uint8_t *current_password,
                uint32_t current_password_length,
                const uint8_t *desired_password,
                uint32_t desired_password_length,
                uint8_t **enrolled_password_handle,
                uint32_t *enrolled_password_handle_length); /* 0x78 */
  int (*verify)(struct gatekeeper_device *device, uint32_t uid, uint64_t challenge,
                const uint8_t *enrolled_password_handle,
                uint32_t enrolled_password_handle_length,
                const uint8_t *provided_password,
                uint32_t provided_password_length,
                uint8_t **auth_token, uint32_t *auth_token_length,
                uint8_t *request_reenroll);
  void *delete_auth_token;
  void *delete_user;
};

extern struct gatekeeper_module HMI;

/* INFO: Internal entry points */
int gatekeeper_open(const struct gatekeeper_module *module, const char *name,
                    struct gatekeeper_device **device);

int gatekeeper_close(struct gatekeeper_device *device);

int gatekeeper_enroll(struct gatekeeper_device *device, uint32_t uid,
                      const uint8_t *current_password_handle,
                      uint32_t current_password_handle_length,
                      const uint8_t *current_password,
                      uint32_t current_password_length,
                      const uint8_t *desired_password,
                      uint32_t desired_password_length,
                      uint8_t **enrolled_password_handle,
                      uint32_t *enrolled_password_handle_length);

int gatekeeper_verify(struct gatekeeper_device *device, uint32_t uid, uint64_t challenge,
                      const uint8_t *enrolled_password_handle,
                      uint32_t enrolled_password_handle_length,
                      const uint8_t *provided_password,
                      uint32_t provided_password_length,
                      uint8_t **auth_token, uint32_t *auth_token_length,
                      uint8_t *request_reenroll);

#endif /* GATEKEEPER_H */
