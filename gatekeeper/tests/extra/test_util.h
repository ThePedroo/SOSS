/* INFO: PASS/FAIL assertion helper shared by the test programs. */

#ifndef GATEKEEPER_TEST_UTIL_H
#define GATEKEEPER_TEST_UTIL_H

#include <stdio.h>

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

#endif /* GATEKEEPER_TEST_UTIL_H */
