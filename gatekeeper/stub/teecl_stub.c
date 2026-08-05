#include <stddef.h>

#include "teec.h"

TEEC_Result TEEC_InitializeContext(const char *name, struct TEEC_Context *context) {
  (void) name; (void) context;

  return TEEC_ERROR_GENERIC;
}

void TEEC_FinalizeContext(struct TEEC_Context *context) {
  (void) context;
}

TEEC_Result TEECS_OpenSession(struct TEEC_Context *context, struct TEEC_Session *session,
                              const struct TEEC_UUID *destination, const void *ta_buffer,
                              size_t ta_size, uint32_t connection_method,
                              const void *connection_data, struct TEEC_Operation *operation,
                              uint32_t *return_origin) {
  (void) context; (void) session; (void) destination; (void) ta_buffer; (void) ta_size; (void) connection_method; (void) connection_data; (void) operation; (void) return_origin;

  return TEEC_ERROR_GENERIC;
}

TEEC_Result TEEC_CloseSession(struct TEEC_Session *session) {
  (void) session;

  return TEEC_ERROR_GENERIC;
}

TEEC_Result TEEC_AllocateSharedMemory(struct TEEC_Context *context, struct TEEC_SharedMemory *shared_memory) {
  (void) context; (void) shared_memory;

  return TEEC_ERROR_GENERIC;
}

void TEEC_ReleaseSharedMemory(struct TEEC_SharedMemory *shared_memory) {
  (void) shared_memory;
}

TEEC_Result TEEC_InvokeCommand(struct TEEC_Session *session, uint32_t command_id, struct TEEC_Operation *operation, uint32_t *return_origin) {
  (void) session; (void) command_id; (void) operation; (void) return_origin;

  return TEEC_ERROR_GENERIC;
}

const char *get_error_text(void) {
  return "stub";
}
