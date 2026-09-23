# One Makefile for all four utilities of the practical works.
#
#   make        - build everything
#   make test   - build, then compare the output against the system utilities
#   make clean  - remove the binaries

CC     = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -O2

BINS = mycat mygrep myls mychmod

all: $(BINS)

$(BINS): %: %.c
	$(CC) $(CFLAGS) -o $@ $<

test: all
	./test.sh

clean:
	rm -f $(BINS)

.PHONY: all test clean
