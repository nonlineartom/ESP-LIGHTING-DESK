# Host-side unit tests for the protocol code (no ESP-IDF needed).
#   make && ./run_tests
CC      ?= gcc
CFLAGS  ?= -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DMESHDECK_HOST_TEST -I../../main
LDLIBS  ?= -lcrypto -lm

SRCS = test_main.c ../../main/mesh_crypto.c ../../main/telink.c ../../main/mesh_net.c ../../main/mesh_prov.c ../../main/mesh_cfg.c ../../main/products.c ../../main/lights.c

run_tests: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

.PHONY: test clean
test: run_tests
	./run_tests

clean:
	rm -f run_tests
