/* INFO: Behavioral test for the libsecnativefeature C99 reimplementation. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "../secnativefeature.h"
#include "extra/shim.h"
#include "extra/test_util.h"

static int test_failures;

static int test_check(int condition, const char *expression) {
  if (condition) {
    printf("PASS %s\n", expression);

    return 0;
  }

  printf("FAIL %s\n", expression);

  return 1;
}

#define CHECK(condition) (test_failures += test_check((condition) != 0, #condition))

static int str_equal(const char *actual, const char *expected) {
  return actual != NULL && strcmp(actual, expected) == 0;
}

#define CHECK_STR(actual, expected) CHECK(str_equal((actual), (expected)))

/* INFO: The SecNativeFeature C++ class is exported under its original
           mangled names, so the C++ API is exercised from C directly. */
void *_ZN16SecNativeFeature11getInstanceEv(void);
void _ZN16SecNativeFeatureC1Ev(void *self);
void _ZN16SecNativeFeatureC2Ev(void *self);
void _ZN16SecNativeFeatureD1Ev(void *self);
void _ZN16SecNativeFeatureD2Ev(void *self);
const char *_ZN16SecNativeFeature3getEPKc(void *self, const char *feature_name);
int _ZN16SecNativeFeature14isDebugEnabledEv(void *self);
int _ZN16SecNativeFeature16isFeatureChangedEv(void *self);
void _ZN16SecNativeFeature16_loadFeatureFileEv(void *self);
int _ZN16SecNativeFeature12_loadFeatureEPcPNSt3__13mapINS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES8_NS1_4lessIS8_EENS6_INS1_4pairIKS8_S8_EEEEEE(void *self, char *path, void *map);
int _ZN16SecNativeFeature20setLastCodeIfChangedEPcS0_(void *self, char *code, char *last_code);
int _ZN16SecNativeFeature15getEnableStatusEPKc(void *self, const char *feature_name);
int _ZN16SecNativeFeature15getEnableStatusEPKcb(void *self, const char *feature_name, int default_value);
int _ZN16SecNativeFeature15getEnableStatusEiPKc(void *self, int feature_id, const char *feature_name);
int _ZN16SecNativeFeature15getEnableStatusEiPKcb(void *self, int feature_id, const char *feature_name, int default_value);
int _ZN16SecNativeFeature10getIntegerEPKc(void *self, const char *feature_name);
int _ZN16SecNativeFeature10getIntegerEPKci(void *self, const char *feature_name, int default_value);
int _ZN16SecNativeFeature10getIntegerEiPKc(void *self, int feature_id, const char *feature_name);
int _ZN16SecNativeFeature10getIntegerEiPKci(void *self, int feature_id, const char *feature_name, int default_value);
const char *_ZN16SecNativeFeature9getStringEPKc(void *self, const char *feature_name);
const char *_ZN16SecNativeFeature9getStringEPKcPc(void *self, const char *feature_name, const char *default_value);
const char *_ZN16SecNativeFeature9getStringEiPKc(void *self, int feature_id, const char *feature_name);
const char *_ZN16SecNativeFeature9getStringEiPKcPc(void *self, int feature_id, const char *feature_name, const char *default_value);

static void test_defaults(void) {
  shim_reset();
  shim_set_property("ro.csc.countryiso_code", "US");

  /* INFO: First call creates the singleton; nothing is registered, so
           every getter falls back to its default. */
  CHECK_STR(SecNativeFeature_getString("CscFeature_Missing"), "");
  CHECK(SecNativeFeature_getString("CscFeature_Missing") != NULL);
  CHECK_STR(SecNativeFeature_getStringWithDefault("CscFeature_Missing", "fallback"), "fallback");
  CHECK(SecNativeFeature_getInteger("CscFeature_Missing") == -1);
  CHECK(SecNativeFeature_getIntegerWithDefault("CscFeature_Missing", 7) == 7);
  CHECK(SecNativeFeature_getEnableStatus("CscFeature_Missing") == 0);
  CHECK(SecNativeFeature_getEnableStatusWithDefault("CscFeature_Missing", 1) == 1);
}

