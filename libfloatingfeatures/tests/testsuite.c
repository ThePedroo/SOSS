/* INFO: Behavioral test for the libfloatingfeature C99 reimplementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "floating_feature.h"
#include "cpp_strings.h"

#ifndef FLOATING_FEATURE_XML_PATH
  #define FLOATING_FEATURE_XML_PATH "/vendor/etc/floating_feature.xml"
#endif

#define LONG_VALUE_LENGTH 3000

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

/* INFO: The SemFloatingFeature C++ class is exported under its original
         mangled names, so the C++ API is exercised from C directly. */
void *_ZN18SemFloatingFeature11getInstanceEv(void);
int _ZN18SemFloatingFeature12_loadFeatureEv(void *self);

int _ZN18SemFloatingFeature15getEnableStatusEPKc(void *self, const char *feature_name);
int _ZN18SemFloatingFeature15getEnableStatusEPKcb(void *self, const char *feature_name, int default_value);
int _ZN18SemFloatingFeature10getIntegerEPKc(void *self, const char *feature_name);
int _ZN18SemFloatingFeature10getIntegerEPKci(void *self, const char *feature_name, int default_value);
struct std_string _ZN18SemFloatingFeature9getStringEPKc(void *self, const char *feature_name);
struct std_string _ZN18SemFloatingFeature9getStringEPKcPc(void *self, const char *feature_name, char *default_value);

/* INFO: Compare a std::string to the expected C string. */
static int check_std_string(const struct std_string *string, const char *expected) {
  const char *data = read_std_string(string);

  return data != NULL && strcmp(data, expected) == 0 &&
         get_std_string_length(string) == strlen(expected);
}

#ifndef NO_LOAD_TEST
static int write_test_xml(void) {
  FILE *file = fopen(FLOATING_FEATURE_XML_PATH, "w");
  if (file == NULL) return -1;

  fputs("<SecFloatingFeatureSet>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_BOOL_TRUE>true</SEC_FLOATING_FEATURE_BOOL_TRUE>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_BOOL_CAPS>TRUE</SEC_FLOATING_FEATURE_BOOL_CAPS>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_BOOL_MIXED>True</SEC_FLOATING_FEATURE_BOOL_MIXED>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_BOOL_OFF>false</SEC_FLOATING_FEATURE_BOOL_OFF>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_INT>42</SEC_FLOATING_FEATURE_INT>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_INT_NEG>-7</SEC_FLOATING_FEATURE_INT_NEG>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_INT_TEXT>abc</SEC_FLOATING_FEATURE_INT_TEXT>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_STRING>hello_world</SEC_FLOATING_FEATURE_STRING>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_DUP>first</SEC_FLOATING_FEATURE_DUP>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_DUP>second</SEC_FLOATING_FEATURE_DUP>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_ATTR name=\"ignored\">attr_value</SEC_FLOATING_FEATURE_ATTR>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_NESTED>\n", file);
  fputs("    <SEC_FLOATING_FEATURE_CHILD>child_value</SEC_FLOATING_FEATURE_CHILD>\n", file);
  fputs("  </SEC_FLOATING_FEATURE_NESTED>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_WS>  padded  </SEC_FLOATING_FEATURE_WS>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_EMPTY/>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_WS_ONLY>\n  </SEC_FLOATING_FEATURE_WS_ONLY>\n", file);
  fputs("  <SEC_FLOATING_FEATURE_LONG>", file);
  for (size_t index = 0; index < LONG_VALUE_LENGTH; index += 1) fputc('a', file);
  fputs("</SEC_FLOATING_FEATURE_LONG>\n", file);
  fputs("</SecFloatingFeatureSet>\n", file);

  fclose(file);

  return 0;
}

