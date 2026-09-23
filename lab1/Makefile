# Один Makefile на все четыре утилиты.
#
#   make        - собрать всё
#   make clean  - удалить собранное

CC     = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -O2

BINS = mycat mygrep myls mychmod

all: $(BINS)

$(BINS): %: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

.PHONY: all clean
