/* INFO: Unit test for libgatekeeper: exported symbols, HMI table, and
         the open/close, enroll and verify flows against a stubbed TEE
         client.  The raw TEEC entry points are defined here under their
         real names, so linking test and library together substitutes
         the stub at link time without touching the device TEE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <stdint.h>

#include "../gatekeeper.h"
#include "extra/test_util.h"

#ifndef GATEKEEPER_TA_PATH
  #define GATEKEEPER_TA_PATH "/vendor/tee/00000000-0000-0000-0000-474154454b45"
#endif

#define GATEKEEPER_HEADER_OFFSET 1024u
#define GATEKEEPER_ENROLL_COMMAND 63
#define GATEKEEPER_VERIFY_COMMAND 126

/* INFO: Stub TEE client layer */
static int testsuite_stub_invoke_mode;
static int testsuite_stub_initialize_mode;
static int testsuite_stub_allocate_mode;
static int testsuite_stub_invoke_count;
static int testsuite_stub_release_count;
static uint64_t testsuite_stub_seen_challenge;
static uint32_t testsuite_stub_seen_uid;
static uint32_t testsuite_stub_seen_eph_len;
static uint32_t testsuite_stub_seen_pp_len;
static uint8_t *testsuite_stub_seen_pwd_data;

TEEC_Result TEEC_InitializeContext(const char *name, struct TEEC_Context *context) {
  (void) name;

  if (testsuite_stub_initialize_mode != 0) return TEEC_ERROR_GENERIC;

  context->imp = (void *)0x1001;

  return TEEC_SUCCESS;
}

void TEEC_FinalizeContext(struct TEEC_Context *context) {
  context->imp = NULL;
}

TEEC_Result TEECS_OpenSession(struct TEEC_Context *context, struct TEEC_Session *session,
                                     const struct TEEC_UUID *destination, const void *ta_buffer,
                                     size_t ta_size, uint32_t connection_method,
                                     const void *connection_data, struct TEEC_Operation *operation,
                                     uint32_t *return_origin) {
  (void) context;
  (void) destination;
  (void) ta_buffer;
  (void) ta_size;
  (void) connection_method;
  (void) connection_data;
  (void) operation;

  session->imp = (void *)0x1002;

  if (return_origin != NULL) *return_origin = 0;

  return TEEC_SUCCESS;
}

TEEC_Result TEEC_CloseSession(struct TEEC_Session *session) {
  session->imp = NULL;

  return TEEC_SUCCESS;
}

TEEC_Result TEEC_AllocateSharedMemory(struct TEEC_Context *context,
                                               struct TEEC_SharedMemory *shared_memory) {
  (void) context;

  if (testsuite_stub_allocate_mode != 0) return TEEC_ERROR_OUT_OF_MEMORY;

  shared_memory->buffer = calloc(1, shared_memory->size);
  if (shared_memory->buffer == NULL) return TEEC_ERROR_OUT_OF_MEMORY;

  return TEEC_SUCCESS;
}

void TEEC_ReleaseSharedMemory(struct TEEC_SharedMemory *shared_memory) {
  testsuite_stub_release_count++;
  free(shared_memory->buffer);
  shared_memory->buffer = NULL;
  shared_memory->size = 0;
  shared_memory->flags = 0;
}

const char *get_error_text(void) {
  return "stub error";
}