static void run_loaded_checks(void) {
  CHECK(FloatingFeature_getEnableStatus("SEC_FLOATING_FEATURE_BOOL_TRUE") == 1);
  CHECK(FloatingFeature_getEnableStatus("SEC_FLOATING_FEATURE_BOOL_CAPS") == 1);
  CHECK(FloatingFeature_getEnableStatus("SEC_FLOATING_FEATURE_BOOL_MIXED") == 0);
  CHECK(FloatingFeature_getEnableStatus("SEC_FLOATING_FEATURE_BOOL_OFF") == 0);
  CHECK(FloatingFeature_getEnableStatus("SEC_FLOATING_FEATURE_MISSING") == 0);

  CHECK(FloatingFeature_getEnableStatusWithDefault("SEC_FLOATING_FEATURE_BOOL_TRUE", 0) == 1);
  CHECK(FloatingFeature_getEnableStatusWithDefault("SEC_FLOATING_FEATURE_BOOL_CAPS", 0) == 1);
  CHECK(FloatingFeature_getEnableStatusWithDefault("SEC_FLOATING_FEATURE_BOOL_OFF", 1) == 1);
  CHECK(FloatingFeature_getEnableStatusWithDefault("SEC_FLOATING_FEATURE_MISSING", 1) == 1);
  CHECK(FloatingFeature_getEnableStatusWithDefault("SEC_FLOATING_FEATURE_MISSING", 0) == 0);

  CHECK(FloatingFeature_getInteger("SEC_FLOATING_FEATURE_INT") == 42);
  CHECK(FloatingFeature_getInteger("SEC_FLOATING_FEATURE_INT_NEG") == -7);
  CHECK(FloatingFeature_getInteger("SEC_FLOATING_FEATURE_INT_TEXT") == 0);
  CHECK(FloatingFeature_getInteger("SEC_FLOATING_FEATURE_MISSING") == -1);
  CHECK(FloatingFeature_getIntegerWithDefault("SEC_FLOATING_FEATURE_INT", 7) == 42);
  CHECK(FloatingFeature_getIntegerWithDefault("SEC_FLOATING_FEATURE_MISSING", 7) == 7);

  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_STRING"), "hello_world") == 0);
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_MISSING"), "TRUE") == 0);
  CHECK(strcmp(FloatingFeature_getStringWithDefault("SEC_FLOATING_FEATURE_STRING", "fallback"), "hello_world") == 0);
  CHECK(strcmp(FloatingFeature_getStringWithDefault("SEC_FLOATING_FEATURE_MISSING", "fallback"), "fallback") == 0);

  /* INFO: Duplicated element names concatenate their character data */
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_DUP"), "firstsecond") == 0);

  /* INFO: Attributes are ignored, the element name is the key */
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_ATTR"), "attr_value") == 0);
  CHECK(strcmp(FloatingFeature_getString("ignored"), "TRUE") == 0);

  /* INFO: Elements containing only child elements have no value */
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_NESTED"), "TRUE") == 0);
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_CHILD"), "child_value") == 0);

  /* INFO: The root element has no value */
  CHECK(strcmp(FloatingFeature_getString("SecFloatingFeatureSet"), "TRUE") == 0);

  /* INFO: Surrounding whitespace is trimmed from the text content */
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_WS"), "padded") == 0);

  /* INFO: Empty and whitespace-only elements have no value */
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_EMPTY"), "TRUE") == 0);
  CHECK(strcmp(FloatingFeature_getString("SEC_FLOATING_FEATURE_WS_ONLY"), "TRUE") == 0);

  /* INFO: The full long value survives parsing */
  const char *long_value = FloatingFeature_getString("SEC_FLOATING_FEATURE_LONG");
  CHECK(strlen(long_value) == (size_t)LONG_VALUE_LENGTH);
  int all_a = 1;
  for (size_t index = 0; index < (size_t)LONG_VALUE_LENGTH; index += 1) {
    if (long_value[index] != 'a') all_a = 0;
  }
  CHECK(all_a != 0);
}

