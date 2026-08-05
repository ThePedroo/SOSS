CC := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang
ADB ?= adb

CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -O2 -I. -Itests -fPIC -DFLOATING_FEATURE_XML_PATH='"/data/local/tmp/floatingfeature_test/floating_feature.xml"'
TEST_CFLAGS := $(CFLAGS) -DNO_LOAD_TEST
LDFLAGS := -Wl,--version-script=floatingfeature.map
TEST_DIR := /data/local/tmp/floatingfeature_test

.PHONY: all test clean

all:
	@mkdir -p build
	$(CC) $(CFLAGS) -shared -Wl,-soname,libexpat.so -o build/libexpat_stub.so stub/expat_stub.c
	$(CC) $(CFLAGS) -c floating_feature.c -o build/floating_feature.o
	$(CC) $(CFLAGS) -c cpp_strings.c -o build/cpp_strings.o
	$(CC) $(CFLAGS) -c sem_floating_feature.c -o build/sem_floating_feature.o
	$(CC) $(LDFLAGS) -shared build/floating_feature.o build/cpp_strings.o build/sem_floating_feature.o -o libfloatingfeature.so -Lbuild -lexpat_stub

test: all
	$(CC) $(CFLAGS) -fPIE -pie tests/testsuite.c cpp_strings.c -L. -lfloatingfeature -o build/test
	$(CC) $(TEST_CFLAGS) -fPIE -pie tests/testsuite.c cpp_strings.c -L. -lfloatingfeature -o build/test_noload
	@$(ADB) shell mkdir -p $(TEST_DIR) >/dev/null
	@$(ADB) push libfloatingfeature.so build/test build/test_noload $(TEST_DIR)/ >/dev/null
	@$(ADB) shell "cd $(TEST_DIR) && LD_LIBRARY_PATH=/system/lib64:. ./test && rm -f floating_feature.xml && LD_LIBRARY_PATH=/system/lib64:. ./test_noload"

clean:
	rm -rf build libfloatingfeature.so