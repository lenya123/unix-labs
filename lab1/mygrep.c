/*
 * mygrep - a simplified grep(1) clone.
 *
 * The pattern is a POSIX basic regular expression, matched with <regex.h>.
 *
 * Supported flags:
 *   -i  ignore case
 *   -v  print the lines that do NOT match
 *   -n  prefix every matching line with its line number
 *
 * With no file operands the standard input is read, which is what makes
 * shell pipelines work:
 *   ./mycat file.txt | ./mygrep pattern
 *   ls -l            | ./mygrep pattern
 *
 * Exit status: 0 - something matched, 1 - nothing matched, 2 - an error.
 */
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int invert;       /* -v */
static int show_line_no; /* -n */
static int show_name;    /* prefix with the file name, as grep does for 2+ files */

static int matched;
static int exit_status;

static void grep_stream(FILE *fp, const regex_t *re, const char *name)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	long n = 0;

	while ((len = getline(&line, &cap, fp)) != -1) {
		int hit;

		n++;
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		hit = (regexec(re, line, 0, NULL, 0) == 0);
		if (hit == invert)
			continue;

		if (show_name)
			printf("%s:", name);
		if (show_line_no)
			printf("%ld:", n);
		puts(line);
		matched = 1;
	}

	free(line);
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-i] [-v] [-n] pattern [file...]\n", prog);
}

int main(int argc, char *argv[])
{
	int opt;
	int cflags = 0;
	int rc;
	int i;
	const char *pattern;
	regex_t re;

	while ((opt = getopt(argc, argv, "ivn")) != -1) {
		switch (opt) {
		case 'i':
			cflags |= REG_ICASE;
			break;
		case 'v':
			invert = 1;
			break;
		case 'n':
			show_line_no = 1;
			break;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	if (optind == argc) {
		usage(argv[0]);
		return 2;
	}

	pattern = argv[optind++];
	rc = regcomp(&re, pattern, cflags);
	if (rc != 0) {
		char err[256];

		regerror(rc, &re, err, sizeof(err));
		fprintf(stderr, "%s: bad pattern '%s': %s\n", argv[0], pattern, err);
		return 2;
	}

	if (optind == argc) {
		grep_stream(stdin, &re, "(standard input)");
	} else {
		show_name = (argc - optind) > 1;

		for (i = optind; i < argc; i++) {
			FILE *fp = fopen(argv[i], "r");

			if (fp == NULL) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
				exit_status = 2;
				continue;
			}
			grep_stream(fp, &re, argv[i]);
			fclose(fp);
		}
	}

	regfree(&re);

	if (exit_status != 0)
		return exit_status;
	return matched ? 0 : 1;
}
