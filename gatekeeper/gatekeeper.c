#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/types.h>

#include <android/log.h>
#include <unistd.h>

#include "gatekeeper.h"

#ifndef GATEKEEPER_TA_PATH
  #define GATEKEEPER_TA_PATH "/vendor/tee/00000000-0000-0000-0000-474154454b45"
#endif

#define GATEKEEPER_ENROLL_COMMAND 63
#define GATEKEEPER_VERIFY_COMMAND 126
#define GATEKEEPER_HEADER_OFFSET 1024u
#define GATEKEEPER_RESPONSE_LENGTH_INITIAL 1024u
#define GATEKEEPER_VERIFY_CMD_PARAM_SIZE 1049u
#define GATEKEEPER_MAX_TOKEN_SIZE 0x401u
#define GATEKEEPER_MAX_ENROLLED_PASSWORD_HANDLE 0x40u
#define GATEKEEPER_MAX_PROVIDED_PASSWORD 0x81u
#define GATEKEEPER_ENROLL_PARAM_TYPES 0x5557u
#define GATEKEEPER_VERIFY_PARAM_TYPES 0x0DDFu
#define GATEKEEPER_RESULT_RETRY 2u

/* INFO: Protocol generations:
          - v1: uses commands 0/1 with the response at the buffer start
          - v2: uses commands 63/126 at offset 1024.
*/
#define GATEKEEPER_PROTOCOL_V1 1u
#define GATEKEEPER_PROTOCOL_V2 2u
#define GATEKEEPER_PROBE_PASSWORD 0x01u
#define GATEKEEPER_PROBE_HANDLE_SIZE 64u
#define GATEKEEPER_PROBE_PWD_SIZE 16u
#define GATEKEEPER_ENROLL_COMMAND_V1 0
#define GATEKEEPER_VERIFY_COMMAND_V1 1

/* INFO: Optional built-time force to allow test suites */
#ifndef GATEKEEPER_FORCE_PROTOCOL
  #define GATEKEEPER_FORCE_PROTOCOL 0
#endif

#define GATEKEEPER_SHMEM_CMD_FLAGS (TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)
#define GATEKEEPER_SHMEM_PWD_HANDLE_FLAGS TEEC_MEM_INPUT
#define GATEKEEPER_SHMEM_PWD_FLAGS TEEC_MEM_INPUT

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gatekeeper", __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "gatekeeper", __VA_ARGS__)

struct gatekeeper_v1_enroll_response {
  uint32_t uid;
  uint32_t result;
  uint32_t token_length;
  uint32_t timeout;
  uint8_t token[GATEKEEPER_HEADER_OFFSET];
} __attribute__((packed));

struct gatekeeper_v1_verify_response {
  uint64_t challenge;
  uint32_t uid;
  uint32_t result;
  uint32_t timeout;
  uint8_t reenroll;
  uint32_t token_length;
  uint8_t token[GATEKEEPER_HEADER_OFFSET];
} __attribute__((packed));

struct gatekeeper_v2_enroll_response {
  uint8_t token[GATEKEEPER_HEADER_OFFSET];
  uint32_t token_length;
  uint32_t result;
  uint32_t timeout;
  uint32_t uid;
} __attribute__((packed));

struct gatekeeper_v2_verify_response {
  uint8_t token[GATEKEEPER_HEADER_OFFSET];
  uint64_t challenge;
  uint32_t length;
  uint8_t reenroll;
  uint32_t result;
  uint32_t timeout;
  uint32_t uid;
} __attribute__((packed));

/* INFO: Protocol views of the enroll and verify response buffers: the
         v1 and v2 generations lay the same fields out at different
         offsets, so each operation interprets the buffer with the
         layout of its protocol. */
union gatekeeper_enroll_response {
  struct gatekeeper_v1_enroll_response v1;
  struct gatekeeper_v2_enroll_response v2;
};

union gatekeeper_verify_response {
  struct gatekeeper_v1_verify_response v1;
  struct gatekeeper_v2_verify_response v2;
};

/* INFO: TEE state shared between the HAL methods */
static uint8_t gatekeeper_opened;

/* INFO: Probe picks the protocol at open unless compile-time forced */
#if GATEKEEPER_FORCE_PROTOCOL == 1
  static uint32_t gatekeeper_protocol = GATEKEEPER_PROTOCOL_V1;
