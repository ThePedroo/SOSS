/* INFO: Minimal hand-declared TEEC (Trusted Execution Environment Client) API used by the gatekeeper. */

#ifndef TEEC_H
#define TEEC_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t TEEC_Result;

#define TEEC_SUCCESS 0x00000000u
#define TEEC_ERROR_GENERIC 0xFFFF0000u
#define TEEC_ERROR_OUT_OF_MEMORY 0xFFFF000Cu

#define TEEC_MEM_INPUT 0x1u
#define TEEC_MEM_OUTPUT 0x2u

struct TEEC_Context {
  void *imp;
};

struct TEEC_Session {
  void *imp;
};

struct TEEC_SharedMemory {
  void *buffer;
  size_t size;
  uint32_t flags;
  int32_t internal[3];
};

struct TEEC_Parameter {
  void *buffer;
  size_t size;
  uint64_t tail;
};

struct TEEC_Operation {
  uint32_t started;
  uint32_t param_types;
  struct TEEC_Parameter params[4];
  uint64_t reserved;
};

struct TEEC_UUID {
  uint32_t time_low;
  uint16_t time_mid;
  uint16_t time_hi_and_version;
  uint8_t clock_seq_and_node[8];
};

TEEC_Result TEEC_InitializeContext(const char *name, struct TEEC_Context *context);

void TEEC_FinalizeContext(struct TEEC_Context *context);

TEEC_Result TEECS_OpenSession(struct TEEC_Context *context, struct TEEC_Session *session,
                              const struct TEEC_UUID *destination, const void *ta_buffer,
                              size_t ta_size, uint32_t connection_method,
                              const void *connection_data, struct TEEC_Operation *operation,
                              uint32_t *return_origin);

TEEC_Result TEEC_CloseSession(struct TEEC_Session *session);

TEEC_Result TEEC_AllocateSharedMemory(struct TEEC_Context *context,
                                      struct TEEC_SharedMemory *shared_memory);

void TEEC_ReleaseSharedMemory(struct TEEC_SharedMemory *shared_memory);

TEEC_Result TEEC_InvokeCommand(struct TEEC_Session *session, uint32_t command_id,
                               struct TEEC_Operation *operation, uint32_t *return_origin);

const char *get_error_text(void);

#endif /* TEEC_H */