TEEC_Result TEEC_InvokeCommand(struct TEEC_Session *session, uint32_t command_id,
                                       struct TEEC_Operation *operation, uint32_t *return_origin) {
  (void) session;

  testsuite_stub_invoke_count++;

  if (return_origin != NULL) *return_origin = 0;

  if (testsuite_stub_invoke_mode == 1) return TEEC_ERROR_GENERIC;

  if (command_id == GATEKEEPER_ENROLL_COMMAND) {
    uint8_t *response = (uint8_t *)operation->params[0].buffer;

    memset(response, 0, 1040);
    memcpy(response, "hello", 5);

    if (testsuite_stub_invoke_mode == 2) {
      uint64_t header = ((uint64_t)2 << 32) | 0x400u;
      uint32_t timeout = 1500;

      memcpy(response + GATEKEEPER_HEADER_OFFSET, &header, sizeof(header));
      memcpy(response + GATEKEEPER_HEADER_OFFSET + 8, &timeout, sizeof(timeout));
    } else {
      uint64_t header = 5;

      memcpy(response + GATEKEEPER_HEADER_OFFSET, &header, sizeof(header));
    }

    return TEEC_SUCCESS;
  }

  if (command_id == GATEKEEPER_VERIFY_COMMAND) {
    struct TEEC_SharedMemory *cmd = (struct TEEC_SharedMemory *)operation->params[0].buffer;
    uint8_t *buffer = (uint8_t *)cmd->buffer;

    uint64_t challenge = 0;
    uint32_t uid = 0;

    memcpy(&challenge, buffer + GATEKEEPER_HEADER_OFFSET, sizeof(challenge));
    memcpy(&uid, buffer + GATEKEEPER_HEADER_OFFSET + 21, sizeof(uid));
    testsuite_stub_seen_challenge = challenge;
    testsuite_stub_seen_uid = uid;
    testsuite_stub_seen_eph_len = (uint32_t)operation->params[1].size;
    testsuite_stub_seen_pp_len = (uint32_t)operation->params[2].size;
    testsuite_stub_seen_pwd_data = (uint8_t *)((struct TEEC_SharedMemory *)operation->params[2].buffer)->buffer;

    uint32_t response_length = 5;
    memset(buffer, 0, 1049);
    memcpy(buffer, "tok01", 5);
    memcpy(buffer + GATEKEEPER_HEADER_OFFSET, &challenge, sizeof(challenge));
    memcpy(buffer + GATEKEEPER_HEADER_OFFSET + 8, &response_length, sizeof(response_length));

    if (testsuite_stub_invoke_mode == 2) {
      uint32_t result_code = 2;
      uint32_t timeout = 2500;

      memcpy(buffer + GATEKEEPER_HEADER_OFFSET + 13, &result_code, sizeof(result_code));
      memcpy(buffer + GATEKEEPER_HEADER_OFFSET + 17, &timeout, sizeof(timeout));
    } else {
      uint32_t result_code = 0;
      uint8_t reenroll = 1;

      memcpy(buffer + GATEKEEPER_HEADER_OFFSET + 13, &result_code, sizeof(result_code));
      buffer[GATEKEEPER_HEADER_OFFSET + 12] = reenroll;
    }

    return TEEC_SUCCESS;
  }

  return TEEC_ERROR_GENERIC;
}

/* INFO: Resets the stub state and releases any TEE state left open by
         the previous test. */
static void testsuite_stub_reset(void) {
  gatekeeper_close(NULL);

  testsuite_stub_invoke_mode = 0;
  testsuite_stub_initialize_mode = 0;
  testsuite_stub_allocate_mode = 0;
  testsuite_stub_invoke_count = 0;
  testsuite_stub_release_count = 0;
  testsuite_stub_seen_challenge = 0;
  testsuite_stub_seen_uid = 0;
  testsuite_stub_seen_eph_len = 0;
  testsuite_stub_seen_pp_len = 0;
  testsuite_stub_seen_pwd_data = NULL;
}

static int testsuite_write_fake_ta(void) {
  FILE *file = fopen(GATEKEEPER_TA_PATH, "w");
  if (file == NULL) return -1;

  fputs("fake gatekeeper trustlet", file);
  fclose(file);

  return 0;
}

static void testsuite_exported_symbols(void) {
  void *handle = dlopen("libgatekeeper.so", RTLD_NOW);

  CHECK(handle != NULL);
  if (handle == NULL) return;

  struct gatekeeper_module *hmi = (struct gatekeeper_module *)dlsym(handle, "HMI");
  CHECK(hmi != NULL);

  if (hmi != NULL) {
    CHECK(hmi->tag == GATEKEEPER_MODULE_TAG);
    CHECK(hmi->module_api_version == 1);
    CHECK(hmi->hal_api_version == 0x0100);
    CHECK(hmi->id != NULL && strcmp(hmi->id, "gatekeeper") == 0);
    CHECK(hmi->name != NULL && strcmp(hmi->name, "Gatekeeper TEEGRIS HAL") == 0);
    CHECK(hmi->author != NULL && strcmp(hmi->author, "TEEGRIS") == 0);
    CHECK(hmi->methods != NULL && hmi->methods->open != NULL);
  }

  {
    const uint64_t *cmd = (const uint64_t *)dlsym(handle, "shmem_sz_cmd");
    const uint64_t *pwd_handle = (const uint64_t *)dlsym(handle, "shmem_sz_pwd_handle");
    const uint64_t *pwd = (const uint64_t *)dlsym(handle, "shmem_sz_pwd");

    CHECK(cmd != NULL && *cmd == 1080);
    CHECK(pwd_handle != NULL && *pwd_handle == 64);
    CHECK(pwd != NULL && *pwd == 128);
  }

  dlclose(handle);
}