#elif GATEKEEPER_FORCE_PROTOCOL == 2
  static uint32_t gatekeeper_protocol = GATEKEEPER_PROTOCOL_V2;
#else
  static uint32_t gatekeeper_protocol = GATEKEEPER_PROTOCOL_V2;
#endif
static struct TEEC_SharedMemory gatekeeper_shmem_cmd;
static struct TEEC_SharedMemory gatekeeper_shmem_pwd_handle;
static struct TEEC_SharedMemory gatekeeper_shmem_pwd;
static struct TEEC_Session gatekeeper_session;
static struct TEEC_Context gatekeeper_context;

/* INFO: libteecl derives the TA file name from the Trustlet UUID */
static const struct TEEC_UUID gatekeeper_ta_uuid = {
  0,
  0,
  0,
  { 0x00, 0x00, 0x47, 0x41, 0x54, 0x45, 0x4B, 0x45 },
};

/* INFO: Opens a fresh session with the trustlet. The probe tries one
         protocol per session: issuing an unknown command (the other
         protocol's) can tear the session down, so the next probe must
         run on a clean session. */
#if GATEKEEPER_FORCE_PROTOCOL == 0
  static TEEC_Result gatekeeper_reopen_session(void) {
    int fd = open(GATEKEEPER_TA_PATH, O_RDONLY);
    if (fd < 0) return TEEC_ERROR_GENERIC;

    struct stat status;
    if (fstat(fd, &status) != 0) {
      close(fd);

      return TEEC_ERROR_GENERIC;
    }

    size_t ta_size = (size_t)status.st_size;
    uint8_t *ta_buffer = (uint8_t *)malloc(ta_size);
    if (ta_buffer == NULL) {
      close(fd);

      return TEEC_ERROR_OUT_OF_MEMORY;
    }

    ssize_t read_count = read(fd, ta_buffer, ta_size);
    close(fd);
    if (read_count != (ssize_t)ta_size) {
      free(ta_buffer);

      return TEEC_ERROR_GENERIC;
    }

    uint32_t origin = 0;
    TEEC_Result open_result = TEECS_OpenSession(&gatekeeper_context, &gatekeeper_session,
                                                &gatekeeper_ta_uuid, ta_buffer, ta_size,
                                                0, NULL, NULL, &origin);

    free(ta_buffer);

    return open_result;
  }

