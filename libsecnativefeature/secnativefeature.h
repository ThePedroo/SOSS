#ifndef SECNATIVEFEATURE_H
#define SECNATIVEFEATURE_H

int SecNativeFeature_getEnableStatus(const char *feature_name);

int SecNativeFeature_getEnableStatusWithDefault(const char *feature_name, int default_value);

int SecNativeFeature_getInteger(const char *feature_name);

int SecNativeFeature_getIntegerWithDefault(const char *feature_name, int default_value);

const char *SecNativeFeature_getString(const char *feature_name);

const char *SecNativeFeature_getStringWithDefault(const char *feature_name, const char *default_value);

/* INFO: Internal helper (hidden by the version script) */
struct secnativefeature;

struct secnativefeature *secnativefeature_get_instance(void);

const char *secnativefeature_get(struct secnativefeature *instance, const char *feature_name);

int secnativefeature_load_feature(const char *path, struct secnativefeature *instance);

void secnativefeature_load_feature_file(struct secnativefeature *instance);

int secnativefeature_is_debug_enabled(void);

int secnativefeature_is_feature_changed(void);

/* INFO: The lazy singleton pointer, exported under the class's own mangled name (its Itanium data member). */
extern struct secnativefeature *_ZN16SecNativeFeature9_instanceE;

#endif /* SECNATIVEFEATURE_H */
