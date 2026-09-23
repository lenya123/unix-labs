/* myls - аналог ls: флаги -l и -a, цвета только в терминал */
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* те же коды, что у ls */
#define COLOR_DIR     "\033[01;34m" /* каталог */
#define COLOR_EXEC    "\033[01;32m" /* исполняемый */
#define COLOR_LINK    "\033[01;36m" /* ссылка */
#define COLOR_DIR_OW  "\033[34;42m" /* каталог, открытый на запись всем */
#define COLOR_DIR_TW  "\033[30;42m" /* он же со sticky */
#define COLOR_DIR_ST  "\033[37;44m" /* sticky */
#define COLOR_OFF     "\033[0m"

#define DEFAULT_TERM_WIDTH 80

/* старше полугода - ls печатает год вместо времени */
#define SIX_MONTHS (6L * 30 * 24 * 60 * 60)

struct entry {
	char *name;         /* что печатаем */
	char *path;         /* полный путь - для lstat и readlink */
	struct stat st;
};

static int opt_long;
static int opt_all;
static int use_color;
static int is_tty;
static int term_width = DEFAULT_TERM_WIDTH;
static int exit_status;

static void clear_to_eol(size_t line_width)
{
	if (is_tty && line_width > (size_t)term_width)
		fputs("\033[K", stdout);
}

/* порядок как у ls: сначала COLUMNS, потом терминал её перебивает */
static void detect_term_width(void)
{
	const char *columns_env = getenv("COLUMNS");
	struct winsize ws;

	if (columns_env != NULL && *columns_env != '\0') {
		long v = strtol(columns_env, NULL, 10);

		if (v > 0 && v < INT_MAX)
			term_width = (int)v;
	}
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		term_width = ws.ws_col;
}

/* ls один раз за запуск сбрасывает цвет - перед первым цветным именем
   по порядку сортировки, а не по порядку печати */
static const struct entry *reset_before;
static int reset_emitted;

/* ширина имени на экране: в UTF-8 байты-продолжения (10xxxxxx)
   своей позиции не занимают, их считать нельзя */
static size_t display_width(const char *s)
{
	size_t w = 0;

	for (; *s != '\0'; s++)
		if ((*s & 0xC0) != 0x80)
			w++;

	return w;
}

/* собирает строку вида drwxr-xr-x */
static void mode_string(mode_t m, char *out)
{
	out[0] = S_ISDIR(m)  ? 'd' :
	         S_ISLNK(m)  ? 'l' :
	         S_ISCHR(m)  ? 'c' :
	         S_ISBLK(m)  ? 'b' :
	         S_ISFIFO(m) ? 'p' :
	         S_ISSOCK(m) ? 's' : '-';

	out[1] = (m & S_IRUSR) ? 'r' : '-';
	out[2] = (m & S_IWUSR) ? 'w' : '-';
	out[3] = (m & S_ISUID) ? ((m & S_IXUSR) ? 's' : 'S')
	                       : ((m & S_IXUSR) ? 'x' : '-');
	out[4] = (m & S_IRGRP) ? 'r' : '-';
	out[5] = (m & S_IWGRP) ? 'w' : '-';
	out[6] = (m & S_ISGID) ? ((m & S_IXGRP) ? 's' : 'S')
	                       : ((m & S_IXGRP) ? 'x' : '-');
	out[7] = (m & S_IROTH) ? 'r' : '-';
	out[8] = (m & S_IWOTH) ? 'w' : '-';
	out[9] = (m & S_ISVTX) ? ((m & S_IXOTH) ? 't' : 'T')
	                       : ((m & S_IXOTH) ? 'x' : '-');
	out[10] = '\0';
}

static void time_string(time_t t, char *out, size_t size)
{
	time_t now = time(NULL);
	struct tm tm;

	localtime_r(&t, &tm);
	if (t <= now + 3600 && t > now - SIX_MONTHS)
		strftime(out, size, "%b %e %H:%M", &tm);
	else
		strftime(out, size, "%b %e  %Y", &tm);
}

static const char *user_name(uid_t uid)
{
	static char fallback[32];
	struct passwd *pw = getpwuid(uid);

	if (pw != NULL)
		return pw->pw_name;

	snprintf(fallback, sizeof(fallback), "%u", (unsigned)uid);
	return fallback;
}

static const char *group_name(gid_t gid)
{
	static char fallback[32];
	struct group *gr = getgrgid(gid);

	if (gr != NULL)
		return gr->gr_name;

	snprintf(fallback, sizeof(fallback), "%u", (unsigned)gid);
	return fallback;
}

