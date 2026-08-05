/* INFO: Differential test: replays scripted TEE scenarios against the
         original gatekeeper.s5e8825.so binaries (v1 and v2 protocols)
         and the reimplementation, requiring identical results and
         byte-identical recorded TEE call streams. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <stdint.h>

#include "../gatekeeper.h"
#include "extra/shim.h"
#include "extra/test_util.h"

#define DIFFERENTIAL_VERIFY_PARAM_TYPES 0x0DDFu
#define DIFFERENTIAL_SHMEM_CMD_SIZE 1080u
#define DIFFERENTIAL_SHMEM_PWD_HANDLE_SIZE 64u
#define DIFFERENTIAL_SHMEM_PWD_SIZE 128u
#define DIFFERENTIAL_CMD_TAIL_SIZE 56u
#define DIFFERENTIAL_PROBE_PASSWORD 0x01u
#define DIFFERENTIAL_PROBE_GROUP_LENGTH 5u
#define DIFFERENTIAL_REOPEN_GROUP_LENGTH 6u
#define DIFFERENTIAL_READ_PAYLOAD 32u
#define DIFFERENTIAL_MAX_GROUPS 2u

/* INFO: Scripted TEE behavior and record stream, bound to the shim */
static void (*shim_reset_fn)(void);
static void (*shim_set_scenario_fn)(const struct shim_scenario *scenario);
static uint32_t (*shim_record_count_fn)(void);
static const struct shim_record *(*shim_records_fn)(void);

static void differential_bind_shim(void) {
  shim_reset_fn = (void (*)(void))dlsym(RTLD_DEFAULT, "shim_reset");
  shim_set_scenario_fn = (void (*)(const struct shim_scenario *))dlsym(RTLD_DEFAULT, "shim_set_scenario");
  shim_record_count_fn = (uint32_t (*)(void))dlsym(RTLD_DEFAULT, "shim_record_count");
  shim_records_fn = (const struct shim_record *(*)(void))dlsym(RTLD_DEFAULT, "shim_records");
  if (shim_reset_fn == NULL || shim_set_scenario_fn == NULL || shim_record_count_fn == NULL || shim_records_fn == NULL) {
    fprintf(stderr, "shim control symbols missing (preload not active?): %s\n", dlerror());
    exit(1);
  }
}

static void *differential_symbol(void *handle, const char *name) {
  void *symbol = dlsym(handle, name);
  if (symbol == NULL) {
    fprintf(stderr, "missing symbol %s: %s\n", name, dlerror());
    exit(1);
  }

  return symbol;
}

static const struct gatekeeper_module *differential_load(const char *path, void **handle_out) {
  void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (handle == NULL) {
    fprintf(stderr, "dlopen %s: %s\n", path, dlerror());
    exit(1);
  }

  *handle_out = handle;

  return (const struct gatekeeper_module *)differential_symbol(handle, "HMI");
}

static uint64_t differential_constant(void *handle, const char *name) {
  return *(const uint64_t *)differential_symbol(handle, name);
}

static void differential_check_module(const struct gatekeeper_module *module) {
  CHECK(module->tag == GATEKEEPER_MODULE_TAG);
  CHECK(module->module_api_version == 1);
  CHECK(module->hal_api_version == 0x0100);
  CHECK(strcmp(module->id, "gatekeeper") == 0);
  CHECK(strcmp(module->name, "Gatekeeper TEEGRIS HAL") == 0);
  CHECK(strcmp(module->author, "TEEGRIS") == 0);
  CHECK(module->methods != NULL);
}

static void differential_check_library(const char *label, void *handle, const struct gatekeeper_module *module) {
  printf("== %s ==\n", label);
  CHECK(differential_constant(handle, "shmem_sz_cmd") == 1080);
  CHECK(differential_constant(handle, "shmem_sz_pwd") == 128);
  CHECK(differential_constant(handle, "shmem_sz_pwd_handle") == 64);
  differential_check_module(module);
}

/* INFO: Outcome of one scenario run against one library */
struct differential_result {
  int open_rc;
  uint32_t device_tag;
  uint32_t device_version;
  int enroll_rc;
  uint32_t enroll_length;
  uint8_t enroll_handle[64];
  int verify_rc;
  uint32_t verify_length;
  uint8_t verify_token[128];
  uint8_t verify_reenroll;
  uint32_t record_count;
  const struct shim_record *records;
};