static void testsuite_open_close(void) {
  testsuite_stub_reset();

  struct gatekeeper_device *device = NULL;
  int rc = gatekeeper_open(&HMI, "gatekeeper", &device);
  CHECK(rc == 0);

  if (rc == 0 && device != NULL) {
    CHECK(device->tag == GATEKEEPER_DEVICE_TAG);
    CHECK(device->version == 1);
    CHECK(device->close == gatekeeper_close);
    CHECK(device->enroll == gatekeeper_enroll);
    CHECK(device->verify == gatekeeper_verify);
    CHECK(device->delete_auth_token == NULL);
    CHECK(device->delete_user == NULL);

    rc = gatekeeper_close(device);
    CHECK(rc == 0);
  }

  CHECK(testsuite_stub_release_count == 3);
}

static void testsuite_open_initialize_failure(void) {
  testsuite_stub_reset();
  testsuite_stub_initialize_mode = 1;

  struct gatekeeper_device *device = NULL;
  int rc = gatekeeper_open(&HMI, "gatekeeper", &device);
  CHECK(rc == GATEKEEPER_ERROR_TZ_OPEN);
}

static void testsuite_open_allocate_failure(void) {
  testsuite_stub_reset();
  testsuite_stub_allocate_mode = 1;

  struct gatekeeper_device *device = NULL;
  int rc = gatekeeper_open(&HMI, "gatekeeper", &device);
  CHECK(rc == GATEKEEPER_ERROR_TZ_OPEN);
}

static void testsuite_enroll_success(void) {
  testsuite_stub_reset();

  struct gatekeeper_device device = { 0 };
  uint8_t current_password_handle[] = "old handle";
  uint8_t current_password[] = "old password";
  uint8_t desired_password[] = "new password";
  uint8_t *handle = NULL;
  uint32_t handle_length = 0;
  int rc = gatekeeper_enroll(&device, 1001, current_password_handle, sizeof(current_password_handle) - 1,
                             current_password, sizeof(current_password) - 1,
                             desired_password, sizeof(desired_password) - 1,
                             &handle, &handle_length);
  CHECK(rc == 0);
  CHECK(handle_length == 5);
  CHECK(handle != NULL && memcmp(handle, "hello", 5) == 0);
  free(handle);
}

static void testsuite_enroll_bad_parameters(void) {
  testsuite_stub_reset();

  struct gatekeeper_device device = { 0 };
  uint8_t desired_password[] = "new password";
  uint8_t *handle = NULL;
  uint32_t handle_length = 0;
  int rc = gatekeeper_enroll(&device, 0, NULL, 0, NULL, 0, desired_password,
                             sizeof(desired_password) - 1, NULL, NULL);
  CHECK(rc == GATEKEEPER_ERROR_BAD_PARAMETERS);

  rc = gatekeeper_enroll(&device, 0, NULL, 0, NULL, 0, NULL, 0, &handle, &handle_length);
  CHECK(rc == GATEKEEPER_ERROR_BAD_PARAMETERS);
}

static void testsuite_enroll_invoke_failure(void) {
  testsuite_stub_reset();
  testsuite_stub_invoke_mode = 1;

  struct gatekeeper_device device = { 0 };
  uint8_t desired_password[] = "new password";
  uint8_t *handle = NULL;
  uint32_t handle_length = 0;
  int rc = gatekeeper_enroll(&device, 1001, NULL, 0, NULL, 0, desired_password,
                         sizeof(desired_password) - 1, &handle, &handle_length);
  CHECK(rc == GATEKEEPER_ERROR_INVOKE);
}

static void testsuite_enroll_retry(void) {
  testsuite_stub_reset();
  testsuite_stub_invoke_mode = 2;

  struct gatekeeper_device device = { 0 };
  uint8_t desired_password[] = "new password";
  uint8_t *handle = NULL;
  uint32_t handle_length = 0;
  int rc = gatekeeper_enroll(&device, 1001, NULL, 0, NULL, 0, desired_password,
                             sizeof(desired_password) - 1, &handle, &handle_length);
  CHECK(rc == 1500);
}

