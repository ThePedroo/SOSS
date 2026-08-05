#ifndef FLOATING_FEATURE_H
#define FLOATING_FEATURE_H

int FloatingFeature_getEnableStatus(const char *featureName);

int FloatingFeature_getEnableStatusWithDefault(const char *featureName, int defaultValue);

int FloatingFeature_getInteger(const char *featureName);

int FloatingFeature_getIntegerWithDefault(const char *featureName, int defaultValue);

char *FloatingFeature_getString(const char *featureName);

char *FloatingFeature_getStringWithDefault(const char *featureName, const char *defaultValue);

#endif /* FLOATING_FEATURE_H */
