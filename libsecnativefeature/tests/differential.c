/*
   INFO: Differential test for libsecnativefeature: dlopens the
           original Samsung binary and the C99 reimplementation in one
           process under the LD_PRELOAD shim (libsnfshim.so), then
           compares all six exported functions across the same scripted
           property/file scenarios
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>

#include "extra/test_util.h"

struct snf_bindings {
  int (*get_enable_status)(const char *);
  int (*get_enable_status_default)(const char *, int);
  int (*get_integer)(const char *);
  int (*get_integer_default)(const char *, int);
  const char *(*get_string)(const char *);
  const char *(*get_string_default)(const char *, const char *);
};

static void (*fn_shim_reset)(void);
static void (*fn_shim_set_property)(const char *, const char *);
static int (*fn_shim_register_file)(const char *, const char *, size_t);
static void (*fn_shim_clear_files)(void);

static int test_failures;

static int resolve_shim(void) {
  fn_shim_reset = (void (*)(void))dlsym(RTLD_DEFAULT, "shim_reset");
  fn_shim_set_property = (void (*)(const char *, const char *))dlsym(RTLD_DEFAULT, "shim_set_property");
  fn_shim_register_file = (int (*)(const char *, const char *, size_t))dlsym(RTLD_DEFAULT, "shim_register_file");
  fn_shim_clear_files = (void (*)(void))dlsym(RTLD_DEFAULT, "shim_clear_files");

  if (fn_shim_reset == NULL || fn_shim_set_property == NULL || fn_shim_register_file == NULL || fn_shim_clear_files == NULL) {
    printf("FAIL shim API resolution: %s\n", dlerror());

    return 0;
  }

  return 1;
}

static int load_bindings(const char *path, struct snf_bindings *bindings, void **handle_out) {
  void *handle = dlopen(path, RTLD_NOW);

  if (handle == NULL) {
    printf("FAIL dlopen %s: %s\n", path, dlerror());

    return 0;
  }

  bindings->get_enable_status = (int (*)(const char *))dlsym(handle, "SecNativeFeature_getEnableStatus");
  bindings->get_enable_status_default = (int (*)(const char *, int))dlsym(handle, "SecNativeFeature_getEnableStatusWithDefault");
  bindings->get_integer = (int (*)(const char *))dlsym(handle, "SecNativeFeature_getInteger");
  bindings->get_integer_default =
    (int (*)(const char *, int))dlsym(handle, "SecNativeFeature_getIntegerWithDefault");
  bindings->get_string = (const char *(*)(const char *))dlsym(handle, "SecNativeFeature_getString");
  bindings->get_string_default =
    (const char *(*)(const char *, const char *))dlsym(handle, "SecNativeFeature_getStringWithDefault");

  if (bindings->get_enable_status == NULL || bindings->get_enable_status_default == NULL ||
      bindings->get_integer == NULL || bindings->get_integer_default == NULL ||
      bindings->get_string == NULL || bindings->get_string_default == NULL) {
    printf("FAIL dlsym on %s: %s\n", path, dlerror());

    return 0;
  }

  *handle_out = handle;

  return 1;
}

static int check_int(int left, int right, const char *expr) {
  if (left != right) {
    printf("FAIL %s: original=%d ours=%d\n", expr, left, right);

    return 1;
  }

  printf("PASS %s\n", expr);

  return 0;
}

static int check_str(const char *left, const char *right, const char *expr) {
  if ((left == NULL) != (right == NULL) || (left != NULL && strcmp(left, right) != 0)) {
    printf("FAIL %s: original=\"%s\" ours=\"%s\"\n", expr, left != NULL ? left : "(null)", right != NULL ? right : "(null)");

    return 1;
  }

  printf("PASS %s\n", expr);

  return 0;
}

static const char *const feature_names[] = {
  "CscFeature_Plain",
  "CscFeature_On",
  "CscFeature_Upper",
  "CscFeature_Off",
  "CscFeature_Number",
  "CscFeature_Negative",
  "CscFeature_Junk",
  "CscFeature_Text",
  "CscFeature_Entities",
  "CscFeature_Dup",
  "CscFeature_Country",
  "CscFeature_AfterNested",
  "CscFeature_WithAttr",
  "Inner",
  "A",
  "CscFeature_Unicode",
  "CscFeature_First",
  "CscFeature_Broken",
  "CscFeature_Missing",
  NULL
};

static void compare_all(const struct snf_bindings *orig, const struct snf_bindings *ours) {
  for (size_t i = 0; feature_names[i] != NULL; i++) {
    const char *name = feature_names[i];

    test_failures += check_int(orig->get_enable_status(name), ours->get_enable_status(name), name);
    test_failures += check_int(orig->get_enable_status_default(name, 1), ours->get_enable_status_default(name, 1), name);
    test_failures += check_int(orig->get_enable_status_default(name, 0), ours->get_enable_status_default(name, 0), name);
    test_failures += check_int(orig->get_integer(name), ours->get_integer(name), name);
    test_failures += check_int(orig->get_integer_default(name, 7), ours->get_integer_default(name, 7), name);
    test_failures += check_str(orig->get_string(name), ours->get_string(name), name);
    test_failures += check_str(orig->get_string_default(name, "fallback"), ours->get_string_default(name, "fallback"), name);
  }
}

/* INFO: First call for both libraries: the singleton is created here,
         so the country is fixed for the whole run. */
static void scenario_defaults(struct snf_bindings *orig, struct snf_bindings *ours) {
  fn_shim_reset();
  fn_shim_set_property("ro.csc.countryiso_code", "US");

  compare_all(orig, ours);
}