static void testsuite_verify_bad_parameters(void) {
  testsuite_stub_reset();

  struct gatekeeper_device device = { 0 };
  uint8_t eph[] = "handle";
  uint8_t pp[] = "password";
  uint8_t *token = NULL;
  uint32_t token_length = 0;
  uint8_t reenroll = 0;
  int rc = gatekeeper_verify(&device, 0, 0, NULL, 0, NULL, 0, NULL, NULL, NULL);
  CHECK(rc == GATEKEEPER_ERROR_BAD_PARAMETERS);

  rc = gatekeeper_verify(&device, 0, 0, eph, 0x41, pp, sizeof(pp) - 1,
                         &token, &token_length, &reenroll);
  CHECK(rc == GATEKEEPER_ERROR_BAD_PARAMETERS);

  rc = gatekeeper_verify(&device, 0, 0, eph, sizeof(eph) - 1, pp, 0x81,
                         &token, &token_length, &reenroll);
  CHECK(rc == GATEKEEPER_ERROR_BAD_PARAMETERS);
}

static void testsuite_verify_success(void) {
  testsuite_stub_reset();

  struct gatekeeper_device *device = NULL;
  uint8_t eph[] = "handle";
  uint8_t pp[] = "password";
  uint8_t *token = NULL;
  uint32_t token_length = 0;
  uint8_t reenroll = 0;
  int rc = gatekeeper_open(&HMI, "gatekeeper", &device);
  CHECK(rc == 0);

  if (rc == 0) {
    rc = gatekeeper_verify(device, 2002, 0x1122334455667788ULL, eph, sizeof(eph) - 1,
                           pp, sizeof(pp) - 1, &token, &token_length, &reenroll);
    CHECK(rc == 0);
    CHECK(token_length == 5);
    CHECK(token != NULL && memcmp(token, "tok01", 5) == 0);
    CHECK(reenroll == 1);
    CHECK(testsuite_stub_seen_challenge == 0x1122334455667788ULL);
    CHECK(testsuite_stub_seen_uid == 2002);
    CHECK(testsuite_stub_seen_eph_len == sizeof(eph) - 1);
    CHECK(testsuite_stub_seen_pp_len == sizeof(pp) - 1);
    CHECK(testsuite_stub_seen_pwd_data != NULL);

    if (testsuite_stub_seen_pwd_data != NULL) {
      CHECK(testsuite_stub_seen_pwd_data[0] == 0);
      CHECK(memcmp(testsuite_stub_seen_pwd_data, pp, sizeof(pp) - 1) != 0);
    }

    free(token);
    gatekeeper_close(device);
  }
}

static void testsuite_verify_invoke_failure(void) {
  testsuite_stub_reset();

  struct gatekeeper_device *device = NULL;
  uint8_t eph[] = "handle";
  uint8_t pp[] = "password";
  uint8_t *token = NULL;
  uint32_t token_length = 0;
  uint8_t reenroll = 0;
  int rc = gatekeeper_open(&HMI, "gatekeeper", &device);
  CHECK(rc == 0);

  if (rc == 0) {
    testsuite_stub_invoke_mode = 1;
    rc = gatekeeper_verify(device, 2002, 0x1122334455667788ULL, eph, sizeof(eph) - 1,
                           pp, sizeof(pp) - 1, &token, &token_length, &reenroll);
    CHECK(rc == GATEKEEPER_ERROR_INVOKE);

    gatekeeper_close(device);
  }
}

static void testsuite_verify_retry(void) {
  testsuite_stub_reset();

  struct gatekeeper_device *device = NULL;
  uint8_t eph[] = "handle";
  uint8_t pp[] = "password";
  uint8_t *token = NULL;
  uint32_t token_length = 0;
  uint8_t reenroll = 0;
  int rc = gatekeeper_open(&HMI, "gatekeeper", &device);
  CHECK(rc == 0);

  if (rc == 0) {
    testsuite_stub_invoke_mode = 2;
    rc = gatekeeper_verify(device, 2002, 0x1122334455667788ULL, eph, sizeof(eph) - 1,
                           pp, sizeof(pp) - 1, &token, &token_length, &reenroll);
    CHECK(rc == 2500);

    gatekeeper_close(device);
  }
}

int main(void) {
  if (testsuite_write_fake_ta() != 0) {
    printf("cannot write fake trustlet at %s\n", GATEKEEPER_TA_PATH);
    test_failures++;
  }

  testsuite_exported_symbols();
  testsuite_open_close();
  testsuite_open_initialize_failure();
  testsuite_open_allocate_failure();
  testsuite_enroll_success();
  testsuite_enroll_bad_parameters();
  testsuite_enroll_invoke_failure();
  testsuite_enroll_retry();
  testsuite_verify_bad_parameters();
  testsuite_verify_success();
  testsuite_verify_invoke_failure();
  testsuite_verify_retry();

  if (test_failures == 0) {
    printf("OK all checks passed\n");

    return 0;
  }
  printf("FAILED %d checks\n", test_failures);

  return 1;
}
