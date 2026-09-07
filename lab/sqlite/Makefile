ZIG ?= zig
CC = $(ZIG) cc
SQLITE_PREFIX ?= /opt/homebrew/opt/sqlite
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  EXT_SUFFIX := .dll
else ifeq ($(UNAME_S),Darwin)
  EXT_SUFFIX := .dylib
else
  EXT_SUFFIX := .so
endif
OUT ?= bebopsqlite$(EXT_SUFFIX)
CFLAGS ?= -std=c11 -O2 -fPIC -Wall -Wextra -Werror -Wno-unused-function
INCLUDES = -I../../bebop/include -I../../bebop/src/generated -I$(SQLITE_PREFIX)/include
SOURCES = bebop.c ../../bebop/src/bebop_wire.c ../../bebop/src/generated/descriptor.bb.c

.PHONY: all test clean
all: $(OUT)

test: $(OUT)
	python3 test.py

$(OUT): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -shared $(SOURCES) -L$(SQLITE_PREFIX)/lib -lsqlite3 -o $@

clean:
	rm -f $(OUT)
