#include "common.h"
#include "comms.h"
#include <sys/wait.h>
#include <poll.h>
#include <errno.h>

#define LLM_SERV_DEFAULT_PORT	18001
#define LLM_SERV_DEFAULT_HOST	"0.0.0.0"
#define LLM_BUF_SIZE		4096
#define POLL_TIMEOUT_MS		1000

#define DEFAULT_LLM_DEMO	"./engine/mnn/llm_demo"
#define DEFAULT_MODEL_CONFIG	"./models/Qwen35-08b_mnn/config.json"

static volatile int g_running = 1;
static pid_t g_child_pid = -1;
static int g_child_stdin_fd = -1;
static int g_child_stdout_fd = -1;

static void signal_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

static void sigchld_handler(int sig)
{
	int status;
	pid_t pid;

	(void)sig;
	pid = waitpid(-1, &status, WNOHANG);
	if (pid > 0 && pid == g_child_pid) {
		fprintf(stderr, "[llm_serv] llm_demo (pid=%d) exited", pid);
		if (WIFEXITED(status))
			fprintf(stderr, " with status %d", WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			fprintf(stderr, " by signal %d", WTERMSIG(status));
		fprintf(stderr, "\n");
		g_child_pid = -1;
		g_running = 0;
	}
}

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [options]\n"
		"  -p <port>        Listen port (default: %d)\n"
		"  -h <host>        Listen host (default: %s)\n"
		"  -e <path>        llm_demo binary path (default: %s)\n"
		"  -c <config>      Model config.json path (default: %s)\n"
		"  -w <dir>         Set working directory for llm_demo\n"
		"  -v               Verbose mode\n"
		"  -help            Show this help\n",
		prog, LLM_SERV_DEFAULT_PORT, LLM_SERV_DEFAULT_HOST,
		DEFAULT_LLM_DEMO, DEFAULT_MODEL_CONFIG);
}

static int start_llm_demo(const char *demo_path, const char *config_path, const char *work_dir)
{
	int stdin_pipe[2];
	int stdout_pipe[2];
	pid_t pid;

	if (pipe(stdin_pipe) < 0) {
		perror("[llm_serv] pipe(stdin)");
		return FAIL;
	}
	if (pipe(stdout_pipe) < 0) {
		perror("[llm_serv] pipe(stdout)");
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		return FAIL;
	}

	pid = fork();
	if (pid < 0) {
		perror("[llm_serv] fork");
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		return FAIL;
	}

	if (pid == 0) {
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);

		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		dup2(stdout_pipe[1], STDERR_FILENO);

		close(stdin_pipe[0]);
		close(stdout_pipe[1]);

		if (work_dir && chdir(work_dir) < 0) {
			fprintf(stderr, "[llm_demo] chdir %s failed: %s\n", work_dir, strerror(errno));
			_exit(1);
		}

		execl(demo_path, demo_path, config_path, (char *)NULL);
		fprintf(stderr, "[llm_demo] execl %s failed: %s\n", demo_path, strerror(errno));
		_exit(127);
	}

	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	g_child_pid = pid;
	g_child_stdin_fd = stdin_pipe[1];
	g_child_stdout_fd = stdout_pipe[0];

	printf("[llm_serv] llm_demo started (pid=%d)\n", pid);
	printf("[llm_serv]   binary : %s\n", demo_path);
	printf("[llm_serv]   config : %s\n", config_path);
	if (work_dir)
		printf("[llm_serv]   workdir: %s\n", work_dir);

	return SUCCEED;
}

static void stop_llm_demo(void)
{
	if (g_child_stdin_fd >= 0) {
		close(g_child_stdin_fd);
		g_child_stdin_fd = -1;
	}
	if (g_child_stdout_fd >= 0) {
		close(g_child_stdout_fd);
		g_child_stdout_fd = -1;
	}
	if (g_child_pid > 0) {
		int i;
		kill(g_child_pid, SIGTERM);
		for (i = 0; i < 10; i++) {
			int status;
			pid_t ret = waitpid(g_child_pid, &status, WNOHANG);
			if (ret > 0) {
				g_child_pid = -1;
				return;
			}
			usleep(200000);
		}
		kill(g_child_pid, SIGKILL);
		waitpid(g_child_pid, NULL, 0);
		g_child_pid = -1;
	}
}