static void check_plain_results(void) {
  CHECK_STR(SecNativeFeature_getString("CscFeature_Plain"), "plain");
  CHECK_STR(SecNativeFeature_getString("CscFeature_On"), "true");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Upper"), "TRUE");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Off"), "false");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Number"), "42");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Negative"), "-7");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Junk"), "12abc");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Text"), "  padded  ");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Entities"), "a&b<c>d\"e'f");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Dup"), "onetwo");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Country"), "Samsung");
  CHECK_STR(SecNativeFeature_getString("CscFeature_AfterNested"), "after");
  CHECK_STR(SecNativeFeature_getString("CscFeature_WithAttr"), "");
  CHECK_STR(SecNativeFeature_getString("Inner"), "");

  CHECK(SecNativeFeature_getEnableStatus("CscFeature_On") == 1);
  CHECK(SecNativeFeature_getEnableStatus("CscFeature_Upper") == 1);
  CHECK(SecNativeFeature_getEnableStatus("CscFeature_Off") == 0);
  CHECK(SecNativeFeature_getEnableStatusWithDefault("CscFeature_Off", 1) == 0);
  CHECK(SecNativeFeature_getEnableStatusWithDefault("CscFeature_Junk", 1) == 1);
  CHECK(SecNativeFeature_getEnableStatusWithDefault("CscFeature_Junk", 0) == 0);
  CHECK(SecNativeFeature_getEnableStatusWithDefault("CscFeature_On", 0) == 1);

  CHECK(SecNativeFeature_getInteger("CscFeature_Number") == 42);
  CHECK(SecNativeFeature_getInteger("CscFeature_Negative") == -7);
  CHECK(SecNativeFeature_getInteger("CscFeature_Junk") == 12);
  CHECK(SecNativeFeature_getInteger("CscFeature_Text") == 0);
  CHECK(SecNativeFeature_getInteger("CscFeature_Missing") == -1);
  CHECK(SecNativeFeature_getIntegerWithDefault("CscFeature_Missing", 9) == 9);
  CHECK(SecNativeFeature_getIntegerWithDefault("CscFeature_Number", 9) == 42);
}