/* INFO: Detects the protocol version based on the answer.  The v2
         trustlet answers command 126; the v1 trustlet answers the
         command 1, and an unknown command (v2 on a v1 trustlet, or
         v1 on a v2 trustlet) is rejected with TEEC_ERROR_GENERIC.

         A real trustlet that recognizes the command but gets
         unusable credentials answers a non-GENERIC error (e.g.
         TEEC_ERROR_BAD_PARAMETERS) instead of TEEC_SUCCESS, because
         it validates the enrolled handle before verifying.  A
         fabricated handle can never pass that validation, so only the
         error code tells a recognized command from an unknown one:
         any non-GENERIC outcome marks the command as belonging to
         the native protocol. */
  static void gatekeeper_probe_protocol(void) {
    struct TEEC_Operation operation = {
      .param_types = GATEKEEPER_VERIFY_PARAM_TYPES,
      .params = {
        {
          .buffer = &gatekeeper_shmem_cmd,
          .size = GATEKEEPER_VERIFY_CMD_PARAM_SIZE
        },
        {
          .buffer = &gatekeeper_shmem_pwd_handle,
          .size = GATEKEEPER_PROBE_HANDLE_SIZE
        },
        {
          .buffer = &gatekeeper_shmem_pwd,
          .size = GATEKEEPER_PROBE_PWD_SIZE
        }
      }
    };

    uint8_t *cmd_buffer = (uint8_t *)gatekeeper_shmem_cmd.buffer;
    uint8_t *pwd_handle_buffer = (uint8_t *)gatekeeper_shmem_pwd_handle.buffer;
    uint8_t *pwd_buffer = (uint8_t *)gatekeeper_shmem_pwd.buffer;
    struct gatekeeper_v2_verify_response *v2_response = (struct gatekeeper_v2_verify_response *)cmd_buffer;
    struct gatekeeper_v1_verify_response *v1_response = (struct gatekeeper_v1_verify_response *)cmd_buffer;

    /* INFO: Probe v2 command 126 first, the native protocol of this HAL */
    memset(cmd_buffer, 0, (size_t)shmem_sz_cmd);
    v2_response->length = GATEKEEPER_RESPONSE_LENGTH_INITIAL;
    v2_response->result = 0xFFFFFFFFu;
    pwd_handle_buffer[0] = GATEKEEPER_PROBE_PASSWORD;
    pwd_buffer[0] = GATEKEEPER_PROBE_PASSWORD;

    uint32_t origin = 0;
    TEEC_Result result = TEEC_InvokeCommand(&gatekeeper_session, GATEKEEPER_VERIFY_COMMAND, &operation, &origin);
    if (result == TEEC_SUCCESS && (v2_response->length != 0 || v2_response->result != 0)) {
      gatekeeper_protocol = GATEKEEPER_PROTOCOL_V2;

      LOGD("probe: detected v2 protocol\n");

      return;
    }

    if (result != TEEC_SUCCESS && result != TEEC_ERROR_GENERIC) {
      gatekeeper_protocol = GATEKEEPER_PROTOCOL_V2;

      LOGE("probe: v2 command 126 recognized. res = 0x%x, origin = 0x%x\n", result, origin);

      return;
    }

    if (result != TEEC_SUCCESS) {
      LOGE("probe: v2 command 126 failed. res = 0x%x, origin = 0x%x\n", result, origin);
    } else {
      LOGD("probe: v2 command 126 unanswered\n");
    }

    /* INFO: Probe v1 command 1 on a fresh session. A v2 trustlet rejected
             command 126 above, and this command only answers on a v1 trustlet. */
    TEEC_CloseSession(&gatekeeper_session);
    if (gatekeeper_reopen_session() != TEEC_SUCCESS) {
      LOGE("probe: session reopen failed\n");

      gatekeeper_protocol = GATEKEEPER_PROTOCOL_V1;

      return;
    }

    memset(cmd_buffer, 0, (size_t)shmem_sz_cmd);
    v1_response->result = 0xFFFFFFFFu;
    v1_response->token_length = GATEKEEPER_RESPONSE_LENGTH_INITIAL;

    result = TEEC_InvokeCommand(&gatekeeper_session, GATEKEEPER_VERIFY_COMMAND_V1, &operation, &origin);
    if (result == TEEC_SUCCESS && (v1_response->result != 0 || v1_response->token_length != GATEKEEPER_RESPONSE_LENGTH_INITIAL)) {
      gatekeeper_protocol = GATEKEEPER_PROTOCOL_V1;

      LOGD("probe: detected v1 protocol\n");

      return;
    }

    if (result != TEEC_SUCCESS && result != TEEC_ERROR_GENERIC) {
      gatekeeper_protocol = GATEKEEPER_PROTOCOL_V1;

      LOGE("probe: v1 command 1 recognized. res = 0x%x, origin = 0x%x\n", result, origin);

      return;
    }

    if (result != TEEC_SUCCESS) {
      LOGE("probe: v1 command 1 failed. res = 0x%x, origin = 0x%x\n", result, origin);
    } else {
      LOGD("probe: v1 command 1 unanswered\n");
    }

    gatekeeper_protocol = GATEKEEPER_PROTOCOL_V2;

    LOGE("probe: trustlet protocol not detected, defaulting to v2\n");
  }
#endif

