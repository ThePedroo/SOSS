/* INFO: implementation of functions to analyze behavior */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "shim.h"
#include "../../teec.h"

/* INFO: Fortify wrappers the original binary imports; declared here
           because the NDK headers only expose them under _FORTIFY_SOURCE. */
int __open_2(const char *path, int flags);
ssize_t __read_chk(int fd, void *buffer, size_t count, size_t buffer_length);
int __android_log_print(int priority, const char *tag, const char *format, ...);

/* INFO: Recorded call stream and scripted TEE behavior */
static struct shim_record records[SHIM_RECORD_MAX];
static uint32_t record_count;
static struct shim_scenario scenario;

/* INFO: Real libc entry points, resolved once through RTLD_NEXT */
static int (*real_open)(const char *path, int flags, ...);
static int (*real_open_2)(const char *path, int flags);
static ssize_t (*real_read)(int fd, void *buffer, size_t count);
static ssize_t (*real_read_chk)(int fd, void *buffer, size_t count, size_t buffer_length);
static int (*real_fstat)(int fd, struct stat *status);
static int (*real_close)(int fd);

static void shim_forward_resolve(void) {
  if (real_open != NULL) return;

  real_open = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
  real_open_2 = (int (*)(const char *, int))dlsym(RTLD_NEXT, "__open_2");
  real_read = (ssize_t (*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
  real_read_chk = (ssize_t (*)(int, void *, size_t, size_t))dlsym(RTLD_NEXT, "__read_chk");
  real_fstat = (int (*)(int, struct stat *))dlsym(RTLD_NEXT, "fstat");
  real_close = (int (*)(int))dlsym(RTLD_NEXT, "close");
}

/* INFO: Appends one call record; long payloads are truncated */
static void shim_record(uint32_t kind, uint32_t a, uint32_t b, uint32_t c,
                        const void *payload, uint16_t length) {
  struct shim_record *record;

  if (record_count >= SHIM_RECORD_MAX) return;

  record = &records[record_count++];
  record->kind = kind;
  record->a = a;
  record->b = b;
  record->c = c;
  record->length = length;

  if (length > SHIM_RECORD_PAYLOAD) length = SHIM_RECORD_PAYLOAD;

  if (payload != NULL) memcpy(record->payload, payload, length);
}

void shim_reset(void) {
  memset(records, 0, sizeof(records));
  record_count = 0;
  memset(&scenario, 0, sizeof(scenario));
}

void shim_set_scenario(const struct shim_scenario *new_scenario) {
  scenario = *new_scenario;
  if (scenario.protocol != SHIM_PROTOCOL_V1) scenario.protocol = SHIM_PROTOCOL_V2;
}

uint32_t shim_record_count(void) {
  return record_count;
}

const struct shim_record *shim_records(void) {
  return records;
}

/* INFO: Fake TA file: the trustlet blob is a deterministic byte pattern */
static int shim_open_fake(const char *path, int flags) {
  shim_record(SHIM_KIND_OPEN, (uint32_t)flags, 0, SHIM_FAKE_FD, path,
              (uint16_t)(strlen(path) + 1));

  return SHIM_FAKE_FD;
}

static ssize_t shim_read_fake(void *buffer, size_t count) {
  uint8_t *out = (uint8_t *)buffer;
  uint32_t length = scenario.ta_size < count ? scenario.ta_size : (uint32_t)count;
  uint32_t i;

  for (i = 0; i < length; i++) out[i] = (uint8_t)(0xA0 + i);

  shim_record(SHIM_KIND_READ, SHIM_FAKE_FD, (uint32_t)count, length, out,
              (uint16_t)(length < 32 ? length : 32));

  return (ssize_t)length;
}

/* INFO: Interposed libc entry points; only the trustlet open is faked */
int open(const char *path, int flags, ...) {
  shim_forward_resolve();

  if (strcmp(path, SHIM_TA_PATH) == 0) return shim_open_fake(path, flags);

  va_list args;

  va_start(args, flags);
  unsigned int mode = va_arg(args, unsigned int);
  va_end(args);

  return real_open(path, flags, mode);
}

int __open_2(const char *path, int flags) {
  shim_forward_resolve();

  if (strcmp(path, SHIM_TA_PATH) == 0) return shim_open_fake(path, flags);

  return real_open_2(path, flags);
}

ssize_t read(int fd, void *buffer, size_t count) {
  shim_forward_resolve();

  if (fd == SHIM_FAKE_FD) return shim_read_fake(buffer, count);

  return real_read(fd, buffer, count);
}

ssize_t __read_chk(int fd, void *buffer, size_t count, size_t buffer_length) {
  (void) buffer_length;

  shim_forward_resolve();

  if (fd == SHIM_FAKE_FD) return shim_read_fake(buffer, count);

  return real_read_chk(fd, buffer, count, buffer_length);
}

int fstat(int fd, struct stat *status) {
  shim_forward_resolve();

  if (fd == SHIM_FAKE_FD) {
    memset(status, 0, sizeof(struct stat));
    status->st_size = (off_t)scenario.ta_size;
    shim_record(SHIM_KIND_FSTAT, (uint32_t)fd, 0, scenario.ta_size, NULL, 0);

    return 0;
  }

  return real_fstat(fd, status);
}

int close(int fd) {
  shim_forward_resolve();

  if (fd == SHIM_FAKE_FD) {
    shim_record(SHIM_KIND_CLOSE_FD, (uint32_t)fd, 0, 0, NULL, 0);

    return 0;
  }

  return real_close(fd);
}

/* INFO: Interposed property read: reports the TEE daemon as ready */
int __system_property_get(const char *name, char *value) {
  shim_record(SHIM_KIND_PROP_GET, 0, 0, 1, name, (uint16_t)(strlen(name) + 1));
  memcpy(value, "Ready", 6);

  return 1;
}

/* INFO: Interposed logger: captures the formatted message for diffing */
int __android_log_print(int priority, const char *tag, const char *format, ...) {
  (void) tag;

  char buffer[SHIM_RECORD_PAYLOAD];
  va_list args;

  va_start(args, format);
  int written = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (written < 0) written = 0;

  shim_record(SHIM_KIND_LOG, (uint32_t)priority, 0, (uint32_t)written, buffer,
              (uint16_t)(written > (int)(SHIM_RECORD_PAYLOAD - 1) ? SHIM_RECORD_PAYLOAD - 1 : written));

  return written;
}

/* INFO: TEE client entry points; the scripted trustlet response is
         written at the exact offsets the gatekeeper HALs use. */
TEEC_Result TEEC_InitializeContext(const char *name, struct TEEC_Context *context) {
  (void) name;

  int saved_errno = errno;

  context->imp = (void *)0x1;
  shim_record(SHIM_KIND_CTX_INIT, TEEC_SUCCESS, 0, 0, NULL, 0);
  errno = saved_errno;

  return TEEC_SUCCESS;
}

void TEEC_FinalizeContext(struct TEEC_Context *context) {
  (void) context;

  int saved_errno = errno;

  shim_record(SHIM_KIND_CTX_FINALIZE, TEEC_SUCCESS, 0, 0, NULL, 0);
  errno = saved_errno;
}

TEEC_Result TEECS_OpenSession(struct TEEC_Context *context, struct TEEC_Session *session,
                              const struct TEEC_UUID *destination, const void *ta_buffer,
                              size_t ta_size, uint32_t connection_method,
                              const void *connection_data, struct TEEC_Operation *operation,
                              uint32_t *return_origin) {
  (void) context; (void) destination; (void) ta_buffer; (void) ta_size; (void) connection_method; (void) connection_data; (void) operation;

  int saved_errno = errno;

  session->imp = (void *)0x2;

  if (return_origin != NULL) *return_origin = 0;

  shim_record(SHIM_KIND_SESSION_OPEN, scenario.open_session_result, 0, 0, NULL, 0);
  errno = saved_errno;

  return scenario.open_session_result;
}

TEEC_Result TEEC_CloseSession(struct TEEC_Session *session) {
  (void) session;

  int saved_errno = errno;

  shim_record(SHIM_KIND_SESSION_CLOSE, TEEC_SUCCESS, 0, 0, NULL, 0);
  errno = saved_errno;

  return TEEC_SUCCESS;
}

TEEC_Result TEEC_AllocateSharedMemory(struct TEEC_Context *context,
                                      struct TEEC_SharedMemory *shared_memory) {
  (void) context;

  int saved_errno = errno;

  /* INFO: calloc keeps the unused buffer area deterministic */
  shared_memory->buffer = calloc(1, shared_memory->size);
  shim_record(SHIM_KIND_ALLOCATE, TEEC_SUCCESS, (uint32_t)shared_memory->size,
              shared_memory->flags, NULL, 0);
  errno = saved_errno;

  return TEEC_SUCCESS;
}

void TEEC_ReleaseSharedMemory(struct TEEC_SharedMemory *shared_memory) {
  int saved_errno = errno;

  shim_record(SHIM_KIND_RELEASE, 0, (uint32_t)shared_memory->size, 0, NULL, 0);
  free(shared_memory->buffer);
  shared_memory->buffer = NULL;
  errno = saved_errno;
}

const char *get_error_text(void) {
  return "SHIM TEE error";
}

/* INFO: Writes the scripted trustlet response: command 63 (enroll) and
         command 126 (verify) answer in the v2 layout (header at
         offset 1024), command 0 (enroll) and command 1 (verify) in
         the v1 layout (fields at the start of the buffer); the offsets
         match the HALs. */
static void shim_script_response(uint32_t command_id, struct TEEC_Operation *operation) {
  if (command_id == 63) {
    uint8_t *response = (uint8_t *)operation->params[0].buffer;
    uint64_t header = ((uint64_t)scenario.enroll_result << 32) | scenario.enroll_token_length;

    memcpy(response + SHIM_TAIL_OFFSET, &header, sizeof(header));

    if (scenario.enroll_result == 2)
      memcpy(response + SHIM_TAIL_OFFSET + 8, &scenario.enroll_timeout, sizeof(scenario.enroll_timeout));
    else
      memcpy(response, scenario.enroll_token, scenario.enroll_token_length);
  } else if (command_id == 126) {
    struct TEEC_SharedMemory *command_shm = (struct TEEC_SharedMemory *)operation->params[0].buffer;
    uint8_t *command = (uint8_t *)command_shm->buffer;

    memcpy(command + SHIM_TAIL_OFFSET + 8, &scenario.verify_token_length, sizeof(scenario.verify_token_length));
    command[SHIM_TAIL_OFFSET + 12] = scenario.verify_reenroll;
    memcpy(command + SHIM_TAIL_OFFSET + 13, &scenario.verify_result, sizeof(scenario.verify_result));

    if (scenario.verify_result == 2)
      memcpy(command + SHIM_TAIL_OFFSET + 17, &scenario.verify_timeout, sizeof(scenario.verify_timeout));
    else
      memcpy(command, scenario.verify_token, scenario.verify_token_length);
  } else if (command_id == 0) {
    uint8_t *response = (uint8_t *)operation->params[0].buffer;
    uint32_t result = scenario.enroll_result;
    uint32_t token_length = scenario.enroll_token_length;

    memcpy(response + 4, &result, sizeof(result));
    memcpy(response + 8, &token_length, sizeof(token_length));

    if (result == 2) {
      memcpy(response + 12, &scenario.enroll_timeout, sizeof(scenario.enroll_timeout));
    } else {
      memcpy(response + 16, scenario.enroll_token, scenario.enroll_token_length);
    }
  } else if (command_id == 1) {
    struct TEEC_SharedMemory *command_shm = (struct TEEC_SharedMemory *)operation->params[0].buffer;
    uint8_t *command = (uint8_t *)command_shm->buffer;
    uint32_t result = scenario.verify_result;
    uint32_t token_length = scenario.verify_token_length;

    memcpy(command + 12, &result, sizeof(result));

    if (result == 2) {
      memcpy(command + 16, &scenario.verify_timeout, sizeof(scenario.verify_timeout));
    } else {
      command[20] = scenario.verify_reenroll;
      memcpy(command + 21, &token_length, sizeof(token_length));
      memcpy(command + 25, scenario.verify_token, scenario.verify_token_length);
    }
  }
}

/* INFO: Verify command 126 or 1 with one-byte credentials and a zero challenge and uid */
static uint32_t shim_probe_kind(uint32_t command_id, struct TEEC_Operation *operation) {
  if (operation->params[1].size != SHIM_PROBE_HANDLE_SIZE || operation->params[2].size != SHIM_PROBE_PWD_SIZE) return 0;

  uint64_t challenge = 0;
  uint32_t uid = 0;

  if (command_id == 126) {
    struct TEEC_SharedMemory *command_shm = (struct TEEC_SharedMemory *)operation->params[0].buffer;
    const uint8_t *command = (const uint8_t *)command_shm->buffer;

    memcpy(&challenge, command + SHIM_TAIL_OFFSET, sizeof(challenge));
    memcpy(&uid, command + SHIM_TAIL_OFFSET + 21, sizeof(uid));

    if (challenge == 0 && uid == 0) return SHIM_PROTOCOL_V2;
  } else if (command_id == 1) {
    struct TEEC_SharedMemory *command_shm = (struct TEEC_SharedMemory *)operation->params[0].buffer;
    const uint8_t *command = (const uint8_t *)command_shm->buffer;

    memcpy(&challenge, command, sizeof(challenge));
    memcpy(&uid, command + 8, sizeof(uid));

    if (challenge == 0 && uid == 0) return SHIM_PROTOCOL_V1;
  }

  return 0;
}

/* INFO: Commands the emulated trustlet generation accepts */
static int shim_command_known(uint32_t command_id) {
  if (scenario.protocol == SHIM_PROTOCOL_V1)
    return command_id == 0 || command_id == 1;

  return command_id == 63 || command_id == 126;
}

/* INFO: A failed verify answer, so the probe sees a written result
           code but never a success.  The v2 envelope writes into
           the header at 1024, the v1 envelope into the fields at
           the start of the buffer. */
static void shim_probe_response_v2(struct TEEC_Operation *operation) {
  struct TEEC_SharedMemory *command_shm = (struct TEEC_SharedMemory *)operation->params[0].buffer;
  uint8_t *command = (uint8_t *)command_shm->buffer;
  uint32_t result_code = 0xFFFFFFFFu;

  memcpy(command + SHIM_TAIL_OFFSET + 13, &result_code, sizeof(result_code));
}

static void shim_probe_response_v1(struct TEEC_Operation *operation) {
  struct TEEC_SharedMemory *command_shm = (struct TEEC_SharedMemory *)operation->params[0].buffer;
  uint8_t *command = (uint8_t *)command_shm->buffer;
  uint32_t result_code = 0xFFFFFFFFu;

  memcpy(command + 12, &result_code, sizeof(result_code));
}

static uint16_t shim_cap(uint32_t length) {
  return (uint16_t)(length < SHIM_RECORD_PAYLOAD ? length : SHIM_RECORD_PAYLOAD);
}

static void shim_record_param(uint32_t command_id, uint32_t index, struct TEEC_Operation *operation) {
  const uint8_t *base = NULL;
  uint32_t size = 0;

  if (command_id == 0 || command_id == 63) {
    base = (const uint8_t *)operation->params[index].buffer;
    size = (uint32_t)operation->params[index].size;
  } else if ((command_id == 1 || command_id == 126) && index < 3) {
    struct TEEC_SharedMemory *shm = (struct TEEC_SharedMemory *)operation->params[index].buffer;

    base = (const uint8_t *)shm->buffer;
    size = (uint32_t)shm->size;
  } else return;

  shim_record(SHIM_KIND_INVOKE_PARAM, command_id, index, size, base, shim_cap(size));

  if (size > SHIM_TAIL_OFFSET) {
    shim_record(SHIM_KIND_INVOKE_TAIL, command_id, index, size - SHIM_TAIL_OFFSET,
                base + SHIM_TAIL_OFFSET, shim_cap(size - SHIM_TAIL_OFFSET));
  }
}

TEEC_Result TEEC_InvokeCommand(struct TEEC_Session *session, uint32_t command_id,
                               struct TEEC_Operation *operation, uint32_t *return_origin) {
  (void) session;

  int saved_errno = errno;

  if (return_origin != NULL) *return_origin = 0;

  uint32_t probe = shim_probe_kind(command_id, operation);
  TEEC_Result result;

  if (probe != 0) {
    if (probe == scenario.protocol) {
      result = TEEC_SUCCESS;

      if (probe == SHIM_PROTOCOL_V1) shim_probe_response_v1(operation);
      else shim_probe_response_v2(operation);
    } else {
      result = TEEC_ERROR_GENERIC;
    }
  } else if (shim_command_known(command_id)) {
    result = scenario.invoke_result;
    if (result == TEEC_SUCCESS) shim_script_response(command_id, operation);
  } else {
    result = TEEC_ERROR_GENERIC;
  }

  shim_record(SHIM_KIND_INVOKE, command_id, operation->param_types, result, NULL, 0);

  for (uint32_t index = 0; index < 4; index++) shim_record_param(command_id, index, operation);

  errno = saved_errno;

  return result;
}
