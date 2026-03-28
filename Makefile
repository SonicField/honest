CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CFLAGS  += -Iinclude
AR      ?= ar

SRCDIR  = src
INCDIR  = include
TOOLDIR = tools
TESTDIR = tests
BUILDDIR = build

LIB_SRCS = $(wildcard $(SRCDIR)/*.c)
LIB_OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(LIB_SRCS))

TOOL_SRCS = $(wildcard $(TOOLDIR)/*.c)
TOOL_BINS = $(patsubst $(TOOLDIR)/%.c,$(BUILDDIR)/%,$(TOOL_SRCS))

TEST_SRCS = $(wildcard $(TESTDIR)/*.c)
TEST_BINS = $(patsubst $(TESTDIR)/%.c,$(BUILDDIR)/test_%,$(TEST_SRCS))

LIBNAME = libhonest.a

.PHONY: all lib tools tests check clean

all: lib tools

lib: $(BUILDDIR)/$(LIBNAME)

tools: lib $(TOOL_BINS)

tests: lib $(TEST_BINS)

check: tests
	@for t in $(TEST_BINS); do echo "=== $$t ==="; $$t || exit 1; done
	@echo "All tests passed."

$(BUILDDIR)/$(LIBNAME): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%: $(TOOLDIR)/%.c $(BUILDDIR)/$(LIBNAME) | $(BUILDDIR)
	$(CC) $(CFLAGS) $< -L$(BUILDDIR) -lhonest -o $@

$(BUILDDIR)/test_%: $(TESTDIR)/%.c $(BUILDDIR)/$(LIBNAME) | $(BUILDDIR)
	$(CC) $(CFLAGS) $< -L$(BUILDDIR) -lhonest -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