/* INFO: One differential scenario; empty pointers mean "not supplied",
           which drives the bad-parameter paths. */
struct differential_case {
  const char *name;
  struct shim_scenario scenario;
  uint32_t uid;
  uint64_t challenge;
  const uint8_t *current_password_handle;
  uint32_t current_password_handle_length;
  const uint8_t *current_password;
  uint32_t current_password_length;
  const uint8_t *desired_password;
  uint32_t desired_password_length;
  const uint8_t *enrolled_password_handle;
  uint32_t enrolled_password_handle_length;
  const uint8_t *provided_password;
  uint32_t provided_password_length;
  int do_enroll;
  int do_verify;
  int expect_open_rc;
  int expect_enroll_rc;
  int expect_verify_rc;
};

static void differential_default_scenario(struct shim_scenario *scenario) {
  memset(scenario, 0, sizeof(struct shim_scenario));
  scenario->protocol = SHIM_PROTOCOL_V2;
  scenario->open_session_result = TEEC_SUCCESS;
  scenario->ta_size = 0x60;
  scenario->invoke_result = TEEC_SUCCESS;
}

static void differential_pattern(uint8_t *buffer, uint32_t length, uint8_t start) {
  for (uint32_t i = 0; i < length; i++) buffer[i] = (uint8_t)(start + i);
}

/* INFO: Replays one scenario; the caller provides the record storage */
static void differential_run_case(const struct gatekeeper_module *module, const struct differential_case *tc,
                                  struct differential_result *result, struct shim_record *records) {
  memset(result, 0, sizeof(struct differential_result));
  errno = 0;
  shim_reset_fn();
  shim_set_scenario_fn(&tc->scenario);

  struct gatekeeper_device *device = NULL;
  result->open_rc = module->methods->open(module, "gatekeeper", &device);

  uint8_t *handle = NULL;
  uint32_t handle_length = 0;
  uint8_t *token = NULL;
  uint32_t token_length = 0;
  uint8_t reenroll = 0;

  /* INFO: The HALs assign *device before opening the TEE and free it
           without clearing it on failure; only use it on success. */
  if (result->open_rc == 0 && device != NULL) {
    result->device_tag = device->tag;
    result->device_version = device->version;

    if (tc->do_enroll != 0) {
      result->enroll_rc = device->enroll(device, tc->uid, tc->current_password_handle,
                                         tc->current_password_handle_length,
                                         tc->current_password, tc->current_password_length,
                                         tc->desired_password, tc->desired_password_length,
                                         &handle, &handle_length);

      if (handle != NULL) {
        result->enroll_length = handle_length;
        if (handle_length > sizeof(result->enroll_handle)) {
          handle_length = sizeof(result->enroll_handle);
        }

        memcpy(result->enroll_handle, handle, handle_length);
      }
    }

    if (tc->do_verify != 0) {
      result->verify_rc = device->verify(device, tc->uid, tc->challenge,
                                         tc->enrolled_password_handle,
                                         tc->enrolled_password_handle_length,
                                         tc->provided_password, tc->provided_password_length,
                                         &token, &token_length, &reenroll);

      if (token != NULL) {
        result->verify_length = token_length;

        if (token_length > sizeof(result->verify_token)) {
          token_length = sizeof(result->verify_token);
        }
        memcpy(result->verify_token, token, token_length);
      }
    }

    result->verify_reenroll = reenroll;
    device->close(device);
  }

  free(handle);
  free(token);

  result->record_count = shim_record_count_fn();
  memcpy(records, shim_records_fn(), sizeof(struct shim_record) * result->record_count);

  result->records = records;
}

static void differential_check_results(const struct differential_result *original, const struct differential_result *ours) {
  CHECK(ours->open_rc == original->open_rc);
  CHECK(ours->device_tag == original->device_tag);
  CHECK(ours->device_version == original->device_version);
  CHECK(ours->enroll_rc == original->enroll_rc);
  CHECK(ours->enroll_length == original->enroll_length);
  CHECK(memcmp(ours->enroll_handle, original->enroll_handle, sizeof(original->enroll_handle)) == 0);
  CHECK(ours->verify_rc == original->verify_rc);
  CHECK(ours->verify_length == original->verify_length);
  CHECK(memcmp(ours->verify_token, original->verify_token, sizeof(original->verify_token)) == 0);
  CHECK(ours->verify_reenroll == original->verify_reenroll);
}