static int gatekeeper_tz_open_session(TEEC_Result *result_out) {
  int fd = open(GATEKEEPER_TA_PATH, O_RDONLY);
  if (fd < 0) {
    LOGE("TA file path open() failed. errno = %d\n", errno);

    *result_out = 0;

    return -1;
  }

  struct stat status;
  if (fstat(fd, &status) != 0) {
    close(fd);
    *result_out = 0;

    return -1;
  }

  size_t ta_size = (size_t)status.st_size;
  uint8_t *ta_buffer = (uint8_t *)malloc(ta_size);
  if (ta_buffer == NULL) {
    LOGE("malloc() failed. errno = %d\n", errno);

    close(fd);
    *result_out = 0;

    return -1;
  }

  ssize_t read_count = read(fd, ta_buffer, ta_size);
  if (read_count != (ssize_t)ta_size) {
    LOGE("read() failed. Returned %d, errno = %d\n", (int)read_count, errno);

    free(ta_buffer);
    close(fd);
    *result_out = 0;

    return -1;
  }

  uint32_t origin = 0;
  TEEC_Result open_result = TEECS_OpenSession(&gatekeeper_context, &gatekeeper_session,
                                              &gatekeeper_ta_uuid, ta_buffer, ta_size,
                                              0, NULL, NULL, &origin);
  if (open_result != TEEC_SUCCESS) {
    LOGE("TEECS_OpenSession returned %x from %x %s\n", open_result, origin, get_error_text());

    free(ta_buffer);
    close(fd);
    *result_out = open_result;

    return -1;
  }

  LOGD("GATEKEEPER OpenSession is success\n");

  gatekeeper_shmem_cmd.size = shmem_sz_cmd;
  gatekeeper_shmem_cmd.flags = GATEKEEPER_SHMEM_CMD_FLAGS;
  gatekeeper_shmem_pwd_handle.size = shmem_sz_pwd_handle;
  gatekeeper_shmem_pwd_handle.flags = GATEKEEPER_SHMEM_PWD_HANDLE_FLAGS;
  gatekeeper_shmem_pwd.size = shmem_sz_pwd;
  gatekeeper_shmem_pwd.flags = GATEKEEPER_SHMEM_PWD_FLAGS;

  TEEC_Result result = TEEC_AllocateSharedMemory(&gatekeeper_context, &gatekeeper_shmem_cmd);
  if (result != TEEC_SUCCESS) {
    LOGE("TEEC_AllocateSharedMemory shmem cmd failed. %x , %s\n", result, get_error_text());

    TEEC_CloseSession(&gatekeeper_session);

    free(ta_buffer);
    close(fd);
    *result_out = result;

    return -1;
  }

  result = TEEC_AllocateSharedMemory(&gatekeeper_context, &gatekeeper_shmem_pwd_handle);
  if (result != TEEC_SUCCESS) {
    LOGE("TEEC_AllocateSharedMemory shmem pwd handle failed. %x , %s\n", result, get_error_text());

    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_cmd);
    TEEC_CloseSession(&gatekeeper_session);

    free(ta_buffer);
    close(fd);
    *result_out = result;

    return -1;
  }

  result = TEEC_AllocateSharedMemory(&gatekeeper_context, &gatekeeper_shmem_pwd);
  if (result != TEEC_SUCCESS) {
    LOGE("TEEC_AllocateSharedMemory shmem pwd failed. %x , %s\n", result, get_error_text());

    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_pwd_handle);
    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_cmd);
    TEEC_CloseSession(&gatekeeper_session);

    free(ta_buffer);
    close(fd);
    *result_out = result;

    return -1;
  }

  free(ta_buffer);
  close(fd);

  /* INFO: Find which protocol version the current device uses */
  #if GATEKEEPER_FORCE_PROTOCOL == 0
    gatekeeper_probe_protocol();
  #endif

  LOGD("GATEKEEPER initialized with protocol version %u\n", gatekeeper_protocol);

  *result_out = TEEC_SUCCESS;

  return 0;
}

static int gatekeeper_tz_open_connection(void) {
  if (gatekeeper_opened == 1) {
    LOGE("tz is opened\n");

    return -1;
  }

  #ifndef GATEKEEPER_TEST_SKIP_PROPERTY_WAIT
    int attempts = 100;
    char property_value[PROP_VALUE_MAX] = "";

    for (;;) {
      if (__system_property_get("vendor.tzts_daemon", property_value) < 1) {
        /* INFO: Property missing. Keep polling. */
      } else if (memcmp(property_value, "Ready", strlen("Ready")) == 0)
        break;

      LOGD("TZTSdaemon property is not ready. waiting.. (value: %s)", property_value);

      usleep(100000);

      if (--attempts == 0) break;
    }

    LOGD("Gatekeeper checked vendor.tzts_daemon : %s property", property_value);
  #endif

  TEEC_Result result = TEEC_InitializeContext(NULL, &gatekeeper_context);
  if (result != TEEC_SUCCESS) {
    LOGE("TEEC_InitializeContext failed: %s %x\n", get_error_text(), result);

    goto tz_failed;
  }

  if (gatekeeper_tz_open_session(&result) != 0) {
    TEEC_FinalizeContext(&gatekeeper_context);

    goto tz_failed;
  }

  gatekeeper_opened = 1;

  return 0;

  tz_failed:
    LOGE("tz open failed. res = 0x%08x, errno = %d\n", result, errno);

    int error = errno;
    if (error == 0) error = (int)result;
    if (error != 0) {
      LOGE("tz_open failed\n");

      return -1;
    }

    gatekeeper_opened = 1;

    return 0;
}

