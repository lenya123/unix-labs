# Один Makefile на все четыре утилиты.
#
#   make        - собрать всё
#   make test   - собрать и сравнить вывод с системными утилитами
#   make clean  - удалить собранное

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