static void differential_check_expected(const struct differential_result *result, const struct differential_case *tc) {
  CHECK(result->open_rc == tc->expect_open_rc);

  if (tc->do_enroll != 0) {
    CHECK(result->enroll_rc == tc->expect_enroll_rc);

    if (tc->expect_enroll_rc == 0) {
      CHECK(result->enroll_length == tc->scenario.enroll_token_length);
      CHECK(memcmp(result->enroll_handle, tc->scenario.enroll_token,
                   tc->scenario.enroll_token_length) == 0);
    }
  }

  if (tc->do_verify != 0) {
    CHECK(result->verify_rc == tc->expect_verify_rc);
    if (tc->expect_verify_rc == 0) {
      CHECK(result->verify_length == tc->scenario.verify_token_length);
      CHECK(memcmp(result->verify_token, tc->scenario.verify_token, tc->scenario.verify_token_length) == 0);
      CHECK(result->verify_reenroll == tc->scenario.verify_reenroll);
    }
  }
}

/* INFO: Compares two record streams byte for byte.  Log records are
         not part of the parity contract (the reimplementation logs
         less than the original HALs), so both sides skip them before
         comparing; everything else must still match exactly. */
static void differential_check_streams(const struct shim_record *a, uint32_t a_count,
                                       const struct shim_record *b, uint32_t b_count) {
  uint32_t a_index = 0;
  uint32_t b_index = 0;
  uint32_t compared = 0;

  for (;;) {
    while (a_index < a_count && a[a_index].kind == SHIM_KIND_LOG) a_index++;
    while (b_index < b_count && b[b_index].kind == SHIM_KIND_LOG) b_index++;

    if (a_index >= a_count && b_index >= b_count) {
      printf("PASS record stream identical (%u records)\n", (unsigned)compared);

      return;
    }

    if (a_index >= a_count || b_index >= b_count) {
      printf("FAIL record count: original %u ours %u (after skipping logs)\n", (unsigned)a_count, (unsigned)b_count);

      test_failures++;

      return;
    }

    if (memcmp(&a[a_index], &b[b_index], sizeof(struct shim_record)) != 0) {
      const struct shim_record *left = &a[a_index];
      const struct shim_record *right = &b[b_index];
      uint32_t first_byte = 0;
      uint32_t found = 0;

      for (uint32_t j = 0; j < sizeof(left->payload); j++) {
        if (left->payload[j] == right->payload[j]) continue;

        first_byte = j;
        found = 1;

        break;
      }

      printf("FAIL record %u: original {kind=%u a=%u b=%u c=%u len=%u} ours {kind=%u a=%u b=%u c=%u len=%u}%s\n",
             (unsigned)compared, (unsigned)left->kind, (unsigned)left->a, (unsigned)left->b,
             (unsigned)left->c, (unsigned)left->length, (unsigned)right->kind, (unsigned)right->a,
             (unsigned)right->b, (unsigned)right->c, (unsigned)right->length,
             found ? " payload differs" : "");

      if (found) printf("  first differing payload byte %u: original 0x%02x ours 0x%02x\n",
                        (unsigned)first_byte, (unsigned)left->payload[first_byte],
                        (unsigned)right->payload[first_byte]);

      test_failures++;

      return;
    }

    a_index++;
    b_index++;
    compared++;
  }
}

/* INFO: The original fstats twice on session open, ours once (proposital). */
static void differential_check_records(const struct differential_result *original, const struct differential_result *ours) {
  differential_check_streams(original->records, original->record_count, ours->records, ours->record_count);
}

/* INFO: Builds the deterministic record group the shim produces for
         one protocol probe: INVOKE, PARAM0, TAIL0, PARAM1, PARAM2.
         The invoke result and the envelope bytes depend on whether the
         probed command belongs to the emulated trustlet generation. */
