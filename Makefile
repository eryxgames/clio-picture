# CLIo Picture: make builds the library for this machine into build/, make test checks it.
#
# The floats must round as CLIo's Python rounds them: no fused multiply-add (-ffp-contract=off) and no
# fast-math. Any C99 compiler; on Windows use the MinGW gcc or clang (or see build.bat for MSVC).

CC      ?= cc
CFLAGS  ?= -O2
override CFLAGS += -std=c99 -Wall -Wextra -fPIC -fvisibility=hidden -ffp-contract=off -fno-fast-math -Iinclude
override LDLIBS += -lm

UNAME := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  LIB := build/clio_picture.dll
  SHARED := -shared
else ifeq ($(UNAME),Darwin)
  LIB := build/libclio_picture.dylib
  SHARED := -dynamiclib -install_name @rpath/libclio_picture.dylib
else
  LIB := build/libclio_picture.so
  SHARED := -shared
endif

SRC := src/noise.c src/dots.c src/ground.c src/sky.c src/room.c src/frame.c src/model.c
HDR := include/clio_picture.h

all: $(LIB)

$(LIB): $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SHARED) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

build/test_picture: tests/test_picture.c tests/cases.h $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -DCLIO_PICTURE_STATIC -o $@ tests/test_picture.c $(SRC) $(LDFLAGS) $(LDLIBS)

test: build/test_picture
	./build/test_picture

clean:
	rm -rf build

.PHONY: all test clean
