CFLAGS := $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -fPIC -s -Wl,--version-script=libcrex.version

ifeq ($(ENV),development)
	CFLAGS := $(CFLAGS) -g -O0
else
	CFLAGS := $(CFLAGS) -O3 -DNDEBUG
endif

LIBRARY_SOURCE := crex.c
LIBRARY_HEADERS := crex.h executor.h
LIBRARY_OBJECT := build/crex.o
DYNAMIC_LIBRARY := libcrex.so.1
STATIC_LIBRARY := libcrex.a

EXAMINER_SOURCE := crexamine.c
EXAMINER_OBJECT := build/crexamine.o
EXAMINER_BINARY := bin/crexamine

DUMPER_SOURCE := crexdump.c
DUMPER_OBJECT := build/crexdump.o
DUMPER_BINARY := bin/crexdump

TEST_ENGINE_SOURCE := test-engine.c
TEST_ENGINE_OBJECT := build/test-engine.o
TEST_ENGINE_BINARY := bin/test-engine
TEST_ENGINE_TESTCASES := build/engine-testcases.h

TEST_ALLOC_HYGIENE_SOURCE := test-alloc-hygiene.c
TEST_ALLOC_HYGIENE_OBJECT := build/test-alloc-hygiene.o
TEST_ALLOC_HYGIENE_BINARY := bin/test-alloc-hygiene

DEP_FILES := $(shell find build -name "*.in")

AR ?= ar
STRIP ?= strip

.PHONY: all clean tests run-tests

.PRECIOUS: build/%.o bin/%

all: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)

tools: $(EXAMINER_BINARY) $(DUMPER_BINARY)

tests: $(TEST_ENGINE_BINARY) $(TEST_ALLOC_HYGIENE_BINARY)

run-tests: tests
	$(TEST_ENGINE_BINARY)
	$(TEST_ALLOC_HYGIENE_BINARY)

$(STATIC_LIBRARY): $(LIBRARY_OBJECT)
	$(AR) rcs $@ $^
	$(STRIP) -x $@

$(DYNAMIC_LIBRARY): $(LIBRARY_OBJECT)
	$(CC) $(CFLAGS) -shared -o $@ $^

build/%.o: %.c
	$(CC) -MMD -MF $(@:.o=.in) $(CFLAGS) -c -o $@ $<

bin/%: build/%.o $(STATIC_LIBRARY)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_ENGINE_OBJECT): $(TEST_ENGINE_SOURCE) $(TEST_ENGINE_TESTCASES)

$(TEST_ENGINE_TESTCASES):
	test-engine/generate-testcases $@ $(@:.h=.in)

clean:
	rm -f $(LIBRARY_OBJECT) $(DYNAMIC_LIBRARY) $(STATIC_LIBRARY)
	rm -f $(EXAMINER_OBJECT) $(EXAMINER_BINARY)
	rm -f $(DUMPER_OBJECT) $(DUMPER_BINARY)
	rm -f $(TEST_ENGINE_TESTCASES) $(TEST_ENGINE_OBJECT) $(TEST_ENGINE_BINARY)
	rm -f $(TEST_ALLOC_HYGIENE_OBJECT) $(TEST_ALLOC_HYGIENE_BINARY)
	rm -f $(DEP_FILES)

include $(DEP_FILES)
