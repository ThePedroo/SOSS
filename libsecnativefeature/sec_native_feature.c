/* INFO: The SecNativeFeature C++ class, reimplemented in C */

#include <string.h>

#include "secnativefeature.h"

void *_ZN16SecNativeFeature11getInstanceEv(void) {
  return (void *)secnativefeature_get_instance();
}

void _ZN16SecNativeFeatureC1Ev(void *self) {
  (void) self;
}

void _ZN16SecNativeFeatureC2Ev(void *self) {
  (void) self;
}

void _ZN16SecNativeFeatureD1Ev(void *self) {
  (void) self;
}

void _ZN16SecNativeFeatureD2Ev(void *self) {
  (void) self;
}

const char *_ZN16SecNativeFeature3getEPKc(void *self, const char *feature_name) {
  (void) self;

  return secnativefeature_get(secnativefeature_get_instance(), feature_name);
}

int _ZN16SecNativeFeature14isDebugEnabledEv(void *self) {
  (void) self;

  return secnativefeature_is_debug_enabled();
}

int _ZN16SecNativeFeature16isFeatureChangedEv(void *self) {
  (void) self;

  return secnativefeature_is_feature_changed();
}

void _ZN16SecNativeFeature16_loadFeatureFileEv(void *self) {
  (void) self;

  secnativefeature_load_feature_file(secnativefeature_get_instance());
}

/* INFO: Parses the file into the C singleton's map */
int _ZN16SecNativeFeature12_loadFeatureEPcPNSt3__13mapINS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES8_NS1_4lessIS8_EENS6_INS1_4pairIKS8_S8_EEEEEE(void *self, char *path, void *map) {
  (void) self; (void) map;

  return secnativefeature_load_feature(path, secnativefeature_get_instance());
}

/* INFO: Copies code into last_code when they differ and reports whether
           they changed (the original uses an unbounded sprintf copy). */
int _ZN16SecNativeFeature20setLastCodeIfChangedEPcS0_(void *self, char *code, char *last_code) {
  (void) self;

  if (strcmp(code, last_code) == 0) return 0;

  strcpy(last_code, code);

  return 1;
}

int _ZN16SecNativeFeature15getEnableStatusEPKc(void *self, const char *feature_name) {
  (void) self;

  return SecNativeFeature_getEnableStatus(feature_name);
}

int _ZN16SecNativeFeature15getEnableStatusEPKcb(void *self, const char *feature_name, int default_value) {
  (void) self;

  return SecNativeFeature_getEnableStatusWithDefault(feature_name, default_value);
}

int _ZN16SecNativeFeature15getEnableStatusEiPKc(void *self, int feature_id, const char *feature_name) {
  (void) self; (void) feature_id;

  return SecNativeFeature_getEnableStatus(feature_name);
}

int _ZN16SecNativeFeature15getEnableStatusEiPKcb(void *self, int feature_id, const char *feature_name, int default_value) {
  (void) self; (void) feature_id;

  return SecNativeFeature_getEnableStatusWithDefault(feature_name, default_value);
}

int _ZN16SecNativeFeature10getIntegerEPKc(void *self, const char *feature_name) {
  (void) self;

  return SecNativeFeature_getInteger(feature_name);
}

int _ZN16SecNativeFeature10getIntegerEPKci(void *self, const char *feature_name, int default_value) {
  (void) self;

  return SecNativeFeature_getIntegerWithDefault(feature_name, default_value);
}

int _ZN16SecNativeFeature10getIntegerEiPKc(void *self, int feature_id, const char *feature_name) {
  (void) self; (void) feature_id;

  return SecNativeFeature_getInteger(feature_name);
}

int _ZN16SecNativeFeature10getIntegerEiPKci(void *self, int feature_id, const char *feature_name, int default_value) {
  (void) self; (void) feature_id;

  return SecNativeFeature_getIntegerWithDefault(feature_name, default_value);
}

const char *_ZN16SecNativeFeature9getStringEPKc(void *self, const char *feature_name) {
  (void) self;

  return SecNativeFeature_getString(feature_name);
}

const char *_ZN16SecNativeFeature9getStringEPKcPc(void *self, const char *feature_name, const char *default_value) {
  (void) self;

  return SecNativeFeature_getStringWithDefault(feature_name, default_value);
}

const char *_ZN16SecNativeFeature9getStringEiPKc(void *self, int feature_id, const char *feature_name) {
  (void) self; (void) feature_id;

  return SecNativeFeature_getString(feature_name);
}

const char *_ZN16SecNativeFeature9getStringEiPKcPc(void *self, int feature_id, const char *feature_name, const char *default_value) {
  (void) self; (void) feature_id;

  return SecNativeFeature_getStringWithDefault(feature_name, default_value);
}