static const char *color_of(const struct stat *st)
{
	if (S_ISLNK(st->st_mode))
		return COLOR_LINK;

	if (S_ISDIR(st->st_mode)) {
		/* каталог, куда может писать кто угодно, ls красит не синим,
		   а предупреждающим цветом - так выглядит /tmp */
		int other_writable = (st->st_mode & S_IWOTH) != 0;
		int sticky = (st->st_mode & S_ISVTX) != 0;

		if (other_writable && sticky)
			return COLOR_DIR_TW;
		if (other_writable)
			return COLOR_DIR_OW;
		if (sticky)
			return COLOR_DIR_ST;
		return COLOR_DIR;
	}

	if (S_ISREG(st->st_mode) && (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
		return COLOR_EXEC;

	return NULL;
}

static void print_name(const struct entry *e)
{
	const char *color = use_color ? color_of(&e->st) : NULL;

	if (color == NULL) {
		fputs(e->name, stdout);
		return;
	}

	if (e == reset_before) {
		fputs(COLOR_OFF, stdout);
		reset_before = NULL;
		reset_emitted = 1;
	}
	printf("%s%s%s", color, e->name, COLOR_OFF);
}

static int compare_entries(const void *a, const void *b)
{
	const struct entry *ea = a;
	const struct entry *eb = b;

	return strcoll(ea->name, eb->name);
}

static void free_entries(struct entry *v, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		free(v[i].name);
		free(v[i].path);
	}
	free(v);
}

static char *join_path(const char *dir, const char *name)
{
	size_t len = strlen(dir) + 1 + strlen(name) + 1;
	char *p = malloc(len);

	if (p == NULL) {
		perror("malloc");
		exit(2);
	}
	snprintf(p, len, "%s/%s", dir, name);
	return p;
}

static char *xstrdup(const char *s)
{
	char *p = strdup(s);

	if (p == NULL) {
		perror("strdup");
		exit(2);
	}
	return p;
}

static void push_entry(struct entry **v, size_t *n, size_t *cap,
                       char *name, char *path)
{
	struct stat st;

	/* lstat, а не stat: иначе ссылка показалась бы тем файлом, на который
	   указывает, и никогда не была бы бирюзовой */
	if (lstat(path, &st) != 0) {
		fprintf(stderr, "myls: cannot access '%s': %s\n", path, strerror(errno));
		exit_status = 1;
		free(name);
		free(path);
		return;
	}

	if (*n == *cap) {
		size_t newcap = (*cap == 0) ? 32 : *cap * 2;
		struct entry *bigger = realloc(*v, newcap * sizeof(**v));

		if (bigger == NULL) {
			perror("realloc");
			exit(2);
		}
		*v = bigger;
		*cap = newcap;
	}

	(*v)[*n].name = name;
	(*v)[*n].path = path;
	(*v)[*n].st = st;
	(*n)++;
}

/* show_total выключен для обычных файлов: у "ls -l file.txt" строки "total" нет */
static void list_long(const struct entry *v, size_t n, int show_total)
{
	int w_links = 0, w_user = 0, w_group = 0, w_size = 0;
	unsigned long long blocks = 0;
	size_t i;

	/* первый проход - ширины колонок, чтобы таблица вышла ровной */
	for (i = 0; i < n; i++) {
		char buf[64];
		int len;

		blocks += (unsigned long long)v[i].st.st_blocks;

		len = snprintf(buf, sizeof(buf), "%lu", (unsigned long)v[i].st.st_nlink);
		if (len > w_links)
			w_links = len;

		len = (int)strlen(user_name(v[i].st.st_uid));
		if (len > w_user)
			w_user = len;

		len = (int)strlen(group_name(v[i].st.st_gid));
		if (len > w_group)
			w_group = len;

		len = snprintf(buf, sizeof(buf), "%lld", (long long)v[i].st.st_size);
		if (len > w_size)
			w_size = len;
	}

	/* система считает блоки по 512 байт, а ls печатает килобайтные */
	if (show_total)
		printf("total %llu\n", blocks / 2);

	for (i = 0; i < n; i++) {
		char mode[11];
		char when[64];
		size_t line_width;

		mode_string(v[i].st.st_mode, mode);
		time_string(v[i].st.st_mtime, when, sizeof(when));

		printf("%s %*lu %-*s %-*s %*lld %s ",
		       mode,
		       w_links, (unsigned long)v[i].st.st_nlink,
		       w_user, user_name(v[i].st.st_uid),
		       w_group, group_name(v[i].st.st_gid),
		       w_size, (long long)v[i].st.st_size,
		       when);

		line_width = 10 + 1 + (size_t)w_links + 1 + (size_t)w_user + 1
		             + (size_t)w_group + 1 + (size_t)w_size + 1
		             + display_width(when) + 1 + display_width(v[i].name);

		print_name(&v[i]);
		clear_to_eol(line_width);

		if (S_ISLNK(v[i].st.st_mode)) {
			char target[PATH_MAX];
			ssize_t len = readlink(v[i].path, target, sizeof(target) - 1);

			if (len >= 0) {
				target[len] = '\0';
				printf(" -> %s", target);
			}
		}

		putchar('\n');
	}
}

static size_t column_width(const struct entry *v, size_t n, size_t rows, size_t c)
{
	size_t widest = 0, r;

	for (r = 0; r < rows; r++) {
		size_t idx = c * rows + r;
		size_t w;

		if (idx >= n)
			break;
		w = display_width(v[idx].name);
		if (w > widest)
			widest = w;
	}

	return widest;
}

/* в терминал - колонками сверху вниз, иначе по одному имени в строку: ls
   ведёт себя так же. Ширина каждой колонки считается по ней самой, а не по
   самому длинному имени во всём списке - иначе вывод разъезжается */
static void list_columns(const struct entry *v, size_t n)
{
	size_t i, cols, rows, r, c;
	size_t best_cols = 1;

	if (n == 0)
		return;

	if (!is_tty) { /* не терминал - по одному имени в строку */
		for (i = 0; i < n; i++) {
			print_name(&v[i]);
			putchar('\n');
		}
		return;
	}

	/* берём самую широкую раскладку, которая ещё влезает */
	for (cols = 1; cols <= n; cols++) {
		size_t total = 0;

		rows = (n + cols - 1) / cols;
		for (c = 0; c < cols; c++) {
			total += column_width(v, n, rows, c);
			if (c + 1 < cols)
				total += 2; /* зазор между колонками */
		}

		if (total > (size_t)term_width)
			break;
		best_cols = cols;
	}

	cols = best_cols;
	rows = (n + cols - 1) / cols;

	for (r = 0; r < rows; r++) {
		size_t line_width = 0;

		for (c = 0; c < cols; c++) {
			size_t idx = c * rows + r;

			if (idx >= n)
				continue;

			print_name(&v[idx]);
			line_width += display_width(v[idx].name);

			/* добиваем пробелами, только если справа ещё есть имя */
			if (idx + rows < n) {
				size_t pad = column_width(v, n, rows, c) + 2
				             - display_width(v[idx].name);

				printf("%*s", (int)pad, "");
				line_width += pad;
			}
		}
		clear_to_eol(line_width);
		putchar('\n');
	}
}

static void list_entries(struct entry *v, size_t n, int show_total)
{
	qsort(v, n, sizeof(*v), compare_entries);

	if (use_color && !reset_emitted && reset_before == NULL) {
		size_t i;

		for (i = 0; i < n; i++)
			if (color_of(&v[i].st) != NULL) {
				reset_before = &v[i];
				break;
			}
	}

	if (opt_long)
		list_long(v, n, show_total);
	else
		list_columns(v, n);
}

static void list_dir(const char *path, int with_header)
{
	struct entry *v = NULL;
	size_t n = 0, cap = 0;
	DIR *dp = opendir(path);
	struct dirent *de;

	if (dp == NULL) {
		fprintf(stderr, "myls: cannot open directory '%s': %s\n",
		        path, strerror(errno));
		exit_status = 1;
		return;
	}

	if (with_header)
		printf("%s:\n", path);

	errno = 0;
	while ((de = readdir(dp)) != NULL) {
		if (!opt_all && de->d_name[0] == '.')
			continue;
		push_entry(&v, &n, &cap, xstrdup(de->d_name), join_path(path, de->d_name));
		errno = 0;
	}
	if (errno != 0) {
		fprintf(stderr, "myls: reading '%s': %s\n", path, strerror(errno));
		exit_status = 1;
	}
	closedir(dp);

	list_entries(v, n, 1); /* у каталога строка "total" есть */
	free_entries(v, n);
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-l] [-a] [file...]\n", prog);
}

