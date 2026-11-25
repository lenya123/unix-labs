CC     = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -O2

BINS = fork_demo

all: $(BINS)

$(BINS): %: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

.PHONY: all clean
