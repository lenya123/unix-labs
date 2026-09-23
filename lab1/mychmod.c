/* mychmod - аналог chmod: режим восьмеричный (766) или символьный (ug+rw) */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define ALL_R (S_IRUSR | S_IRGRP | S_IROTH)
#define ALL_W (S_IWUSR | S_IWGRP | S_IWOTH)
#define ALL_X (S_IXUSR | S_IXGRP | S_IXOTH)
#define ALL_WHO (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID)

static int parse_octal(const char *s, mode_t *out)
{
	const char *p;
	unsigned long v;

	if (*s == '\0')
		return 0;

	for (p = s; *p != '\0'; p++)
		if (*p < '0' || *p > '7')
			return 0;

	if (p - s > 4)
		return 0;

	v = strtoul(s, NULL, 8);
	*out = (mode_t)(v & 07777);
	return 1;
}

/* mode приходит целиком, с типом файла: для 'X' надо знать, каталог это или нет */
static int apply_symbolic(const char *spec, mode_t *mode, mode_t umask_val)
{
	const char *p = spec;

	if (*p == '\0')
		return 0;

	while (*p != '\0') {
		mode_t who = 0;
		mode_t bits = 0;
		mode_t target;
		int who_given = 0;
		char op;

		for (; *p != '\0'; p++) {
			if (*p == 'u')
				who |= S_IRWXU | S_ISUID;
			else if (*p == 'g')
				who |= S_IRWXG | S_ISGID;
			else if (*p == 'o')
				who |= S_IRWXO;
			else if (*p == 'a')
				who |= ALL_WHO;
			else
				break;
			who_given = 1;
		}

		if (*p != '+' && *p != '-' && *p != '=')
			return 0;
		op = *p++;

		/* без ugoa изменение идёт всем, и sticky заодно; с явным классом
		   sticky не трогаем, иначе "u=rwx" его сбросит */
		if (!who_given)
			who = ALL_WHO | S_ISVTX;

		for (; *p != '\0' && *p != ','; p++) {
			switch (*p) {
			case 'r':
				bits |= ALL_R;
				break;
			case 'w':
				bits |= ALL_W;
				break;
			case 'x':
				bits |= ALL_X;
				break;
			case 'X': /* x только каталогам и тому, что уже исполняемое */
				if (S_ISDIR(*mode) || (*mode & ALL_X))
					bits |= ALL_X;
				break;
			case 's':
				bits |= S_ISUID | S_ISGID;
				break;
			case 't':
				bits |= S_ISVTX;
				break;
			default:
				return 0;
			}
		}

		target = bits & who;
		if (!who_given)
			target &= ~umask_val; /* "+x" учитывает umask, "a+x" - нет */

		switch (op) {
		case '+':
			*mode |= target;
			break;
		case '-':
			*mode &= ~target;
			break;
		case '=':
			*mode = (*mode & ~who) | target;
			break;
		}

		if (*p == ',')
			p++;
	}

	return 1;
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s MODE file...\n", prog);
	fprintf(stderr, "  MODE is octal (755) or symbolic (u+rw, go-w, +x, a=rx)\n");
}

int main(int argc, char *argv[])
{
	const char *spec;
	mode_t octal = 0;
	mode_t um;
	mode_t probe;
	int is_octal;
	int exit_status = 0;
	int i;

	if (argc < 3) {
		usage(argv[0]);
		return 2;
	}

	spec = argv[1];
	is_octal = parse_octal(spec, &octal);

	/* прочитать umask нельзя, только выставить - ставим и сразу возвращаем */
	um = umask(0);
	umask(um);

	/* неверный режим отвергаем до того, как тронули хоть один файл */
	probe = 0;
	if (!is_octal && !apply_symbolic(spec, &probe, um)) {
		fprintf(stderr, "%s: invalid mode: '%s'\n", argv[0], spec);
		return 2;
	}

	for (i = 2; i < argc; i++) {
		struct stat st;
		mode_t newmode;

		/* stat, а не lstat: chmod идёт по ссылке, у самой ссылки прав нет */
		if (stat(argv[i], &st) != 0) {
			fprintf(stderr, "%s: cannot access '%s': %s\n",
			        argv[0], argv[i], strerror(errno));
			exit_status = 1;
			continue;
		}

		if (is_octal) {
			newmode = octal;
		} else {
			newmode = st.st_mode;
			apply_symbolic(spec, &newmode, um);
		}

		if (chmod(argv[i], newmode & 07777) != 0) {
			fprintf(stderr, "%s: changing permissions of '%s': %s\n",
			        argv[0], argv[i], strerror(errno));
			exit_status = 1;
		}
	}

	return exit_status;
}
