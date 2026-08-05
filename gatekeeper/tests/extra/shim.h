/* INFO: Contract between the TEE shim (shim.c) and the differential
         harness (tests/differential.c): scripted behavior and the
         recorded call stream. */

#ifndef SHIM_H
#define SHIM_H

#include <stdint.h>

#define SHIM_TA_PATH "/vendor/tee/00000000-0000-0000-0000-474154454b45"
#define SHIM_FAKE_FD 4096
#define SHIM_RECORD_PAYLOAD 128
#define SHIM_RECORD_MAX 512
#define SHIM_TOKEN_MAX 64
#define SHIM_HEAD_OFFSET 0u
#define SHIM_TAIL_OFFSET 1024u

/* INFO: Realistic sizes to not be denied, but within shared buf limit */
#define SHIM_PROBE_HANDLE_SIZE 64u
#define SHIM_PROBE_PWD_SIZE 16u

#define SHIM_PROTOCOL_V1 1u
#define SHIM_PROTOCOL_V2 2u

/* INFO: Recorded call kinds; a and b and c hold kind-specific fields */
enum shim_call_kind {
  SHIM_KIND_CTX_INIT,
  SHIM_KIND_CTX_FINALIZE,
  SHIM_KIND_SESSION_OPEN,
  SHIM_KIND_SESSION_CLOSE,
  SHIM_KIND_ALLOCATE,
  SHIM_KIND_RELEASE,
  SHIM_KIND_INVOKE,
  SHIM_KIND_INVOKE_PARAM,
  SHIM_KIND_INVOKE_TAIL,
  SHIM_KIND_OPEN,
  SHIM_KIND_FSTAT,
  SHIM_KIND_READ,
  SHIM_KIND_CLOSE_FD,
  SHIM_KIND_PROP_GET,
  SHIM_KIND_LOG
};

struct shim_record {
  uint32_t kind;
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint16_t length;
  uint16_t reserved;
  uint8_t payload[SHIM_RECORD_PAYLOAD];
};

struct shim_scenario {
  uint32_t protocol;
  uint32_t open_session_result;
  uint32_t ta_size;
  uint32_t invoke_result;
  uint32_t enroll_result;
  uint32_t enroll_timeout;
  uint32_t enroll_token_length;
  uint8_t enroll_token[SHIM_TOKEN_MAX];
  uint32_t verify_result;
  uint32_t verify_timeout;
  uint8_t verify_reenroll;
  uint32_t verify_token_length;
  uint8_t verify_token[SHIM_TOKEN_MAX];
};

void shim_reset(void);

void shim_set_scenario(const struct shim_scenario *scenario);

uint32_t shim_record_count(void);

const struct shim_record *shim_records(void);

#endif /* SHIM_H */