static void differential_probe_group(uint32_t command_id, uint32_t invoke_result, struct shim_record *group) {
  memset(group, 0, sizeof(struct shim_record) * DIFFERENTIAL_PROBE_GROUP_LENGTH);

  group[0].kind = SHIM_KIND_INVOKE;
  group[0].a = command_id;
  group[0].b = DIFFERENTIAL_VERIFY_PARAM_TYPES;
  group[0].c = invoke_result;

  group[1].kind = SHIM_KIND_INVOKE_PARAM;
  group[1].a = command_id;
  group[1].b = 0;
  group[1].c = DIFFERENTIAL_SHMEM_CMD_SIZE;
  group[1].length = SHIM_RECORD_PAYLOAD;

  group[2].kind = SHIM_KIND_INVOKE_TAIL;
  group[2].a = command_id;
  group[2].b = 0;
  group[2].c = DIFFERENTIAL_CMD_TAIL_SIZE;
  group[2].length = DIFFERENTIAL_CMD_TAIL_SIZE;

  group[3].kind = SHIM_KIND_INVOKE_PARAM;
  group[3].a = command_id;
  group[3].b = 1;
  group[3].c = DIFFERENTIAL_SHMEM_PWD_HANDLE_SIZE;
  group[3].length = DIFFERENTIAL_SHMEM_PWD_HANDLE_SIZE;
  group[3].payload[0] = DIFFERENTIAL_PROBE_PASSWORD;

  group[4].kind = SHIM_KIND_INVOKE_PARAM;
  group[4].a = command_id;
  group[4].b = 2;
  group[4].c = DIFFERENTIAL_SHMEM_PWD_SIZE;
  group[4].length = DIFFERENTIAL_SHMEM_PWD_SIZE;
  group[4].payload[0] = DIFFERENTIAL_PROBE_PASSWORD;

  uint32_t result_code = 0xFFFFFFFFu;
  uint32_t length = 1024u;

  if (command_id == 126) {
    memcpy(group[2].payload + 8, &length, sizeof(length));
    memcpy(group[2].payload + 13, &result_code, sizeof(result_code));
  } else {
    memcpy(group[1].payload + 12, &result_code, sizeof(result_code));
    memcpy(group[1].payload + 21, &length, sizeof(length));
  }
}

/* INFO: Builds the record block the probe emits when it re-opens the
         trustlet session between probe attempts (a v1 trustlet rejected
         command 126, so the session is torn down and command 1 must run
         on a fresh one): SESSION_CLOSE, then the file-open sequence of
         every session open (OPEN, FSTAT, READ, CLOSE_FD, SESSION_OPEN). */
static void differential_reopen_group(uint32_t ta_size, uint32_t open_result, struct shim_record *out) {
  memset(out, 0, sizeof(struct shim_record) * DIFFERENTIAL_REOPEN_GROUP_LENGTH);

  out[0].kind = SHIM_KIND_SESSION_CLOSE;
  out[0].a = TEEC_SUCCESS;

  out[1].kind = SHIM_KIND_OPEN;
  out[1].a = 0;
  out[1].c = SHIM_FAKE_FD;
  out[1].length = (uint16_t)(strlen(SHIM_TA_PATH) + 1);
  memcpy(out[1].payload, SHIM_TA_PATH, out[1].length);

  out[2].kind = SHIM_KIND_FSTAT;
  out[2].a = SHIM_FAKE_FD;
  out[2].c = ta_size;

  out[3].kind = SHIM_KIND_READ;
  out[3].a = SHIM_FAKE_FD;
  out[3].b = ta_size;
  out[3].c = ta_size;
  uint32_t read_len = ta_size < DIFFERENTIAL_READ_PAYLOAD ? ta_size : DIFFERENTIAL_READ_PAYLOAD;
  out[3].length = (uint16_t)read_len;
  for (uint32_t i = 0; i < read_len; i++) out[3].payload[i] = (uint8_t)(0xA0u + i);

  out[4].kind = SHIM_KIND_CLOSE_FD;
  out[4].a = SHIM_FAKE_FD;

  out[5].kind = SHIM_KIND_SESSION_OPEN;
  out[5].a = open_result;
}

/* INFO: Builds the expected auto-detecting stream: the pinned build's
           stream with the probe record groups spliced in after the close
           of the trustlet file (the record that follows the third
           shared-memory allocation of every open, where the probe runs;
           the trustlet restart path opens again). A reopen block is
           spliced between probe groups because the probe re-opens the
           session after a rejected command. */
