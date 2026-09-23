/*
 * mycat - a simplified cat(1) clone.
 *
 * Supported flags:
 *   -n  number all output lines
 *   -b  number non-empty output lines only (overrides -n)
 *   -E  display '$' at the end of each line
 *
 * With no file operands (or with "-") the standard input is read,
 * so mycat works inside a shell pipeline.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int number_all;      /* -n */
static int number_nonblank; /* -b */
static int show_ends;       /* -E */

static int line_no = 1;     /* line counter is continuous across files, like cat */
static int exit_status;

static void cat_stream(FILE *fp)
{
	int c;
	int at_line_start = 1;

	while ((c = getc(fp)) != EOF) {
		if (at_line_start) {
			if (number_nonblank) {
				if (c != '\n')
					printf("%6d\t", line_no++);
			} else if (number_all) {
				printf("%6d\t", line_no++);
			}
			at_line_start = 0;
		}

		if (c == '\n') {
			if (show_ends)
				putchar('$');
			at_line_start = 1;
		}
		putchar(c);
	}
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-n] [-b] [-E] [file...]\n", prog);
}

int main(int argc, char *argv[])
{
	int opt;
	int i;

	while ((opt = getopt(argc, argv, "nbE")) != -1) {
		switch (opt) {
		case 'n':
			number_all = 1;
			break;
		case 'b':
			number_nonblank = 1;
			break;
		case 'E':
			show_ends = 1;
			break;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	if (optind == argc) {
		cat_stream(stdin);
		return exit_status;
	}

	for (i = optind; i < argc; i++) {
		FILE *fp;

		if (strcmp(argv[i], "-") == 0) {
			cat_stream(stdin);
			continue;
		}

		fp = fopen(argv[i], "r");
		if (fp == NULL) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			exit_status = 1;
			continue;
		}
		cat_stream(fp);
		fclose(fp);
	}

	return exit_status;
}
