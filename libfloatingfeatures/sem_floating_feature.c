/* INFO: The SemFloatingFeature C++ class, reimplemented in C */

#include "cpp_strings.h"
#include "floating_feature.h"

/* INFO: Opaque handle, as the singleton lives in the C API */
struct sem_floating_feature {
  int unused;
};

struct sem_floating_feature *_ZN18SemFloatingFeature11getInstanceEv(void) {
  static struct sem_floating_feature instance = { 0 };

  return &instance;
}

int _ZN18SemFloatingFeature12_loadFeatureEv(void *self) {
  (void) self;

  return floatingfeature_is_loaded();
}

int _ZN18SemFloatingFeature15getEnableStatusEPKc(void *self, const char *feature_name) {
  (void) self;

  return FloatingFeature_getEnableStatus(feature_name);
}

int _ZN18SemFloatingFeature15getEnableStatusEPKcb(void *self, const char *feature_name, int default_value) {
  (void) self;

  return FloatingFeature_getEnableStatusWithDefault(feature_name, default_value);
}

int _ZN18SemFloatingFeature10getIntegerEPKc(void *self, const char *feature_name) {
  (void) self;

  return FloatingFeature_getInteger(feature_name);
}

int _ZN18SemFloatingFeature10getIntegerEPKci(void *self, const char *feature_name, int default_value) {
  (void) self;

  return FloatingFeature_getIntegerWithDefault(feature_name, default_value);
}

struct std_string _ZN18SemFloatingFeature9getStringEPKc(void *self, const char *feature_name) {
  (void) self;

  struct std_string result = { 0 };
  const char *value = FloatingFeature_getString(feature_name);

  init_std_string(&result, value != NULL ? value : "");

  return result;
}

struct std_string _ZN18SemFloatingFeature9getStringEPKcPc(void *self, const char *feature_name, char *default_value) {
  (void) self;

  struct std_string result = { 0 };
  const char *value = FloatingFeature_getStringWithDefault(feature_name, default_value != NULL ? default_value : "");

  init_std_string(&result, value != NULL ? value : "");

  return result;
}

void *_ZN18SemFloatingFeatureC1Ev(void *self) {
  return self;
}

void *_ZN18SemFloatingFeatureC2Ev(void *self) {
  return self;
}

void _ZN18SemFloatingFeatureD1Ev(void *self) {
  (void) self;
}

void _ZN18SemFloatingFeatureD2Ev(void *self) {
  (void) self;
}
