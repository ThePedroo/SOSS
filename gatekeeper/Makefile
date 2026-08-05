CC := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang
ADB ?= adb

CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -O2 -I. -Itests -fPIC
TEST_CFLAGS := $(CFLAGS) -DGATEKEEPER_TEST_SKIP_PROPERTY_WAIT -DGATEKEEPER_TA_PATH='"/data/local/tmp/gatekeeper_test/ta"'
LDFLAGS := -Wl,--version-script=gatekeeper.map
TEST_DIR := /data/local/tmp/gatekeeper_test

.PHONY: all test diftest clean

all:
	@mkdir -p build
	$(CC) $(CFLAGS) -shared -Wl,-soname,libteecl.so -o build/libteecl_stub.so stub/teecl_stub.c
	$(CC) $(CFLAGS) -c gatekeeper.c -o build/gatekeeper.o
	$(CC) $(LDFLAGS) -shared build/gatekeeper.o -o libgatekeeper.so -llog -Lbuild -lteecl_stub

test: all
	$(CC) $(TEST_CFLAGS) -fPIE -pie tests/testsuite.c gatekeeper.c -o build/test -llog
	@$(ADB) shell mkdir -p $(TEST_DIR) >/dev/null
	@$(ADB) push libgatekeeper.so build/test $(TEST_DIR)/ >/dev/null
	@$(ADB) shell "cd $(TEST_DIR) && LD_LIBRARY_PATH=. ./test"

diftest: all
	@mkdir -p build
	$(CC) $(CFLAGS) -shared -Wl,-soname,libteecl.so tests/extra/shim.c -o build/libteecl.so
	$(CC) $(CFLAGS) -DGATEKEEPER_FORCE_PROTOCOL=1 -shared gatekeeper.c -o build/libgatekeeper_v1.so -llog -Lbuild -lteecl_stub
	$(CC) $(CFLAGS) -DGATEKEEPER_FORCE_PROTOCOL=2 -shared gatekeeper.c -o build/libgatekeeper_v2.so -llog -Lbuild -lteecl_stub
	$(CC) $(CFLAGS) -fPIE -pie tests/differential.c -o build/diftest -ldl
	@$(ADB) shell mkdir -p $(TEST_DIR) >/dev/null
	@$(ADB) push libgatekeeper.so build/libteecl.so build/libgatekeeper_v1.so build/libgatekeeper_v2.so build/diftest $(TEST_DIR)/ >/dev/null
	@$(ADB) push blobs/gatekeeper.s5e8825.so $(TEST_DIR)/ >/dev/null
	@$(ADB) push blobs/gatekeeper_old.so $(TEST_DIR)/gatekeeper_old.so >/dev/null
	@$(ADB) shell "cd $(TEST_DIR) && LD_LIBRARY_PATH=. LD_PRELOAD=./libteecl.so ./diftest"

clean:
	rm -rf build libgatekeeper.so