static void run_loaded_cpp_checks(void) {
  void *instance = _ZN18SemFloatingFeature11getInstanceEv();

  if (instance == NULL) {
    CHECK(instance != NULL);

    return;
  }

  /* INFO: With the loaded feature file, _loadFeature reports success */
  CHECK(_ZN18SemFloatingFeature12_loadFeatureEv(instance) == 1);

  CHECK(_ZN18SemFloatingFeature15getEnableStatusEPKc(instance, "SEC_FLOATING_FEATURE_BOOL_TRUE") == 1);
  CHECK(_ZN18SemFloatingFeature15getEnableStatusEPKc(instance, "SEC_FLOATING_FEATURE_BOOL_OFF") == 0);
  CHECK(_ZN18SemFloatingFeature15getEnableStatusEPKcb(instance, "SEC_FLOATING_FEATURE_MISSING", 1) == 1);
  CHECK(_ZN18SemFloatingFeature15getEnableStatusEPKcb(instance, "SEC_FLOATING_FEATURE_BOOL_TRUE", 0) == 1);

  CHECK(_ZN18SemFloatingFeature10getIntegerEPKc(instance, "SEC_FLOATING_FEATURE_INT") == 42);
  CHECK(_ZN18SemFloatingFeature10getIntegerEPKc(instance, "SEC_FLOATING_FEATURE_INT_NEG") == -7);
  CHECK(_ZN18SemFloatingFeature10getIntegerEPKc(instance, "SEC_FLOATING_FEATURE_MISSING") == -1);
  CHECK(_ZN18SemFloatingFeature10getIntegerEPKci(instance, "SEC_FLOATING_FEATURE_MISSING", 7) == 7);

  struct std_string str = _ZN18SemFloatingFeature9getStringEPKc(instance, "SEC_FLOATING_FEATURE_STRING");
  CHECK(check_std_string(&str, "hello_world") != 0);

  str = _ZN18SemFloatingFeature9getStringEPKc(instance, "SEC_FLOATING_FEATURE_MISSING");
  CHECK(check_std_string(&str, "TRUE") != 0);

  str = _ZN18SemFloatingFeature9getStringEPKcPc(instance, "SEC_FLOATING_FEATURE_MISSING", "fallback");
  CHECK(check_std_string(&str, "fallback") != 0);

  str = _ZN18SemFloatingFeature9getStringEPKcPc(instance, "SEC_FLOATING_FEATURE_STRING", "fallback");
  CHECK(check_std_string(&str, "hello_world") != 0);
}
#endif

#ifdef NO_LOAD_TEST
  static void run_noload_checks(void) {
    CHECK(FloatingFeature_getEnableStatus("ANY_FEATURE") == 0);
    CHECK(FloatingFeature_getEnableStatusWithDefault("ANY_FEATURE", 1) == 1);
    CHECK(FloatingFeature_getInteger("ANY_FEATURE") == -1);
    CHECK(FloatingFeature_getIntegerWithDefault("ANY_FEATURE", 5) == 5);
    CHECK(strcmp(FloatingFeature_getString("ANY_FEATURE"), "TRUE") == 0);
    CHECK(strcmp(FloatingFeature_getStringWithDefault("ANY_FEATURE", "fallback"), "fallback") == 0);
  }

  static void run_noload_cpp_checks(void) {
    void *instance = _ZN18SemFloatingFeature11getInstanceEv();

    if (instance == NULL) {
      CHECK(instance != NULL);

      return;
    }

    /* INFO: With no feature file, _loadFeature honestly reports failure */
    CHECK(_ZN18SemFloatingFeature12_loadFeatureEv(instance) == 0);

    CHECK(_ZN18SemFloatingFeature15getEnableStatusEPKc(instance, "ANY_FEATURE") == 0);
    CHECK(_ZN18SemFloatingFeature15getEnableStatusEPKcb(instance, "ANY_FEATURE", 1) == 1);
    CHECK(_ZN18SemFloatingFeature10getIntegerEPKc(instance, "ANY_FEATURE") == -1);
    CHECK(_ZN18SemFloatingFeature10getIntegerEPKci(instance, "ANY_FEATURE", 5) == 5);

    struct std_string str = _ZN18SemFloatingFeature9getStringEPKc(instance, "ANY_FEATURE");
    CHECK(check_std_string(&str, "TRUE") != 0);
  }
#endif

int main(void) {
  #ifndef NO_LOAD_TEST
    if (write_test_xml() != 0) {
      printf("FAIL cannot write %s\n", FLOATING_FEATURE_XML_PATH);

      return 1;
    }
    run_loaded_checks();
    run_loaded_cpp_checks();
  #else
    run_noload_checks();
    run_noload_cpp_checks();
  #endif

  if (test_failures == 0) {
    printf("OK all checks passed\n");

    return 0;
  }

  printf("FAILED %d check(s)\n", test_failures);

  return 1;
}
