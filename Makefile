CC ?= cc
AR ?= ar
INSTALL ?= install
PYTHON ?= python3

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DESTDIR ?=

CPPFLAGS ?=
CFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

BASE_CPPFLAGS = -Iinclude
BASE_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror
ALL_CPPFLAGS = $(BASE_CPPFLAGS) $(CPPFLAGS)
ALL_CFLAGS = $(BASE_CFLAGS) $(CFLAGS)

SANITIZERS ?=
ifneq ($(strip $(SANITIZERS)),)
ALL_CFLAGS += -fsanitize=$(SANITIZERS) -fno-omit-frame-pointer
LDFLAGS += -fsanitize=$(SANITIZERS)
endif

ANALYZER_CC ?= gcc
ANALYZER_CFLAGS ?= -O0 -g -fanalyzer
FUZZ_CC ?= clang
FUZZ_RUNS ?= 10000

SRCDIR = src
INCDIR = include
TOOLDIR = tools
TESTDIR = tests
BUILDDIR = build

LIB_SRCS = $(wildcard $(SRCDIR)/*.c)
LIB_OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(LIB_SRCS))

TOOL_SRCS = $(wildcard $(TOOLDIR)/*.c)
TOOL_MAIN_SRCS = $(filter-out $(TOOLDIR)/cli_common.c,$(TOOL_SRCS))
TOOL_BINS = $(patsubst $(TOOLDIR)/%.c,$(BUILDDIR)/%,$(TOOL_MAIN_SRCS))
TOOL_COMMON = $(TOOLDIR)/cli_common.c $(TOOLDIR)/cli_common.h

TEST_SRCS = $(filter-out $(TESTDIR)/fuzz_parse.c,$(wildcard $(TESTDIR)/*.c))
TEST_BINS = $(patsubst $(TESTDIR)/%.c,$(BUILDDIR)/test_%,$(TEST_SRCS))

LIBNAME = libhonest.a

.PHONY: all lib tools tests check python-check conformance generated-conformance test audit \
	sanitize check-asan check-ubsan check-leak analyze fuzz-build fuzz-smoke fuzz-random \
	install install-check clean

all: lib tools

lib: $(BUILDDIR)/$(LIBNAME)

tools: lib $(TOOL_BINS)

tests: lib $(TEST_BINS)

check: tests tools
	@for t in $(TEST_BINS); do echo "=== $$t ==="; $$t || exit 1; done
	bash tests/test_cli.sh "$(CURDIR)/$(BUILDDIR)"
	@echo "All C and CLI tests passed."

python-check:
	$(PYTHON) -m pytest python/tests/ -q

conformance: tools
	PYTHON="$(PYTHON)" bash scripts/cross-impl-conformance.sh
	PYTHON="$(PYTHON)" bash scripts/cross-impl-conformance-level3.sh
	PYTHON="$(PYTHON)" bash scripts/cross-impl-boundaries.sh
	PYTHONPATH=python $(PYTHON) scripts/generated-conformance.py

generated-conformance: tools
	PYTHONPATH=python $(PYTHON) scripts/generated-conformance.py

test: check python-check conformance

$(BUILDDIR)/$(LIBNAME): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/honest.h | $(BUILDDIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c $< -o $@

$(BUILDDIR)/%: $(TOOLDIR)/%.c $(TOOL_COMMON) $(BUILDDIR)/$(LIBNAME) | $(BUILDDIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) $(TOOLDIR)/$*.c \
		$(TOOLDIR)/cli_common.c -L$(BUILDDIR) -lhonest \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILDDIR)/test_%: $(TESTDIR)/%.c $(BUILDDIR)/$(LIBNAME) | $(BUILDDIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) $< -L$(BUILDDIR) -lhonest \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

sanitize:
	$(MAKE) clean
	$(MAKE) check CFLAGS='-O1 -g3' SANITIZERS='address,undefined'

check-asan:
	$(MAKE) clean
	$(MAKE) check CFLAGS='-O1 -g3' SANITIZERS='address'

check-ubsan:
	$(MAKE) clean
	$(MAKE) check CFLAGS='-O1 -g3' SANITIZERS='undefined'

check-leak:
	$(MAKE) clean
	$(MAKE) check CFLAGS='-O1 -g3' SANITIZERS='leak'

analyze:
	$(MAKE) clean
	$(MAKE) all CC="$(ANALYZER_CC)" CFLAGS="$(ANALYZER_CFLAGS)"

fuzz-build: $(BUILDDIR)/fuzz_parse

$(BUILDDIR)/fuzz_parse: $(TESTDIR)/fuzz_parse.c $(LIB_SRCS) $(INCDIR)/honest.h | $(BUILDDIR)
	$(FUZZ_CC) $(BASE_CPPFLAGS) $(BASE_CFLAGS) -O1 -g \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		$(TESTDIR)/fuzz_parse.c $(LIB_SRCS) -o $@

fuzz-smoke: fuzz-build
	mkdir -p $(BUILDDIR)/fuzz-corpus
	cp $(TESTDIR)/corpus/valid/*.honest examples/*.honest \
		$(BUILDDIR)/fuzz-corpus/
	$(BUILDDIR)/fuzz_parse -runs=$(FUZZ_RUNS) -max_len=4096 \
		-timeout=5 -artifact_prefix=$(BUILDDIR)/ $(BUILDDIR)/fuzz-corpus

fuzz-random: | $(BUILDDIR)
	$(CC) $(BASE_CPPFLAGS) $(BASE_CFLAGS) -O1 -g \
		-DHONEST_FUZZ_STANDALONE -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(TESTDIR)/fuzz_parse.c $(LIB_SRCS) \
		-o $(BUILDDIR)/fuzz_parse_random
	$(BUILDDIR)/fuzz_parse_random

audit: test sanitize analyze fuzz-random
	@echo "Full audit passed."

install: all
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBDIR)" \
		"$(DESTDIR)$(INCLUDEDIR)"
	$(INSTALL) -m 0755 $(TOOL_BINS) "$(DESTDIR)$(BINDIR)/"
	$(INSTALL) -m 0644 "$(BUILDDIR)/$(LIBNAME)" "$(DESTDIR)$(LIBDIR)/$(LIBNAME)"
	$(INSTALL) -m 0644 "$(INCDIR)/honest.h" "$(DESTDIR)$(INCLUDEDIR)/honest.h"

install-check: all
	bash tests/test_install.sh

clean:
	rm -rf $(BUILDDIR)
