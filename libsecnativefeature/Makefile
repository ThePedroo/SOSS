NDK_PATH ?= $(HOME)/Android/Sdk/ndk/29.0.13113456

TOOLCHAIN = $(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64
SYSROOT = $(TOOLCHAIN)/sysroot
CC = $(TOOLCHAIN)/bin/clang --target=aarch64-linux-android35 --sysroot=$(SYSROOT)

ADB ?= adb

CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -O2 -I. -Itests -fPIC
TEST_CFLAGS := $(CFLAGS)
LDFLAGS := -Wl,--version-script=secnativefeature.map
TEST_DIR := /data/local/tmp/secnativefeature_test

.PHONY: all test diftest clean

all:
	@mkdir -p build
	$(CC) $(CFLAGS) -shared -Wl,-soname,libexpat.so -o build/libexpat_stub.so stub/expat_stub.c
	$(CC) $(CFLAGS) -c secnativefeature.c -o build/secnativefeature.o
	$(CC) $(CFLAGS) -c sec_native_feature.c -o build/sec_native_feature.o
	$(CC) $(LDFLAGS) -shared build/secnativefeature.o build/sec_native_feature.o -o libsecnativefeature.so -lz -Lbuild -lexpat_stub

test: all
	$(CC) $(TEST_CFLAGS) -fPIE -pie tests/testsuite.c tests/extra/shim.c secnativefeature.c sec_native_feature.c -o build/test -lz -Lbuild -lexpat_stub
	@$(ADB) shell mkdir -p $(TEST_DIR) >/dev/null
	@$(ADB) push libsecnativefeature.so build/test $(TEST_DIR)/ >/dev/null
	@$(ADB) shell "cd $(TEST_DIR) && LD_LIBRARY_PATH=. ./test"

diftest: all
	$(CC) $(CFLAGS) -shared -Wl,-soname,libsnfshim.so tests/extra/shim.c -o build/libsnfshim.so
	$(CC) $(CFLAGS) -fPIE -pie tests/differential.c -o build/diftest -ldl -lz
	@$(ADB) shell mkdir -p $(TEST_DIR) >/dev/null
	@$(ADB) push libsecnativefeature.so build/libsnfshim.so build/diftest $(TEST_DIR)/ >/dev/null
	@$(ADB) push blobs/libsecnativefeature.so $(TEST_DIR)/original.so >/dev/null
	@$(ADB) shell "cd $(TEST_DIR) && LD_LIBRARY_PATH=. LD_PRELOAD=./libsnfshim.so ./diftest"

clean:
	rm -rf build libsecnativefeature.so