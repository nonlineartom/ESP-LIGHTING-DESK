# Host-side unit tests for the protocol code (no ESP-IDF needed).
#   make test
#
# Needs GCC (the tests use a nested function, which clang rejects) and
# OpenSSL.  On macOS, Homebrew's gcc-NN and openssl@3 are picked up
# automatically (brew install gcc openssl@3).
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  BREW_GCC := $(lastword $(sort $(wildcard /opt/homebrew/bin/gcc-[0-9]* /usr/local/bin/gcc-[0-9]*)))
  OPENSSL  ?= $(shell brew --prefix openssl@3 2>/dev/null)
  CC       := $(if $(BREW_GCC),$(BREW_GCC),gcc)
  EXTRA_CFLAGS  := $(if $(OPENSSL),-I$(OPENSSL)/include)
  EXTRA_LDFLAGS := $(if $(OPENSSL),-L$(OPENSSL)/lib)
else
  CC ?= gcc
endif

CFLAGS  ?= -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DMESHDECK_HOST_TEST -I../../main $(EXTRA_CFLAGS)
LDLIBS  ?= $(EXTRA_LDFLAGS) -lcrypto -lm

SRCS = test_main.c ../../main/mesh_crypto.c ../../main/telink.c ../../main/mesh_net.c ../../main/mesh_prov.c \
       ../../main/mesh_cfg.c ../../main/products.c ../../main/lights.c

run_tests: $(SRCS) ../../main/*.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

.PHONY: test clean
test: run_tests
	./run_tests

clean:
	rm -rf run_tests run_tests.dSYM
