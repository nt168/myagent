#include "common.h"
#include "comms.h"
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#define CLI_BUF_SIZE		4096
#define CLI_POLL_TIMEOUT_MS	100

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [options] <host> [port]\n"
		"  -v             Verbose mode\n"
		"  -help          Show this help\n"
		"\n"
		"Examples:\n"
		"  %s 127.0.0.1 18001\n"
		"  %s localhost\n"
		"  %s -v 192.168.1.100 18001\n",
		prog, prog, prog, prog);
}

static void restore_terminal(struct termios *orig_termios)
{
	if (orig_termios)
		tcsetattr(STDIN_FILENO, TCSANOW, orig_termios);
}

int main(int argc, char **argv)
{
	nt_socket_t sock;
	struct pollfd fds[2];
	struct termios orig_termios, raw_termios;
	char buf[CLI_BUF_SIZE];
	char host[MAX_STRING_LEN] = "127.0.0.1";
	unsigned short port = 18001;
	int verbose = 0;
	int ret, i;
	int need_term_restore = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0) {
			verbose = 1;
		} else if (strcmp(argv[i], "-help") == 0) {
			print_usage(argv[0]);
			return 0;
		} else if (argv[i][0] != '-') {
			strscpy(host, argv[i]);
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				port = (unsigned short)atoi(argv[++i]);
			}
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
	}

	ret = nt_tcp_connect(&sock, NULL, host, port, 10, NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
	if (SUCCEED != ret) {
		fprintf(stderr, "Connect to %s:%d failed: %s\n", host, port, nt_socket_strerror());
		return 1;
	}

	printf("Connected to %s:%d\n", host, port);
	printf("Type your message and press Enter to send.\n");
	printf("  /quit  - disconnect\n");
	printf("  /exit  - disconnect\n");
	printf("-------------------------------------------\n");
	fflush(stdout);

	if (isatty(STDIN_FILENO)) {
		if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
			raw_termios = orig_termios;
			raw_termios.c_lflag &= ~ICANON;
			raw_termios.c_lflag |= ECHO;
			raw_termios.c_cc[VMIN] = 1;
			raw_termios.c_cc[VTIME] = 0;
			if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) == 0) {
				need_term_restore = 1;
			}
		}
	}

	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	fds[1].fd = sock.socket;
	fds[1].events = POLLIN;

	while (1) {
		fds[0].revents = 0;
		fds[1].revents = 0;

		ret = poll(fds, 2, CLI_POLL_TIMEOUT_MS);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		}
		if (ret == 0)
			continue;

		if (fds[0].revents & POLLIN) {
			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
			if (n <= 0)
				break;

			buf[n] = '\0';

			if (n == 1 && buf[0] == 3) {
				printf("\n");
				break;
			}

			if (need_term_restore) {
				if (n == 1 && buf[0] == '\r') {
					buf[0] = '\n';
				} else if (n >= 2 && buf[n - 1] == '\r') {
					buf[n - 1] = '\n';
				}
			}

			if (verbose)
				fprintf(stderr, "[cli] -> sending %zd bytes\n", n);

			ssize_t written = 0;
			while (written < n) {
				ssize_t w = write(sock.socket, buf + written, n - written);
				if (w < 0) {
					if (errno == EINTR)
						continue;
					perror("write to server");
					goto done;
				}
				written += w;
			}

			if (need_term_restore && buf[n - 1] == '\n') {
				ssize_t cmd_len = n - 1;
				if (cmd_len >= 5 && strncmp(buf, "/quit", 5) == 0)
					break;
				if (cmd_len >= 5 && strncmp(buf, "/exit", 5) == 0)
					break;
			}
		}

		if (fds[0].revents & (POLLHUP | POLLERR)) {
			break;
		}

		if (fds[1].revents & POLLIN) {
			ssize_t n = read(sock.socket, buf, sizeof(buf) - 1);
			if (n <= 0) {
				printf("\n[server disconnected]\n");
				break;
			}
			buf[n] = '\0';
			ssize_t _nw = 0;
			while (_nw < n) {
				ssize_t _w = write(STDOUT_FILENO, buf + _nw, n - _nw);
				if (_w < 0) {
					if (errno == EINTR)
						continue;
					break;
				}
				_nw += _w;
			}
			fflush(stdout);
		}

		if (fds[1].revents & (POLLHUP | POLLERR)) {
			printf("\n[server connection closed]\n");
			break;
		}
	}

done:
	if (need_term_restore)
		restore_terminal(&orig_termios);

	nt_tcp_close(&sock);
	printf("Disconnected.\n");
	return 0;
}