static void differential_build_expected(const struct differential_result *forced, const struct shim_record *groups,
                                        uint32_t group_count, uint32_t ta_size, uint32_t open_result,
                                        struct shim_record *out, uint32_t *length_out) {
  uint32_t allocates = 0;
  uint32_t written = 0;
  uint32_t pending = 0;

  for (uint32_t i = 0; i < forced->record_count; i++) {
    out[written++] = forced->records[i];

    if (pending != 0) {
      for (uint32_t g = 0; g < group_count; g++) {
        memcpy(&out[written], &groups[g * DIFFERENTIAL_PROBE_GROUP_LENGTH], sizeof(struct shim_record) * DIFFERENTIAL_PROBE_GROUP_LENGTH);
        written += DIFFERENTIAL_PROBE_GROUP_LENGTH;

        if (g + 1 < group_count) {
          differential_reopen_group(ta_size, open_result, &out[written]);
          written += DIFFERENTIAL_REOPEN_GROUP_LENGTH;
        }
      }
      pending = 0;
    }

    if (forced->records[i].kind == SHIM_KIND_ALLOCATE) {
      allocates++;

      if (allocates % 3 == 0) pending = 1;
    }
  }

  *length_out = written;
}

/* INFO: Verifies the auto-detecting build: same return values and
         outputs as the pinned build, and its record stream is the
         pinned build's stream with the probe groups spliced in. */
static void differential_check_auto(const struct gatekeeper_module *auto_module, const struct differential_case *tc,
                                    const struct differential_result *forced, const struct shim_record *groups,
                                    uint32_t group_count, struct differential_result *auto_result,
                                    struct shim_record *records) {
  differential_run_case(auto_module, tc, auto_result, records);

  printf("  auto vs pinned results\n");
  differential_check_results(forced, auto_result);

  static struct shim_record expected[SHIM_RECORD_MAX];
  uint32_t expected_count;
  differential_build_expected(forced, groups, group_count, tc->scenario.ta_size,
                              tc->scenario.open_session_result, expected, &expected_count);
  printf("  auto vs pinned stream + %u probe group(s)\n", group_count);
  differential_check_streams(expected, expected_count, auto_result->records, auto_result->record_count);
}