static void test_plain_load(void) {
  shim_set_property("mdc.omc.update_version", "v1");
  shim_set_property("mdc.vendor.path", "/omc/1");
  shim_clear_files();
  shim_register_file("/omc/1/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  check_plain_results();
}

static void test_encoded_load(void) {
  char *encoded = NULL;
  size_t encoded_len = 0;

  snf_test_gzip_encode(fixture_plain, sizeof(fixture_plain) - 1, &encoded, &encoded_len);
  CHECK(memcmp(encoded, "<?xml", 5) != 0);

  shim_set_property("mdc.omc.update_version", "v2");
  shim_set_property("mdc.vendor.path", "/omc/2");
  shim_clear_files();
  shim_register_file("/omc/2/cscfeature.xml", encoded, encoded_len);

  check_plain_results();

  free(encoded);
}

static void test_empty_file(void) {
  shim_set_property("mdc.omc.update_version", "v3");
  shim_set_property("mdc.vendor.path", "/omc/3");
  shim_clear_files();
  shim_register_file("/omc/3/cscfeature.xml", "", 0);

  CHECK_STR(SecNativeFeature_getString("CscFeature_On"), "");
  CHECK(SecNativeFeature_getInteger("CscFeature_Number") == -1);
  CHECK(SecNativeFeature_getEnableStatus("CscFeature_On") == 0);
}

static void test_malformed_file(void) {
  shim_set_property("mdc.omc.update_version", "v4");
  shim_set_property("mdc.vendor.path", "/omc/4");
  shim_clear_files();
  shim_register_file("/omc/4/cscfeature.xml", fixture_malformed, sizeof(fixture_malformed) - 1);

  /* INFO: Parse stops at the truncated element. The map keeps the entries parsed before the error. */
  CHECK_STR(SecNativeFeature_getString("CscFeature_First"), "first");
  CHECK_STR(SecNativeFeature_getString("CscFeature_Broken"), "");
}

static void test_no_reload(void) {
  /* INFO: The version gate must keep the previous map. */
  shim_set_property("mdc.vendor.path", "/omc/5");
  shim_clear_files();
  shim_register_file("/omc/5/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  CHECK_STR(SecNativeFeature_getString("CscFeature_First"), "first");
  CHECK_STR(SecNativeFeature_getString("CscFeature_On"), "");
}

static void test_long_version(void) {
  /* INFO: Update versions are read into the 96-byte per-call buffer
             and cached in full. The version gate holds even when the
             file changes underneath. */
  shim_set_property("mdc.omc.update_version", "0123456789abcdefghij");
  shim_set_property("mdc.vendor.path", "/omc/4");
  shim_clear_files();
  shim_register_file("/omc/4/cscfeature.xml", fixture_malformed, sizeof(fixture_malformed) - 1);

  CHECK_STR(SecNativeFeature_getString("CscFeature_First"), "first");
  CHECK_STR(SecNativeFeature_getString("CscFeature_First"), "first");

  /* INFO: Same version, different path with other content. The map must not reload. */
  shim_set_property("mdc.vendor.path", "/omc/5");
  shim_register_file("/omc/5/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  CHECK_STR(SecNativeFeature_getString("CscFeature_First"), "first");
  CHECK_STR(SecNativeFeature_getString("CscFeature_On"), "");
}

static void test_sized_files(void) {
  char sized[2048] = { 0 };
  snf_test_build_sized(sized, sizeof(sized), 1024);
  CHECK(strlen(sized) == 1024);

  shim_set_property("mdc.omc.update_version", "v5");
  shim_set_property("mdc.vendor.path", "/omc/6");
  shim_clear_files();
  shim_register_file("/omc/6/cscfeature.xml", sized, strlen(sized));

  CHECK(strlen(SecNativeFeature_getString("A")) == 1024 - 53);
  CHECK(SecNativeFeature_getInteger("A") == 0);

  char large[4096] = { 0 };

  snf_test_build_sized(large, sizeof(large), 3000);

  shim_set_property("mdc.omc.update_version", "v6");
  shim_set_property("mdc.vendor.path", "/omc/7");
  shim_clear_files();
  shim_register_file("/omc/7/cscfeature.xml", large, strlen(large));

  /* INFO: The text spans several 1024-byte parse chunks, exercising
             the charData concatenation across chunk boundaries. */
  CHECK(strlen(SecNativeFeature_getString("A")) == 3000 - 53);
}

static void test_utf8(void) {
  shim_set_property("mdc.omc.update_version", "v7");
  shim_set_property("mdc.vendor.path", "/omc/8");
  shim_clear_files();
  shim_register_file("/omc/8/cscfeature.xml", fixture_utf8, sizeof(fixture_utf8) - 1);

  CHECK_STR(SecNativeFeature_getString("CscFeature_Unicode"), "h\u00e9llo w\u00f6rld \u65e5\u672c\u8a9e");
}

static void test_pointer_stability(void) {
  /* INFO: Getters return pointers into the map, so with no reload in
             between, repeated calls must return the same address. */
  shim_set_property("mdc.omc.update_version", "v8");
  shim_set_property("mdc.vendor.path", "/omc/1");
  shim_clear_files();
  shim_register_file("/omc/1/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  const char *first = SecNativeFeature_getString("CscFeature_On");

  CHECK_STR(first, "true");
  CHECK(SecNativeFeature_getString("CscFeature_On") == first);

  SecNativeFeature_getInteger("CscFeature_Number");

  CHECK(SecNativeFeature_getString("CscFeature_On") == first);
}

static void test_cpp_api(void) {
  /* INFO: Fresh scenario so the class getters see deterministic input */
  shim_set_property("mdc.omc.update_version", "cpp_v1");
  shim_set_property("mdc.vendor.path", "/omc/cpp1");
  shim_clear_files();
  shim_register_file("/omc/cpp1/cscfeature.xml", fixture_plain, sizeof(fixture_plain) - 1);

  void *instance = _ZN16SecNativeFeature11getInstanceEv();
  CHECK(instance != NULL);
  CHECK(instance == (void *)_ZN16SecNativeFeature9_instanceE);

  /* INFO: The first getter sees the update version change and reloads */
  CHECK_STR(_ZN16SecNativeFeature3getEPKc(instance, "CscFeature_Plain"), "plain");
  CHECK(_ZN16SecNativeFeature3getEPKc(instance, "CscFeature_Missing") == NULL);

  CHECK_STR(_ZN16SecNativeFeature9getStringEPKc(instance, "CscFeature_Plain"), "plain");
  CHECK_STR(_ZN16SecNativeFeature9getStringEPKc(instance, "CscFeature_Missing"), "");
  CHECK(_ZN16SecNativeFeature9getStringEPKc(instance, "CscFeature_Missing") != NULL);
  CHECK_STR(_ZN16SecNativeFeature9getStringEPKcPc(instance, "CscFeature_Missing", "fallback"), "fallback");
  CHECK_STR(_ZN16SecNativeFeature9getStringEPKcPc(instance, "CscFeature_Plain", "fallback"), "plain");
  CHECK_STR(_ZN16SecNativeFeature9getStringEiPKc(instance, 99, "CscFeature_Plain"), "plain");
  CHECK_STR(_ZN16SecNativeFeature9getStringEiPKcPc(instance, 99, "CscFeature_Missing", "fallback"), "fallback");

  CHECK(_ZN16SecNativeFeature10getIntegerEPKc(instance, "CscFeature_Number") == 42);
  CHECK(_ZN16SecNativeFeature10getIntegerEPKc(instance, "CscFeature_Missing") == -1);
  CHECK(_ZN16SecNativeFeature10getIntegerEPKci(instance, "CscFeature_Missing", 9) == 9);
  CHECK(_ZN16SecNativeFeature10getIntegerEiPKc(instance, 99, "CscFeature_Number") == 42);
  CHECK(_ZN16SecNativeFeature10getIntegerEiPKci(instance, 99, "CscFeature_Missing", 9) == 9);

  CHECK(_ZN16SecNativeFeature15getEnableStatusEPKc(instance, "CscFeature_On") == 1);
  CHECK(_ZN16SecNativeFeature15getEnableStatusEPKc(instance, "CscFeature_Missing") == 0);
  CHECK(_ZN16SecNativeFeature15getEnableStatusEPKcb(instance, "CscFeature_Off", 1) == 0);
  CHECK(_ZN16SecNativeFeature15getEnableStatusEPKcb(instance, "CscFeature_Missing", 1) == 1);
  CHECK(_ZN16SecNativeFeature15getEnableStatusEiPKc(instance, 99, "CscFeature_On") == 1);
  CHECK(_ZN16SecNativeFeature15getEnableStatusEiPKcb(instance, 99, "CscFeature_Missing", 1) == 1);

  /* INFO: setLastCodeIfChanged copies on change, reports stability */
  char last_code[32] = "old";

  CHECK(_ZN16SecNativeFeature20setLastCodeIfChangedEPcS0_(instance, "new", last_code) == 1);
  CHECK_STR(last_code, "new");
  CHECK(_ZN16SecNativeFeature20setLastCodeIfChangedEPcS0_(instance, "new", last_code) == 0);

  /* INFO: isFeatureChanged tracks mdc.omc.update_version without reloading */
  CHECK(_ZN16SecNativeFeature16isFeatureChangedEv(instance) == 0);

  shim_set_property("mdc.omc.update_version", "cpp_v2");

  CHECK(_ZN16SecNativeFeature16isFeatureChangedEv(instance) == 1);
  CHECK(_ZN16SecNativeFeature16isFeatureChangedEv(instance) == 0);

  /* INFO: isDebugEnabled re-reads the two properties on every call */
  shim_set_property("ro.vendor.boot.debug_level", "0x4948");
  shim_set_property("ro.vendor.product_ship", "false");

  CHECK(_ZN16SecNativeFeature14isDebugEnabledEv(instance) == 1);

  shim_set_property("ro.vendor.product_ship", "true");

  CHECK(_ZN16SecNativeFeature14isDebugEnabledEv(instance) == 0);

  shim_set_property("ro.vendor.boot.debug_level", "0x0000");
  shim_set_property("ro.vendor.product_ship", "false");

  CHECK(_ZN16SecNativeFeature14isDebugEnabledEv(instance) == 0);

  /* INFO: _loadFeature returns -1 for a missing file and 0 for a good
             one, inserting entries into the existing map */
  CHECK(_ZN16SecNativeFeature12_loadFeatureEPcPNSt3__13mapINS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES8_NS1_4lessIS8_EENS6_INS1_4pairIKS8_S8_EEEEEE(instance, "/nonexistent.xml", NULL) == -1);

  const char extra_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<FeatureSet>\n"
    "  <CscFeature_Extra>ez</CscFeature_Extra>\n"
    "</FeatureSet>\n";

  shim_register_file("/omc/cpp_extra.xml", extra_xml, sizeof(extra_xml) - 1);

  CHECK(_ZN16SecNativeFeature12_loadFeatureEPcPNSt3__13mapINS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES8_NS1_4lessIS8_EENS6_INS1_4pairIKS8_S8_EEEEEE(instance, "/omc/cpp_extra.xml", NULL) == 0);
  CHECK_STR(_ZN16SecNativeFeature3getEPKc(instance, "CscFeature_Extra"), "ez");
  CHECK_STR(_ZN16SecNativeFeature3getEPKc(instance, "CscFeature_Plain"), "plain");

  /* INFO: The ctor/dtor symbols are safe to call on the instance */
  _ZN16SecNativeFeatureC1Ev(instance);
  _ZN16SecNativeFeatureC2Ev(instance);
  _ZN16SecNativeFeatureD1Ev(instance);
  _ZN16SecNativeFeatureD2Ev(instance);
}

int main(void) {
  test_defaults();
  test_plain_load();
  test_encoded_load();
  test_empty_file();
  test_malformed_file();
  test_no_reload();
  test_long_version();
  test_sized_files();
  test_utf8();
  test_pointer_stability();
  test_cpp_api();

  if (test_failures != 0) {
    printf("%d checks failed\n", test_failures);

    return 1;
  }

  printf("OK all checks passed\n");

  return 0;
}
