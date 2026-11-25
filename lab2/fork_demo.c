#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILD_EXIT_CODE 42
#define CHILD_LIFETIME 15

static void write_str(const char *s)
{
	ssize_t written = write(STDOUT_FILENO, s, strlen(s));

	(void)written;
}

static void at_exit_handler(void)
{
	printf("[pid %d] atexit: обработчик сработал, процесс завершается\n", getpid());
	fflush(stdout);
}

static void sigint_handler(int sig)
{
	(void)sig;
	write_str("\n  <-- signal(): SIGINT (2), прерывание с клавиатуры\n");
}

static void sigterm_handler(int sig, siginfo_t *info, void *context)
{
	(void)sig;
	(void)info;
	(void)context;
	write_str("  <-- sigaction(): SIGTERM (15), запрос на завершение\n");
}

static int install_handlers(void)
{
	struct sigaction sa;

	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		perror("signal");
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sigterm_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGTERM, &sa, NULL) != 0) {
		perror("sigaction");
		return -1;
	}

	return 0;
}

static void child_work(void)
{
	unsigned int left = CHILD_LIFETIME;

	printf("[pid %d] дочерний:  pid=%d  ppid=%d\n", getpid(), getpid(), getppid());
	printf("[pid %d] дочерний: живу %d секунд, можно присылать сигналы\n",
	       getpid(), CHILD_LIFETIME);
	fflush(stdout);

	while (left > 0)
		left = sleep(left);

	printf("[pid %d] дочерний: время вышло, выхожу с кодом %d\n",
	       getpid(), CHILD_EXIT_CODE);
	fflush(stdout);
}

static int parent_work(pid_t child)
{
	int status;

	printf("[pid %d] родитель: pid=%d  ppid=%d,  дочерний pid=%d\n",
	       getpid(), getpid(), getppid(), child);
	fflush(stdout);

	while (waitpid(child, &status, 0) < 0) {
		if (errno == EINTR) {
			printf("[pid %d] родитель: ожидание прервано сигналом, продолжаю ждать\n",
			       getpid());
			fflush(stdout);
			continue;
		}
		perror("waitpid");
		return 1;
	}

	if (WIFEXITED(status))
		printf("[pid %d] родитель: дочерний завершился сам, код завершения %d\n",
		       getpid(), WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		printf("[pid %d] родитель: дочерний убит сигналом %d\n",
		       getpid(), WTERMSIG(status));
	else
		printf("[pid %d] родитель: дочерний остановлен\n", getpid());

	fflush(stdout);
	return 0;
}

int main(void)
{
	pid_t pid;

	if (atexit(at_exit_handler) != 0) {
		fprintf(stderr, "atexit: не удалось зарегистрировать обработчик\n");
		return 1;
	}

	if (install_handlers() != 0)
		return 1;

	printf("[pid %d] до fork: pid=%d  ppid=%d\n", getpid(), getpid(), getppid());
	fflush(stdout);

	pid = fork();

	if (pid < 0) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		child_work();
		return CHILD_EXIT_CODE;
	}

	return parent_work(pid);
}