static void handle_client(NT_SOCKET client_fd, int verbose)
{
	struct pollfd fds[2];
	char buf[LLM_BUF_SIZE];
	ssize_t n;

	fds[0].fd = client_fd;
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	fds[1].fd = g_child_stdout_fd;
	fds[1].events = POLLIN;
	fds[1].revents = 0;

	printf("[llm_serv] client connected, fd=%d\n", client_fd);

	while (g_running) {
		int ret;

		fds[0].events = POLLIN;
		fds[0].revents = 0;
		fds[1].fd = g_child_stdout_fd;
		fds[1].events = POLLIN;
		fds[1].revents = 0;

		if (fds[1].fd < 0) {
			fprintf(stderr, "[llm_serv] llm_demo stdout closed\n");
			break;
		}

		ret = poll(fds, 2, POLL_TIMEOUT_MS);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("[llm_serv] poll");
			break;
		}
		if (ret == 0)
			continue;

		if (fds[0].revents & (POLLIN | POLLHUP)) {
			n = read(client_fd, buf, sizeof(buf) - 1);
			if (n <= 0) {
				if (verbose)
					printf("[llm_serv] client read returned %zd\n", n);
				break;
			}
			buf[n] = '\0';

			if (g_child_stdin_fd >= 0) {
				ssize_t written = 0;
				while (written < n) {
					ssize_t w = write(g_child_stdin_fd, buf + written, n - written);
					if (w < 0) {
						if (errno == EINTR)
							continue;
						perror("[llm_serv] write to llm_demo stdin");
						break;
					}
					written += w;
				}
				if (verbose)
					printf("[llm_serv] -> llm_demo: %.*s", (int)n, buf);
			}
		}

		if (fds[0].revents & (POLLERR | POLLNVAL)) {
			fprintf(stderr, "[llm_serv] client socket error\n");
			break;
		}

		if (fds[1].revents & POLLIN) {
			n = read(g_child_stdout_fd, buf, sizeof(buf) - 1);
			if (n <= 0) {
				if (verbose)
					printf("[llm_serv] llm_demo stdout returned %zd\n", n);
				if (n == 0)
					g_child_stdout_fd = -1;
				continue;
			}
			buf[n] = '\0';

			ssize_t written = 0;
			while (written < n) {
				ssize_t w = write(client_fd, buf + written, n - written);
				if (w < 0) {
					if (errno == EINTR)
						continue;
					perror("[llm_serv] write to client");
					break;
				}
				written += w;
			}
			if (verbose)
				printf("[llm_serv] <- llm_demo: %.*s", (int)n, buf);
		}

		if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
			if (fds[1].revents & POLLERR)
				fprintf(stderr, "[llm_serv] llm_demo stdout error\n");
			if (fds[1].revents & POLLHUP) {
				n = read(g_child_stdout_fd, buf, sizeof(buf) - 1);
				if (n > 0) {
					buf[n] = '\0';
					ssize_t written = 0;
					while (written < n) {
						ssize_t w = write(client_fd, buf + written, n - written);
						if (w < 0) {
							if (errno == EINTR)
								continue;
							break;
						}
						written += w;
					}
				}
			}
			if (fds[1].revents & POLLHUP) {
				g_child_stdout_fd = -1;
			}
		}
	}

	close(client_fd);
	printf("[llm_serv] client disconnected\n");
}

int main(int argc, char **argv)
{
	nt_socket_t listen_sock;
	unsigned short port = LLM_SERV_DEFAULT_PORT;
	const char *host = LLM_SERV_DEFAULT_HOST;
	const char *demo_path = DEFAULT_LLM_DEMO;
	const char *config_path = DEFAULT_MODEL_CONFIG;
	const char *work_dir = NULL;
	int verbose = 0;
	int ret, i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			port = (unsigned short)atoi(argv[++i]);
		} else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
			host = argv[++i];
		} else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
			demo_path = argv[++i];
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			config_path = argv[++i];
		} else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
			work_dir = argv[++i];
		} else if (strcmp(argv[i], "-v") == 0) {
			verbose = 1;
		} else if (strcmp(argv[i], "-help") == 0) {
			print_usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
	}

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGCHLD, sigchld_handler);

	ret = start_llm_demo(demo_path, config_path, work_dir);
	if (SUCCEED != ret) {
		fprintf(stderr, "[llm_serv] Failed to start llm_demo\n");
		return 1;
	}

	ret = nt_tcp_listen(&listen_sock, host, port);
	if (SUCCEED != ret) {
		fprintf(stderr, "[llm_serv] Listen failed: %s\n", nt_socket_strerror());
		stop_llm_demo();
		return 1;
	}

	printf("[llm_serv] listening on %s:%d\n", host, port);
	printf("[llm_serv] waiting for connections...\n");

	while (g_running) {
		ret = nt_tcp_accept(&listen_sock, NT_TCP_SEC_UNENCRYPTED);
		if (SUCCEED != ret) {
			if (!g_running)
				break;
			continue;
		}

		handle_client(listen_sock.socket, verbose);
		nt_tcp_unaccept(&listen_sock);

		if (g_child_pid < 0) {
			fprintf(stderr, "[llm_serv] llm_demo has exited, shutting down\n");
			break;
		}
	}

	printf("[llm_serv] shutting down...\n");
	stop_llm_demo();
	nt_tcp_close(&listen_sock);
	printf("[llm_serv] stopped\n");

	return 0;
}