static int gatekeeper_tz_restart(void) {
  LOGD("GATEKEEPER TA restart\n");

  if ((gatekeeper_opened & 1) != 0) {
    gatekeeper_opened = 0;

    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_pwd);
    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_pwd_handle);
    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_cmd);
    TEEC_CloseSession(&gatekeeper_session);
    TEEC_FinalizeContext(&gatekeeper_context);
  } else {
    LOGE("tz is not opened\n");
  }

  return gatekeeper_tz_open_connection();
}

int gatekeeper_close(struct gatekeeper_device *device) {
  LOGD("GATEKEEPER gk_device_close\n");

  if ((gatekeeper_opened & 1) != 0) {
    gatekeeper_opened = 0;

    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_pwd);
    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_pwd_handle);
    TEEC_ReleaseSharedMemory(&gatekeeper_shmem_cmd);
    TEEC_CloseSession(&gatekeeper_session);
    TEEC_FinalizeContext(&gatekeeper_context);
  } else {
    LOGE("tz is not opened\n");
  }

  free(device);

  return 0;
}

int gatekeeper_enroll(struct gatekeeper_device *device, uint32_t uid,
                      const uint8_t *current_password_handle,
                      uint32_t current_password_handle_length,
                      const uint8_t *current_password,
                      uint32_t current_password_length,
                      const uint8_t *desired_password,
                      uint32_t desired_password_length,
                      uint8_t **enrolled_password_handle,
                      uint32_t *enrolled_password_handle_length) {
  (void) device;

  if (desired_password == NULL || desired_password_length == 0
      || enrolled_password_handle == NULL || enrolled_password_handle_length == NULL) {
    LOGE("enroll: bad parameters\n");

    return GATEKEEPER_ERROR_BAD_PARAMETERS;
  }

  *enrolled_password_handle_length = 0;
  *enrolled_password_handle = NULL;

  uint8_t *current_password_handle_copy = NULL;
  if (current_password_handle_length > 0) {
    current_password_handle_copy = (uint8_t *)malloc(current_password_handle_length);
    if (current_password_handle_copy == NULL) {
      LOGE("enroll: out of resources (%d)\n", (int)current_password_handle_length);

      return GATEKEEPER_ERROR_BAD_PARAMETERS;
    }

    memcpy(current_password_handle_copy, current_password_handle, current_password_handle_length);
  }

  uint8_t *current_password_copy = NULL;
  if (current_password_length > 0) {
    current_password_copy = (uint8_t *)malloc(current_password_length);
    if (current_password_copy == NULL) {
      LOGE("enroll: out of resources\n");

      free(current_password_handle_copy);

      return GATEKEEPER_ERROR_BAD_PARAMETERS;
    }

    memcpy(current_password_copy, current_password, current_password_length);
  }

  uint8_t *desired_password_copy = (uint8_t *)malloc(desired_password_length);
  if (desired_password_copy == NULL) {
    LOGE("enroll: out of resources\n");

    free(current_password_handle_copy);
    free(current_password_copy);

    return GATEKEEPER_ERROR_BAD_PARAMETERS;
  }

  memcpy(desired_password_copy, desired_password, desired_password_length);

  union gatekeeper_enroll_response response = { 0 };

  struct TEEC_Operation operation = {
    .param_types = GATEKEEPER_ENROLL_PARAM_TYPES,
    .params = {
      { .buffer = &response, .size = sizeof(response) },
      { .buffer = current_password_handle_copy, .size = current_password_handle_length },
      { .buffer = current_password_copy, .size = current_password_length },
      { .buffer = desired_password_copy, .size = desired_password_length },
    },
  };

  uint32_t command_id = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? GATEKEEPER_ENROLL_COMMAND_V1 : GATEKEEPER_ENROLL_COMMAND;
  uint32_t invoke_error = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? GATEKEEPER_ERROR_BAD_PARAMETERS : GATEKEEPER_ERROR_INVOKE;
  if (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) {
    response.v1.uid = uid;
    response.v1.result = 0xFFFFFFFFu;
    response.v1.token_length = GATEKEEPER_RESPONSE_LENGTH_INITIAL;
  } else {
    response.v2.uid = uid;
    response.v2.result = 0xFFFFFFFFu;
    response.v2.token_length = GATEKEEPER_RESPONSE_LENGTH_INITIAL;
  }

  uint32_t origin = 0;
  TEEC_Result invoke_result = TEEC_InvokeCommand(&gatekeeper_session, command_id, &operation, &origin);
  if (invoke_result != TEEC_SUCCESS) {
    LOGE("enroll failed: %s (0x%x)\n", get_error_text(), origin);

    if (gatekeeper_tz_restart() != 0) LOGE("tz restart failed\n");
  }

  free(current_password_handle_copy);
  free(current_password_copy);
  free(desired_password_copy);

  if (invoke_result != TEEC_SUCCESS) return invoke_error;

  uint32_t command_result = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response.v1.result : response.v2.result;
  if (command_result == GATEKEEPER_RESULT_RETRY) {
    uint32_t timeout = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response.v1.timeout : response.v2.timeout;

    LOGE("enroll: retry, timeout %d\n", (int)timeout);

    return (int)timeout;
  }

  if (command_result != 0) {
    LOGE("enroll: result %d\n", (int)command_result);

    return GATEKEEPER_ERROR_BAD_PARAMETERS;
  }

  uint32_t length = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response.v1.token_length : response.v2.token_length;
  if (length >= GATEKEEPER_MAX_TOKEN_SIZE) return GATEKEEPER_ERROR_BAD_PARAMETERS;

  uint8_t *handle = (uint8_t *)malloc(length);

  *enrolled_password_handle = handle;

  if (handle == NULL) {
    LOGE("enroll: out of resources\n");

    return GATEKEEPER_ERROR_BAD_PARAMETERS;
  }

  *enrolled_password_handle_length = length;
  memcpy(handle, (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response.v1.token : response.v2.token, length);

  return 0;
}

int gatekeeper_verify(struct gatekeeper_device *device, uint32_t uid, uint64_t challenge,
                      const uint8_t *enrolled_password_handle,
                      uint32_t enrolled_password_handle_length,
                      const uint8_t *provided_password,
                      uint32_t provided_password_length,
                      uint8_t **auth_token, uint32_t *auth_token_length,
                      uint8_t *request_reenroll) {
  (void) device;

  if (provided_password_length == 0 || enrolled_password_handle_length == 0
      || auth_token == NULL || auth_token_length == NULL || request_reenroll == NULL
      || enrolled_password_handle_length > GATEKEEPER_MAX_ENROLLED_PASSWORD_HANDLE
      || provided_password == NULL || enrolled_password_handle == NULL
      || provided_password_length >= GATEKEEPER_MAX_PROVIDED_PASSWORD) {
    LOGE("verify: bad parameters\n");

    return GATEKEEPER_ERROR_BAD_PARAMETERS;
  }

  *auth_token_length = 0;
  *auth_token = NULL;

  struct TEEC_Operation operation = {
    .param_types = GATEKEEPER_VERIFY_PARAM_TYPES,
    .params = {
      { .buffer = &gatekeeper_shmem_cmd, .size = GATEKEEPER_VERIFY_CMD_PARAM_SIZE },
      { .buffer = &gatekeeper_shmem_pwd_handle, .size = enrolled_password_handle_length },
      { .buffer = &gatekeeper_shmem_pwd, .size = provided_password_length },
    },
  };

  union gatekeeper_verify_response *response = (union gatekeeper_verify_response *)gatekeeper_shmem_cmd.buffer;

  uint32_t command_id = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? GATEKEEPER_VERIFY_COMMAND_V1 : GATEKEEPER_VERIFY_COMMAND;
  uint32_t invoke_error = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? GATEKEEPER_ERROR_BAD_PARAMETERS : GATEKEEPER_ERROR_INVOKE;
  if (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) {
    response->v1.challenge = challenge;
    response->v1.uid = uid;
    response->v1.result = 0xFFFFFFFFu;
    response->v1.token_length = GATEKEEPER_RESPONSE_LENGTH_INITIAL;
  } else {
    response->v2.challenge = challenge;
    response->v2.uid = uid;
    response->v2.result = 0xFFFFFFFFu;
    response->v2.length = GATEKEEPER_RESPONSE_LENGTH_INITIAL;
  }

  uint8_t *pwd_handle_buffer = (uint8_t *)gatekeeper_shmem_pwd_handle.buffer;
  uint8_t *pwd_buffer = (uint8_t *)gatekeeper_shmem_pwd.buffer;

  memcpy(pwd_handle_buffer, enrolled_password_handle, enrolled_password_handle_length);
  memcpy(pwd_buffer, provided_password, provided_password_length);

  uint32_t origin = 0;
  TEEC_Result invoke_result = TEEC_InvokeCommand(&gatekeeper_session, command_id, &operation, &origin);
  if (invoke_result != TEEC_SUCCESS) {
    LOGE("verify failed: %s (0x%x)\n", get_error_text(), origin);

    if (gatekeeper_tz_restart() != 0) LOGE("tz restart failed\n");
  }

  memset(pwd_handle_buffer, 0, enrolled_password_handle_length);
  memset(pwd_buffer, 0, provided_password_length);

  if (invoke_result != TEEC_SUCCESS) return invoke_error;

  uint32_t command_result = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response->v1.result : response->v2.result;
  if (command_result == GATEKEEPER_RESULT_RETRY) {
    uint32_t timeout = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1)
                         ? response->v1.timeout : response->v2.timeout;

    LOGE("verify: retry, timeout %d\n", (int)timeout);

    return (int)timeout;
  }

  if (command_result != 0) {
    LOGE("verify: result %d\n", (int)command_result);

    return GATEKEEPER_ERROR_BAD_PARAMETERS;
  }

  uint32_t length = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response->v1.token_length : response->v2.length;
  if (length >= GATEKEEPER_MAX_TOKEN_SIZE) return GATEKEEPER_ERROR_BAD_PARAMETERS;

  uint8_t *token = (uint8_t *)malloc(length);

  *auth_token = token;

  if (token == NULL) {
    LOGE("verify: out of resources\n");

    return GATEKEEPER_ERROR_BAD_PARAMETERS;
  }

  *auth_token_length = length;
  memcpy(token, (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response->v1.token : response->v2.token, length);
  *request_reenroll = (gatekeeper_protocol == GATEKEEPER_PROTOCOL_V1) ? response->v1.reenroll : response->v2.reenroll;

  return 0;
}

int gatekeeper_open(const struct gatekeeper_module *module, const char *name, struct gatekeeper_device **device) {
  (void) name;

  LOGD("GATEKEEPER gk_device_open\n");

  struct gatekeeper_device *dev = (struct gatekeeper_device *)calloc(1, sizeof(struct gatekeeper_device));
  if (dev == NULL) {
    LOGE("[%s] out of resources\n", "gk_device_open");

    return GATEKEEPER_ERROR_OUT_OF_MEMORY;
  }

  dev->tag = GATEKEEPER_DEVICE_TAG;
  dev->version = 1;
  dev->module = (void *)module;
  dev->close = gatekeeper_close;
  dev->enroll = gatekeeper_enroll;
  dev->verify = gatekeeper_verify;

  *device = dev;

  if (gatekeeper_tz_open_connection() != 0) {
    LOGE("tz_open_connection failed\n");

    free(dev);

    return GATEKEEPER_ERROR_TZ_OPEN;
  }

  LOGD("GATEKEEPER open success\n");

  return 0;
}

/* INFO: Hardware module table; single entry point of the HAL */
static struct gatekeeper_module_methods gatekeeper_methods = {
  gatekeeper_open,
};

struct gatekeeper_module HMI = {
  GATEKEEPER_MODULE_TAG,
  1,
  0x0100,
  "gatekeeper",
  "Gatekeeper TEEGRIS HAL",
  "TEEGRIS",
  &gatekeeper_methods,
  NULL,
  { 0 }
};

/* INFO: Shared-memory buffer sizes (bytes) of the TEE commands */
const uint64_t shmem_sz_cmd = 1080;
const uint64_t shmem_sz_pwd = 128;
const uint64_t shmem_sz_pwd_handle = 64;