static void differential_setup_cases(struct differential_case *cases, uint32_t protocol) {
  /* open_close: exercises the full trustlet open and close sequence */
  cases[0].name = "open_close";
  cases[0].expect_open_rc = 0;

  /* enroll_success: plain enroll of a new password */
  cases[1].name = "enroll_success";
  cases[1].uid = 1000;
  cases[1].desired_password = (const uint8_t *)"1234";
  cases[1].desired_password_length = 4;
  cases[1].do_enroll = 1;
  cases[1].expect_open_rc = 0;
  cases[1].expect_enroll_rc = 0;
  cases[1].scenario.enroll_token_length = 32;
  differential_pattern(cases[1].scenario.enroll_token, 32, 0x10);

  /* enroll_with_handle_and_password: all three input buffers in use */
  cases[2].name = "enroll_with_handle_and_password";
  cases[2].uid = 1000;
  cases[2].current_password_handle = (const uint8_t *)"hand1";
  cases[2].current_password_handle_length = 5;
  cases[2].current_password = (const uint8_t *)"oldpw";
  cases[2].current_password_length = 5;
  cases[2].desired_password = (const uint8_t *)"newpw";
  cases[2].desired_password_length = 5;
  cases[2].do_enroll = 1;
  cases[2].expect_open_rc = 0;
  cases[2].expect_enroll_rc = 0;
  cases[2].scenario.enroll_token_length = 32;
  differential_pattern(cases[2].scenario.enroll_token, 32, 0x11);

  /* enroll_retry: the trustlet asks the caller to wait */
  cases[3].name = "enroll_retry";
  cases[3].uid = 1000;
  cases[3].desired_password = (const uint8_t *)"1234";
  cases[3].desired_password_length = 4;
  cases[3].do_enroll = 1;
  cases[3].expect_open_rc = 0;
  cases[3].expect_enroll_rc = 1500;
  cases[3].scenario.enroll_result = 2;
  cases[3].scenario.enroll_timeout = 1500;

  /* enroll_bad_parameters: no desired password */
  cases[4].name = "enroll_bad_parameters";
  cases[4].uid = 1000;
  cases[4].do_enroll = 1;
  cases[4].expect_open_rc = 0;
  cases[4].expect_enroll_rc = GATEKEEPER_ERROR_BAD_PARAMETERS;

  /* enroll_invoke_failure: the TEE call itself fails */
  cases[5].name = "enroll_invoke_failure";
  cases[5].uid = 1000;
  cases[5].desired_password = (const uint8_t *)"1234";
  cases[5].desired_password_length = 4;
  cases[5].do_enroll = 1;
  cases[5].expect_open_rc = 0;
  cases[5].scenario.invoke_result = TEEC_ERROR_GENERIC;

  /* verify_success: challenge and reenroll echo from the trustlet */
  cases[6].name = "verify_success";
  cases[6].uid = 42;
  cases[6].challenge = 0x1122334455667788ULL;
  cases[6].enrolled_password_handle = (const uint8_t *)"hndl01";
  cases[6].enrolled_password_handle_length = 6;
  cases[6].provided_password = (const uint8_t *)"secret";
  cases[6].provided_password_length = 6;
  cases[6].do_verify = 1;
  cases[6].expect_open_rc = 0;
  cases[6].expect_verify_rc = 0;
  cases[6].scenario.verify_token_length = 32;
  cases[6].scenario.verify_reenroll = 1;
  differential_pattern(cases[6].scenario.verify_token, 32, 0x20);

  /* verify_retry: the trustlet asks the caller to wait */
  cases[7].name = "verify_retry";
  cases[7].uid = 42;
  cases[7].challenge = 0x1122334455667788ULL;
  cases[7].enrolled_password_handle = (const uint8_t *)"hndl01";
  cases[7].enrolled_password_handle_length = 6;
  cases[7].provided_password = (const uint8_t *)"secret";
  cases[7].provided_password_length = 6;
  cases[7].do_verify = 1;
  cases[7].expect_open_rc = 0;
  cases[7].expect_verify_rc = 2500;
  cases[7].scenario.verify_result = 2;
  cases[7].scenario.verify_timeout = 2500;

  /* verify_bad_parameters: no provided password */
  cases[8].name = "verify_bad_parameters";
  cases[8].uid = 42;
  cases[8].challenge = 0x1122334455667788ULL;
  cases[8].do_verify = 1;
  cases[8].expect_open_rc = 0;
  cases[8].expect_verify_rc = GATEKEEPER_ERROR_BAD_PARAMETERS;

  /* verify_invoke_failure: the TEE call itself fails */
  cases[9].name = "verify_invoke_failure";
  cases[9].uid = 42;
  cases[9].challenge = 0x1122334455667788ULL;
  cases[9].enrolled_password_handle = (const uint8_t *)"hndl01";
  cases[9].enrolled_password_handle_length = 6;
  cases[9].provided_password = (const uint8_t *)"secret";
  cases[9].provided_password_length = 6;
  cases[9].do_verify = 1;
  cases[9].expect_open_rc = 0;
  cases[9].scenario.invoke_result = TEEC_ERROR_GENERIC;

  /* enroll_then_verify: both operations on one session */
  cases[10].name = "enroll_then_verify";
  cases[10].uid = 7;
  cases[10].challenge = 0x8877665544332211ULL;
  cases[10].desired_password = (const uint8_t *)"1234";
  cases[10].desired_password_length = 4;
  cases[10].enrolled_password_handle = (const uint8_t *)"hndl01";
  cases[10].enrolled_password_handle_length = 6;
  cases[10].provided_password = (const uint8_t *)"secret";
  cases[10].provided_password_length = 6;
  cases[10].do_enroll = 1;
  cases[10].do_verify = 1;
  cases[10].expect_open_rc = 0;
  cases[10].expect_enroll_rc = 0;
  cases[10].expect_verify_rc = 0;
  cases[10].scenario.enroll_token_length = 32;
  differential_pattern(cases[10].scenario.enroll_token, 32, 0x30);
  cases[10].scenario.verify_token_length = 32;
  cases[10].scenario.verify_reenroll = 1;
  differential_pattern(cases[10].scenario.verify_token, 32, 0x40);

  /* open_failure: the trustlet session cannot be opened */
  cases[11].name = "open_failure";
  cases[11].expect_open_rc = GATEKEEPER_ERROR_TZ_OPEN;
  cases[11].scenario.open_session_result = TEEC_ERROR_GENERIC;

  if (protocol == SHIM_PROTOCOL_V1) {
    /* INFO: v1 HALs report invoke failures as bad parameters */
    cases[5].expect_enroll_rc = GATEKEEPER_ERROR_BAD_PARAMETERS;
    cases[9].expect_verify_rc = GATEKEEPER_ERROR_BAD_PARAMETERS;
  } else {
    cases[5].expect_enroll_rc = GATEKEEPER_ERROR_INVOKE;
    cases[9].expect_verify_rc = GATEKEEPER_ERROR_INVOKE;
  }
}