static void scenario_plain(struct snf_bindings *orig, struct snf_bindings *ours) {
  fn_shim_set_property("mdc.omc.update_version", "v1");
  fn_shim_set_property("mdc.vendor.path", "/omc/1");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/1/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  compare_all(orig, ours);
}

static void scenario_encoded(struct snf_bindings *orig, struct snf_bindings *ours) {
  char *encoded = NULL;
  size_t encoded_len = 0;
  snf_test_gzip_encode(fixture_plain, sizeof(fixture_plain) - 1, &encoded, &encoded_len);

  fn_shim_set_property("mdc.omc.update_version", "v2");
  fn_shim_set_property("mdc.vendor.path", "/omc/2");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/2/cscfeature.xml", encoded, encoded_len);

  compare_all(orig, ours);

  free(encoded);
}

static void scenario_empty(struct snf_bindings *orig, struct snf_bindings *ours) {
  fn_shim_set_property("mdc.omc.update_version", "v3");
  fn_shim_set_property("mdc.vendor.path", "/omc/3");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/3/cscfeature.xml", "", 0);

  compare_all(orig, ours);
}

static void scenario_malformed(struct snf_bindings *orig, struct snf_bindings *ours) {
  fn_shim_set_property("mdc.omc.update_version", "v4");
  fn_shim_set_property("mdc.vendor.path", "/omc/4");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/4/cscfeature.xml", fixture_malformed, sizeof(fixture_malformed) - 1);

  compare_all(orig, ours);
}

static void scenario_no_reload(struct snf_bindings *orig, struct snf_bindings *ours) {
  /* INFO: Same update version as the malformed scenario, different
           file: both libraries must keep the previous map. */
  fn_shim_set_property("mdc.vendor.path", "/omc/5");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/5/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  compare_all(orig, ours);
}

static void scenario_long_version(struct snf_bindings *orig, struct snf_bindings *ours) {
  /* INFO: One reload, then the version gate holds even when the file changes underneath. */
  fn_shim_set_property("mdc.omc.update_version", "0123456789abcdefghij");
  fn_shim_set_property("mdc.vendor.path", "/omc/4");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/4/cscfeature.xml", fixture_malformed, sizeof(fixture_malformed) - 1);

  compare_all(orig, ours);
  compare_all(orig, ours);

  /* INFO: Same version, different path with other content: neither library may reload. */
  fn_shim_set_property("mdc.vendor.path", "/omc/5");
  fn_shim_register_file("/omc/5/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  compare_all(orig, ours);
}

static void scenario_sized(struct snf_bindings *orig, struct snf_bindings *ours) {
  char sized[2048] = { 0 };
  snf_test_build_sized(sized, sizeof(sized), 1024);

  fn_shim_set_property("mdc.omc.update_version", "v5");
  fn_shim_set_property("mdc.vendor.path", "/omc/6");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/6/cscfeature.xml", sized, strlen(sized));

  compare_all(orig, ours);

  char large[4096] = { 0 };
  snf_test_build_sized(large, sizeof(large), 3000);

  fn_shim_set_property("mdc.omc.update_version", "v6");
  fn_shim_set_property("mdc.vendor.path", "/omc/7");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/7/cscfeature.xml", large, strlen(large));

  compare_all(orig, ours);
}

static void scenario_utf8(struct snf_bindings *orig, struct snf_bindings *ours) {
  fn_shim_set_property("mdc.omc.update_version", "v7");
  fn_shim_set_property("mdc.vendor.path", "/omc/8");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/8/cscfeature.xml", fixture_utf8, sizeof(fixture_utf8) - 1);

  compare_all(orig, ours);
}

static void scenario_rewind(struct snf_bindings *orig, struct snf_bindings *ours) {
  /* INFO: Version goes back to an earlier value with its file, so both libraries must reload. */
  fn_shim_set_property("mdc.omc.update_version", "v1");
  fn_shim_set_property("mdc.vendor.path", "/omc/1");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/1/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  compare_all(orig, ours);
}

static void scenario_clear_version(struct snf_bindings *orig, struct snf_bindings *ours) {
  /* INFO: Still a change (set but empty), so both libraries reload. */
  fn_shim_set_property("mdc.omc.update_version", "");
  fn_shim_set_property("mdc.vendor.path", "/omc/9");
  fn_shim_clear_files();
  fn_shim_register_file("/omc/9/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  compare_all(orig, ours);
}

int main(void) {
  if (!resolve_shim()) return 1;

  struct snf_bindings orig = { 0 };
  void *orig_handle = NULL;
  if (!load_bindings("original.so", &orig, &orig_handle)) return 1;

  struct snf_bindings ours = { 0 };
  void *ours_handle = NULL;
  if (!load_bindings("libsecnativefeature.so", &ours, &ours_handle)) return 1;

  scenario_defaults(&orig, &ours);
  scenario_plain(&orig, &ours);
  scenario_encoded(&orig, &ours);
  scenario_empty(&orig, &ours);
  scenario_malformed(&orig, &ours);
  scenario_no_reload(&orig, &ours);
  scenario_long_version(&orig, &ours);
  scenario_sized(&orig, &ours);
  scenario_utf8(&orig, &ours);
  scenario_rewind(&orig, &ours);
  scenario_clear_version(&orig, &ours);

  if (dlsym(ours_handle, "secnativefeature_country_iso") != NULL) {
    printf("FAIL internal symbol leaked: secnativefeature_country_iso\n");
    test_failures++;
  }

  if (test_failures != 0) {
    printf("%d checks failed\n", test_failures);

    return 1;
  }

  printf("OK all checks passed\n");

  return 0;
}