int main(int argc, char *argv[])
{
	struct entry *files = NULL;      /* обычные файлы - печатаются первыми */
	size_t nfiles = 0, cap = 0;
	char **dirs;
	size_t ndirs = 0, i;
	int opt, operands;

	/* локаль нужна и для сортировки strcoll, и для названия месяца в дате */
	setlocale(LC_ALL, "");

	while ((opt = getopt(argc, argv, "la")) != -1) {
		switch (opt) {
		case 'l':
			opt_long = 1;
			break;
		case 'a':
			opt_all = 1;
			break;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	is_tty = isatty(STDOUT_FILENO);
	use_color = is_tty;
	detect_term_width();
	operands = argc - optind;

	if (operands == 0) {
		list_dir(".", 0);
		return exit_status;
	}

	dirs = calloc((size_t)operands, sizeof(*dirs));
	if (dirs == NULL) {
		perror("calloc");
		return 2;
	}

	/* как ls: сначала обычные файлы, потом каталоги */
	for (i = 0; i < (size_t)operands; i++) {
		char *arg = argv[optind + (int)i];
		struct stat st;

		if (lstat(arg, &st) != 0) {
			fprintf(stderr, "myls: cannot access '%s': %s\n", arg, strerror(errno));
			exit_status = 1;
			continue;
		}

		if (S_ISDIR(st.st_mode))
			dirs[ndirs++] = arg;
		else
			push_entry(&files, &nfiles, &cap, xstrdup(arg), xstrdup(arg));
	}

	if (nfiles > 0) {
		list_entries(files, nfiles, 0); /* у файлов строки "total" нет */
		free_entries(files, nfiles);
	}

	for (i = 0; i < ndirs; i++) {
		if (nfiles > 0 || i > 0)
			putchar('\n');
		/* заголовок нужен, когда аргументов больше одного */
		list_dir(dirs[i], operands > 1);
	}

	free(dirs);
	return exit_status;
}