int main(void) {
  static struct shim_record original_records[SHIM_RECORD_MAX];
  static struct shim_record ours_records[SHIM_RECORD_MAX];
  static struct shim_record auto_records[SHIM_RECORD_MAX];
  static struct shim_record probe_groups[DIFFERENTIAL_PROBE_GROUP_LENGTH * DIFFERENTIAL_MAX_GROUPS];

  setvbuf(stdout, NULL, _IONBF, 0);

  differential_bind_shim();

  void *new_original_handle;
  const struct gatekeeper_module *new_original_module = differential_load("./gatekeeper.s5e8825.so", &new_original_handle);
  void *old_original_handle;
  const struct gatekeeper_module *old_original_module = differential_load("./gatekeeper_old.so", &old_original_handle);
  void *auto_handle;
  const struct gatekeeper_module *auto_module = differential_load("./libgatekeeper.so", &auto_handle);
  void *forced_v2_handle;
  const struct gatekeeper_module *forced_v2_module = differential_load("./libgatekeeper_v2.so", &forced_v2_handle);
  void *forced_v1_handle;
  const struct gatekeeper_module *forced_v1_module = differential_load("./libgatekeeper_v1.so", &forced_v1_handle);

  differential_check_library("new original", new_original_handle, new_original_module);
  differential_check_library("old original", old_original_handle, old_original_module);
  differential_check_library("ours (auto)", auto_handle, auto_module);
  differential_check_library("ours (v2 pinned)", forced_v2_handle, forced_v2_module);
  differential_check_library("ours (v1 pinned)", forced_v1_handle, forced_v1_module);

  for (uint32_t mode = 0; mode < 2; mode++) {
    const struct gatekeeper_module *original_module;
    const struct gatekeeper_module *forced_module;
    struct shim_record *groups = probe_groups;
    uint32_t group_count;

    if (mode == 0) {
      original_module = new_original_module;
      forced_module = forced_v2_module;
      differential_probe_group(126, TEEC_SUCCESS, &groups[0]);
      group_count = 1;

      printf("===== protocol v2 =====\n");
    } else {
      original_module = old_original_module;
      forced_module = forced_v1_module;
      differential_probe_group(126, TEEC_ERROR_GENERIC, &groups[0]);
      differential_probe_group(1, TEEC_SUCCESS, &groups[DIFFERENTIAL_PROBE_GROUP_LENGTH]);
      group_count = 2;

      printf("===== protocol v1 =====\n");
    }

    struct differential_case cases[12];

    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      memset(&cases[i], 0, sizeof(cases[i]));
      differential_default_scenario(&cases[i].scenario);

      if (mode == 1) cases[i].scenario.protocol = SHIM_PROTOCOL_V1;
    }

    differential_setup_cases(cases, cases[0].scenario.protocol);

    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      printf("--- %s ---\n", cases[i].name);

      struct differential_result original_result;
      struct differential_result ours_result;
      struct differential_result auto_result;

      differential_run_case(original_module, &cases[i], &original_result, original_records);
      differential_run_case(forced_module, &cases[i], &ours_result, ours_records);

      differential_check_results(&original_result, &ours_result);
      differential_check_expected(&original_result, &cases[i]);
      differential_check_records(&original_result, &ours_result);

      differential_check_auto(auto_module, &cases[i], &ours_result, groups, group_count,
                              &auto_result, auto_records);
    }
  }

  if (test_failures == 0) printf("OK all differential checks passed\n");

  return test_failures == 0 ? 0 : 1;
}
