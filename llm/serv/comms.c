/*
** Zabbix Network Socket Communication Library
** Extracted from Zabbix 3.4.7
** Copyright (C) 2001-2018 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#include "comms.h"

#define IPV4_MAX_CIDR_PREFIX	32
#define IPV6_MAX_CIDR_PREFIX	128

#ifndef NT_SOCKLEN_T
#define NT_SOCKLEN_T socklen_t
#endif

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

#define NT_SOCKET_STRERROR_LEN	512

static char	nt_socket_strerror_message[NT_SOCKET_STRERROR_LEN];

const char	*nt_socket_strerror(void)
{
	nt_socket_strerror_message[NT_SOCKET_STRERROR_LEN - 1] = '\0';
	return nt_socket_strerror_message;
}

void nt_set_socket_strerror(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    nt_vsnprintf(nt_socket_strerror_message, sizeof(nt_socket_strerror_message), fmt, args);
    va_end(args);
}

static int	nt_socket_peer_ip_save(nt_socket_t *s)
{
	NT_SOCKADDR	sa;
	NT_SOCKLEN_T	sz = sizeof(sa);
	const char	*error_message = NULL;

	if (NT_PROTO_ERROR == getpeername(s->socket, (struct sockaddr *)&sa, &sz))
	{
		error_message = strerror_from_system(nt_socket_last_error());
		nt_set_socket_strerror("connection rejected, getpeername() failed: %s", error_message);
		return FAIL;
	}

	memcpy(&s->peer_info, &sa, (size_t)sz);
	strscpy(s->peer, inet_ntoa(sa.sin_addr));

	return SUCCEED;
}

void	nt_gethost_by_ip(const char *ip, char *host, size_t hostlen)
{
	struct in_addr	addr;
	struct hostent	*hst;

	assert(ip);

	if (0 == inet_aton(ip, &addr))
	{
		host[0] = '\0';
		return;
	}

	if (NULL == (hst = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET)))
	{
		host[0] = '\0';
		return;
	}

	nt_strlcpy(host, hst->h_name, hostlen);
}

void	nt_socket_clean(nt_socket_t *s)
{
	memset(s, 0, sizeof(nt_socket_t));

	s->buf_type = NT_BUF_TYPE_STAT;
}

static void	nt_socket_free(nt_socket_t *s)
{
	if (NT_BUF_TYPE_DYN == s->buf_type)
		nt_free(s->buffer);
}

static void	nt_socket_timeout_set(nt_socket_t *s, int timeout)
{
	s->timeout = timeout;
	nt_alarm_on(timeout);
}

static void	nt_socket_timeout_cleanup(nt_socket_t *s)
{
	if (0 != s->timeout)
	{
		nt_alarm_off();
		s->timeout = 0;
	}
}

static int	nt_socket_connect(nt_socket_t *s, const struct sockaddr *addr, socklen_t addrlen, int timeout,
		char **error)
{
	if (0 != timeout)
		nt_socket_timeout_set(s, timeout);

	if (NT_PROTO_ERROR == connect(s->socket, addr, addrlen))
	{
		*error = nt_strdup(*error, strerror_from_system(nt_socket_last_error()));
		return FAIL;
	}

	s->connection_type = NT_TCP_SEC_UNENCRYPTED;

	return SUCCEED;
}

static int	nt_socket_create(nt_socket_t *s, int type, const char *source_ip, const char *ip, unsigned short port,
		int timeout, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2)
{
	NT_SOCKADDR	servaddr_in;
	struct hostent	*hp;
	char		*error = NULL;
	void		(*func_socket_close)(nt_socket_t *s);

	if (SOCK_DGRAM == type && (NT_TCP_SEC_TLS_CERT == tls_connect || NT_TCP_SEC_TLS_PSK == tls_connect))
	{
		nt_set_socket_strerror("TLS not supported for UDP sockets");
		return FAIL;
	}

	if (NT_TCP_SEC_TLS_CERT == tls_connect || NT_TCP_SEC_TLS_PSK == tls_connect)
	{
		nt_set_socket_strerror("support for TLS was not compiled in");
		return FAIL;
	}

	NT_UNUSED(tls_arg1);
	NT_UNUSED(tls_arg2);

	nt_socket_clean(s);

	if (NULL == (hp = gethostbyname(ip)))
	{
		nt_set_socket_strerror("gethostbyname() failed for '%s': [%d]", ip, h_errno);
		return FAIL;
	}

	servaddr_in.sin_family = AF_INET;
	servaddr_in.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
	servaddr_in.sin_port = htons(port);

	if (NT_SOCKET_ERROR == (s->socket = socket(AF_INET, type | SOCK_CLOEXEC, 0)))
	{
		nt_set_socket_strerror("cannot create socket [[%s]:%hu]: %s",
				ip, port, strerror_from_system(nt_socket_last_error()));
		return FAIL;
	}

#if !SOCK_CLOEXEC
	fcntl(s->socket, F_SETFD, FD_CLOEXEC);
#endif

	func_socket_close = (SOCK_STREAM == type ? nt_tcp_close : nt_udp_close);

	if (NULL != source_ip)
	{
		NT_SOCKADDR	source_addr;

		memset(&source_addr, 0, sizeof(source_addr));

		source_addr.sin_family = AF_INET;
		source_addr.sin_addr.s_addr = inet_addr(source_ip);
		source_addr.sin_port = 0;

		if (NT_PROTO_ERROR == bind(s->socket, (struct sockaddr *)&source_addr, sizeof(source_addr)))
		{
			nt_set_socket_strerror("bind() failed: %s", strerror_from_system(nt_socket_last_error()));
			func_socket_close(s);
			return FAIL;
		}
	}

	if (SUCCEED != nt_socket_connect(s, (struct sockaddr *)&servaddr_in, sizeof(servaddr_in), timeout, &error))
	{
		func_socket_close(s);
		nt_set_socket_strerror("cannot connect to [[%s]:%hu]: %s", ip, port, error);
		nt_free(error);
		return FAIL;
	}

	nt_strlcpy(s->peer, ip, sizeof(s->peer));

	return SUCCEED;
}

int	nt_tcp_connect(nt_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout,
		unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2)
{
	if (NT_TCP_SEC_UNENCRYPTED != tls_connect && NT_TCP_SEC_TLS_CERT != tls_connect &&
			NT_TCP_SEC_TLS_PSK != tls_connect)
	{
		nt_set_socket_strerror("invalid tls_connect mode");
		return FAIL;
	}

	return nt_socket_create(s, SOCK_STREAM, source_ip, ip, port, timeout, tls_connect, tls_arg1, tls_arg2);
}

static ssize_t	nt_tcp_write(nt_socket_t *s, const char *buf, size_t len)
{
	ssize_t	res;
	int	err;

	do
	{
		res = NT_TCP_WRITE(s->socket, buf, len);
		if (SUCCEED == nt_alarm_timed_out())
		{
			nt_set_socket_strerror("NT_TCP_WRITE() timed out");
			return NT_PROTO_ERROR;
		}
	}
	while (NT_PROTO_ERROR == res && NT_PROTO_AGAIN == (err = nt_socket_last_error()));

	if (NT_PROTO_ERROR == res)
		nt_set_socket_strerror("NT_TCP_WRITE() failed: %s", strerror_from_system(err));

	return res;
}

#define NT_TCP_HEADER_DATA	"NTD"
#define NT_TCP_HEADER_VERSION	"\1"
#define NT_TCP_HEADER		NT_TCP_HEADER_DATA NT_TCP_HEADER_VERSION
#define NT_TCP_HEADER_LEN	5

int	nt_tcp_send_ext(nt_socket_t *s, const char *data, size_t len, unsigned char flags, int timeout)
{
#define NT_TLS_MAX_REC_LEN	16384

	nt_uint64_t	len64_le;
	ssize_t		bytes_sent = 0, written = 0;
	size_t		send_bytes;
	int		ret = SUCCEED;

	if (0 != timeout)
		nt_socket_timeout_set(s, timeout);

	if (0 != (flags & NT_TCP_PROTOCOL))
	{
		size_t	take_bytes;
		char	header_buf[NT_TLS_MAX_REC_LEN];

		memcpy(header_buf, NT_TCP_HEADER, (size_t)NT_TCP_HEADER_LEN);

		len64_le = nt_htole_uint64((nt_uint64_t)len);
		memcpy(header_buf + NT_TCP_HEADER_LEN, &len64_le, sizeof(len64_le));

		take_bytes = MIN(len, NT_TLS_MAX_REC_LEN - NT_TCP_HEADER_LEN - sizeof(len64_le));
		memcpy(header_buf + NT_TCP_HEADER_LEN + sizeof(len64_le), data, take_bytes);

		send_bytes = NT_TCP_HEADER_LEN + sizeof(len64_le) + take_bytes;

		while (written < (ssize_t)send_bytes)
		{
			if (NT_PROTO_ERROR == (bytes_sent = nt_tcp_write(s, header_buf + written,
					send_bytes - (size_t)written)))
			{
				ret = FAIL;
				goto cleanup;
			}
			written += bytes_sent;
		}

		written -= NT_TCP_HEADER_LEN + (ssize_t)sizeof(len64_le);
	}

	while (written < (ssize_t)len)
	{
		if (NT_TCP_SEC_UNENCRYPTED == s->connection_type)
			send_bytes = len - (size_t)written;
		else
			send_bytes = MIN(NT_TLS_MAX_REC_LEN, len - (size_t)written);

		if (NT_PROTO_ERROR == (bytes_sent = nt_tcp_write(s, data + written, send_bytes)))
		{
			ret = FAIL;
			goto cleanup;
		}
		written += bytes_sent;
	}
cleanup:
	if (0 != timeout)
		nt_socket_timeout_cleanup(s);

	return ret;

#undef NT_TLS_MAX_REC_LEN
}

void	nt_tcp_close(nt_socket_t *s)
{
	nt_tcp_unaccept(s);

	nt_socket_timeout_cleanup(s);

	nt_socket_free(s);
	nt_socket_close(s->socket);
}

int	nt_tcp_listen(nt_socket_t *s, const char *listen_ip, unsigned short listen_port)
{
	NT_SOCKADDR	serv_addr;
	char		*ip, *ips, *delim;
	int		i, on, ret = FAIL;

	nt_socket_clean(s);

	ip = ips = (NULL == listen_ip ? NULL : strdup(listen_ip));

	while (1)
	{
		delim = (NULL == ip ? NULL : strchr(ip, ','));
		if (NULL != delim)
			*delim = '\0';

		if (NULL != ip && FAIL == is_ip4(ip))
		{
			nt_set_socket_strerror("incorrect IPv4 address [%s]", ip);
			goto out;
		}

		if (NT_SOCKET_COUNT == s->num_socks)
		{
			nt_set_socket_strerror("not enough space for socket [[%s]:%hu]",
					ip ? ip : "-", listen_port);
			goto out;
		}

		if (NT_SOCKET_ERROR == (s->sockets[s->num_socks] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)))
		{
			nt_set_socket_strerror("socket() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(nt_socket_last_error()));
			goto out;
		}

#if !SOCK_CLOEXEC
		fcntl(s->sockets[s->num_socks], F_SETFD, FD_CLOEXEC);
#endif

		on = 1;
		if (NT_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], SOL_SOCKET, SO_REUSEADDR,
				(void *)&on, sizeof(on)))
		{
			nt_set_socket_strerror("setsockopt() with %s for [[%s]:%hu] failed: %s", "SO_REUSEADDR",
					ip ? ip : "-", listen_port, strerror_from_system(nt_socket_last_error()));
		}

		memset(&serv_addr, 0, sizeof(serv_addr));

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = (NULL != ip ? inet_addr(ip) : htonl(INADDR_ANY));
		serv_addr.sin_port = htons((unsigned short)listen_port);

		if (NT_PROTO_ERROR == bind(s->sockets[s->num_socks], (struct sockaddr *)&serv_addr, sizeof(serv_addr)))
		{
			nt_set_socket_strerror("bind() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(nt_socket_last_error()));
			nt_socket_close(s->sockets[s->num_socks]);
			if (EADDRINUSE == nt_socket_last_error())
				continue;
			else
				goto out;
		}

		if (NT_PROTO_ERROR == listen(s->sockets[s->num_socks], SOMAXCONN))
		{
			nt_set_socket_strerror("listen() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(nt_socket_last_error()));
			nt_socket_close(s->sockets[s->num_socks]);
			goto out;
		}

		s->num_socks++;

		if (NULL == ip || NULL == delim)
			break;
		*delim = ',';
		ip = delim + 1;
	}

	if (0 == s->num_socks)
	{
		nt_set_socket_strerror("nt_tcp_listen() fatal error: unable to serve on any address [[%s]:%hu]",
				listen_ip ? listen_ip : "-", listen_port);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != ips)
		nt_free(ips);

	if (SUCCEED != ret)
	{
		for (i = 0; i < s->num_socks; i++)
			nt_socket_close(s->sockets[i]);
	}

	return ret;
}

int	nt_tcp_accept(nt_socket_t *s, unsigned int tls_accept)
{
	NT_SOCKADDR	serv_addr;
	fd_set		sock_set;
	NT_SOCKET	accepted_socket;
	NT_SOCKLEN_T	nlen;
	int		i, n = 0, ret = FAIL;
	ssize_t		res;
	unsigned char	buf;

	nt_tcp_unaccept(s);

	FD_ZERO(&sock_set);

	for (i = 0; i < s->num_socks; i++)
	{
		FD_SET(s->sockets[i], &sock_set);
		if (s->sockets[i] > n)
			n = s->sockets[i];
	}

	if (NT_PROTO_ERROR == select(n + 1, &sock_set, NULL, NULL, NULL))
	{
		nt_set_socket_strerror("select() failed: %s", strerror_from_system(nt_socket_last_error()));
		return ret;
	}

	for (i = 0; i < s->num_socks; i++)
	{
		if (FD_ISSET(s->sockets[i], &sock_set))
			break;
	}

	nlen = sizeof(serv_addr);
	if (NT_SOCKET_ERROR == (accepted_socket = (NT_SOCKET)accept(s->sockets[i], (struct sockaddr *)&serv_addr,
			&nlen)))
	{
		nt_set_socket_strerror("accept() failed: %s", strerror_from_system(nt_socket_last_error()));
		return ret;
	}

	s->socket_orig = s->socket;
	s->socket = accepted_socket;
	s->accepted = 1;

	if (SUCCEED != nt_socket_peer_ip_save(s))
	{
		nt_tcp_unaccept(s);
		goto out;
	}

	nt_socket_timeout_set(s, CONFIG_TIMEOUT);

	if (NT_SOCKET_ERROR == (res = recv(s->socket, &buf, 1, MSG_PEEK)))
	{
		nt_set_socket_strerror("from %s: reading first byte from connection failed: %s", s->peer,
				strerror_from_system(nt_socket_last_error()));
		nt_tcp_unaccept(s);
		goto out;
	}

	if (0 == (tls_accept & NT_TCP_SEC_UNENCRYPTED))
	{
		nt_set_socket_strerror("from %s: unencrypted connections are not allowed", s->peer);
		nt_tcp_unaccept(s);
		goto out;
	}

	s->connection_type = NT_TCP_SEC_UNENCRYPTED;

	ret = SUCCEED;
out:
	nt_socket_timeout_cleanup(s);

	return ret;
}

void	nt_tcp_unaccept(nt_socket_t *s)
{
	if (!s->accepted) return;

	shutdown(s->socket, 2);

	nt_socket_close(s->socket);

	s->socket = s->socket_orig;
	s->socket_orig = NT_SOCKET_ERROR;
	s->accepted = 0;
}

static const char	*nt_socket_find_line(nt_socket_t *s)
{
	char	*ptr, *line = NULL;

	if (NULL == s->next_line)
		return NULL;

	if ((size_t)(s->next_line - s->buffer) <= s->read_bytes && NULL != (ptr = strchr(s->next_line, '\n')))
	{
		line = s->next_line;
		s->next_line = ptr + 1;

		if (ptr > line && '\r' == *(ptr - 1))
			ptr--;

		*ptr = '\0';
	}

	return line;
}

const char	*nt_tcp_recv_line(nt_socket_t *s)
{
#define NT_TCP_LINE_LEN	(64 * NT_KIBIBYTE)

	char		buffer[NT_STAT_BUF_LEN], *ptr = NULL;
	const char	*line;
	ssize_t		nbytes;
	size_t		alloc = 0, offset = 0, line_length, left;

	if (NULL != (line = nt_socket_find_line(s)))
		return line;

	if (NULL != s->next_line)
	{
		left = s->read_bytes - (s->next_line - s->buffer);
		memmove(s->buf_stat, s->next_line, left);
	}
	else
		left = 0;

	s->read_bytes = left;
	s->next_line = s->buf_stat;

	nt_socket_free(s);
	s->buf_type = NT_BUF_TYPE_STAT;
	s->buffer = s->buf_stat;

	if (NT_PROTO_ERROR == (nbytes = NT_TCP_READ(s->socket, s->buf_stat + left, NT_STAT_BUF_LEN - left - 1)))
		goto out;

	s->buf_stat[left + nbytes] = '\0';

	if (0 == nbytes)
	{
		line = 0 != s->read_bytes ? s->next_line : NULL;
		s->next_line += s->read_bytes;

		goto out;
	}

	s->read_bytes += nbytes;

	if (NULL != (line = nt_socket_find_line(s)))
		goto out;

	s->buf_type = NT_BUF_TYPE_DYN;
	s->buffer = NULL;
	nt_strncpy_alloc(&s->buffer, &alloc, &offset, s->buf_stat, s->read_bytes);
	line_length = s->read_bytes;

	do
	{
		if (NT_PROTO_ERROR == (nbytes = NT_TCP_READ(s->socket, buffer, NT_STAT_BUF_LEN - 1)))
			goto out;

		if (0 == nbytes)
		{
			line = 0 != s->read_bytes ? s->buffer : NULL;
			s->next_line = s->buffer + s->read_bytes;

			goto out;
		}

		buffer[nbytes] = '\0';
		ptr = strchr(buffer, '\n');

		if (s->read_bytes + nbytes < NT_TCP_LINE_LEN && s->read_bytes == line_length)
		{
			nt_strncpy_alloc(&s->buffer, &alloc, &offset, buffer, nbytes);
			s->read_bytes += nbytes;
		}
		else
		{
			if (0 != (left = MIN(NT_TCP_LINE_LEN - s->read_bytes, (size_t)(ptr - buffer))))
			{
				nt_strncpy_alloc(&s->buffer, &alloc, &offset, buffer, left);
				s->read_bytes += left;
			}

			if (NULL != ptr)
			{
				nt_strncpy_alloc(&s->buffer, &alloc, &offset, ptr, nbytes - (ptr - buffer));
				s->read_bytes += nbytes - (ptr - buffer);
			}
		}

		line_length += nbytes;

	}
	while (NULL == ptr);

	s->next_line = s->buffer;
	line = nt_socket_find_line(s);
out:
	return line;

#undef NT_TCP_LINE_LEN
}

static ssize_t	nt_tcp_read(nt_socket_t *s, char *buf, size_t len)
{
	ssize_t	res;
	int	err;

	do
	{
		res = NT_TCP_READ(s->socket, buf, len);
		if (SUCCEED == nt_alarm_timed_out())
		{
			nt_set_socket_strerror("NT_TCP_READ() timed out");
			return NT_PROTO_ERROR;
		}
	}
	while (NT_PROTO_ERROR == res && NT_PROTO_AGAIN == (err = nt_socket_last_error()));

	if (NT_PROTO_ERROR == res)
		nt_set_socket_strerror("NT_TCP_READ() failed: %s", strerror_from_system(err));

	return res;
}

ssize_t	nt_tcp_recv_ext(nt_socket_t *s, unsigned char flags, int timeout)
{
#define NT_TCP_EXPECT_HEADER	1
#define NT_TCP_EXPECT_LENGTH	2
#define NT_TCP_EXPECT_TEXT_XML	3
#define NT_TCP_EXPECT_SIZE	4
#define NT_TCP_EXPECT_CLOSE	5
#define NT_TCP_EXPECT_XML_END	6

	ssize_t		nbytes;
	size_t		allocated = 8 * NT_STAT_BUF_LEN, buf_dyn_bytes = 0, buf_stat_bytes = 0, header_bytes = 0;
	nt_uint64_t	expected_len = 16 * NT_MEBIBYTE;
	unsigned char	expect = NT_TCP_EXPECT_HEADER;

	if (0 != timeout)
		nt_socket_timeout_set(s, timeout);

	nt_socket_free(s);

	s->buf_type = NT_BUF_TYPE_STAT;
	s->buffer = s->buf_stat;

	while (0 != (nbytes = nt_tcp_read(s, s->buf_stat + buf_stat_bytes, sizeof(s->buf_stat) - buf_stat_bytes)))
	{
		if (NT_PROTO_ERROR == nbytes)
			goto out;

		if (NT_BUF_TYPE_STAT == s->buf_type)
			buf_stat_bytes += nbytes;
		else
			nt_strncpy_alloc(&s->buffer, &allocated, &buf_dyn_bytes, s->buf_stat, nbytes);

		if (buf_stat_bytes + buf_dyn_bytes >= expected_len)
			break;

		if (NT_TCP_EXPECT_SIZE == expect || (NT_TCP_EXPECT_CLOSE == expect && NT_BUF_TYPE_DYN == s->buf_type))
			continue;

		if (NT_TCP_EXPECT_HEADER == expect)
		{
			if (NT_TCP_HEADER_LEN > buf_stat_bytes)
			{
				if (0 == strncmp(s->buf_stat, NT_TCP_HEADER, buf_stat_bytes))
					continue;

				expect = NT_TCP_EXPECT_TEXT_XML;
			}
			else
			{
				if (0 == strncmp(s->buf_stat, NT_TCP_HEADER, NT_TCP_HEADER_LEN))
					expect = NT_TCP_EXPECT_LENGTH;
				else
					expect = NT_TCP_EXPECT_TEXT_XML;
			}
		}

		if (NT_TCP_EXPECT_LENGTH == expect)
		{
			if (NT_TCP_HEADER_LEN + sizeof(nt_uint64_t) > buf_stat_bytes)
				continue;

			memcpy(&expected_len, s->buf_stat + NT_TCP_HEADER_LEN, sizeof(nt_uint64_t));
			expected_len = nt_letoh_uint64(expected_len);

			if (NT_MAX_RECV_DATA_SIZE < expected_len)
			{
				nt_set_socket_strerror("Message size " NT_FS_UI64 " from %s exceeds the "
						"maximum size " NT_FS_UI64 " bytes.", expected_len,
						s->peer, (nt_uint64_t)NT_MAX_RECV_DATA_SIZE);
				nbytes = NT_PROTO_ERROR;
				goto out;
			}

			if (sizeof(s->buf_stat) > expected_len)
			{
				buf_stat_bytes -= NT_TCP_HEADER_LEN + sizeof(nt_uint64_t);
				memmove(s->buf_stat, s->buf_stat + NT_TCP_HEADER_LEN + sizeof(nt_uint64_t),
						buf_stat_bytes);
			}
			else
			{
				s->buf_type = NT_BUF_TYPE_DYN;
				s->buffer = nt_malloc(NULL, allocated);
				buf_dyn_bytes = buf_stat_bytes - NT_TCP_HEADER_LEN - sizeof(nt_uint64_t);
				buf_stat_bytes = 0;
				memcpy(s->buffer, s->buf_stat + NT_TCP_HEADER_LEN + sizeof(nt_uint64_t),
						buf_dyn_bytes);
			}

			expect = NT_TCP_EXPECT_SIZE;
			header_bytes = NT_TCP_HEADER_LEN + sizeof(nt_uint64_t);

			if (buf_stat_bytes + buf_dyn_bytes >= expected_len)
				break;

			continue;
		}

		if (sizeof(s->buf_stat) == buf_stat_bytes)
		{
			s->buf_type = NT_BUF_TYPE_DYN;
			s->buffer = nt_malloc(NULL, allocated);
			buf_dyn_bytes = sizeof(s->buf_stat);
			buf_stat_bytes = 0;
			memcpy(s->buffer, s->buf_stat, sizeof(s->buf_stat));
			continue;
		}

		if (sizeof(s->buf_stat) == nbytes)
			continue;

		if (NT_TCP_EXPECT_TEXT_XML == expect)
		{
			if (0 != (flags & NT_TCP_READ_UNTIL_CLOSE))
			{
				expect = NT_TCP_EXPECT_CLOSE;
				continue;
			}

			if (NT_CONST_STRLEN("<req>") > buf_stat_bytes + buf_dyn_bytes)
			{
				if (0 != strncmp(s->buffer, "<req>", buf_stat_bytes + buf_dyn_bytes))
					break;

				continue;
			}
			else
			{
				if (0 != strncmp(s->buffer, "<req>", NT_CONST_STRLEN("<req>")))
					break;

				expect = NT_TCP_EXPECT_XML_END;
			}
		}

		if (NT_TCP_EXPECT_XML_END == expect)
		{
			s->buffer[buf_stat_bytes + buf_dyn_bytes] = '\0';
			if (NULL != strstr(s->buffer + buf_stat_bytes + buf_dyn_bytes - (10 > buf_stat_bytes +
					buf_dyn_bytes ? buf_stat_bytes + buf_dyn_bytes : 10), "</req>"))
			{
				break;
			}
		}
	}

	if (NT_TCP_EXPECT_SIZE == expect)
	{
		if (buf_stat_bytes + buf_dyn_bytes != expected_len)
		{
			nt_set_socket_strerror("Message from %s size mismatch", s->peer);
			nbytes = NT_PROTO_ERROR;
			goto out;
		}
	}
	else if (buf_stat_bytes + buf_dyn_bytes >= expected_len)
	{
		nt_set_socket_strerror("Message from %s is too long", s->peer);
		nbytes = NT_PROTO_ERROR;
		goto out;
	}

	s->read_bytes = buf_stat_bytes + buf_dyn_bytes;
	s->buffer[s->read_bytes] = '\0';
out:
	if (0 != timeout)
		nt_socket_timeout_cleanup(s);

	return (NT_PROTO_ERROR == nbytes ? FAIL : (ssize_t)(s->read_bytes + header_bytes));

#undef NT_TCP_EXPECT_HEADER
#undef NT_TCP_EXPECT_LENGTH
#undef NT_TCP_EXPECT_TEXT_XML
#undef NT_TCP_EXPECT_SIZE
#undef NT_TCP_EXPECT_CLOSE
#undef NT_TCP_EXPECT_XML_END
}

static int	subnet_match(int af, unsigned int prefix_size, void *address1, void *address2)
{
	unsigned char	netmask[16] = {0};
	int		i, j, bytes;

	if (af == AF_INET)
	{
		if (prefix_size > IPV4_MAX_CIDR_PREFIX)
			return FAIL;
		bytes = 4;
	}
	else
	{
		if (prefix_size > IPV6_MAX_CIDR_PREFIX)
			return FAIL;
		bytes = 16;
	}

	for (i = (int)prefix_size, j = 0; i > 0 && j < bytes; i -= 8, j++)
		netmask[j] = i >= 8 ? 0xFF : ~((1 << (8 - i)) - 1);

	for (i = 0; i < bytes; i++)
	{
		if ((((unsigned char *)address1)[i] & netmask[i]) != (((unsigned char *)address2)[i] & netmask[i]))
			return FAIL;
	}

	return SUCCEED;
}

static int	validate_cidr(const char *ip, const char *cidr, void *value)
{
	if (SUCCEED == is_ip4(ip))
		return is_uint_range(cidr, value, 0, IPV4_MAX_CIDR_PREFIX);
	return FAIL;
}

int	nt_validate_peer_list(const char *peer_list, char **error)
{
	char	*start, *end, *cidr_sep;
	char	tmp[MAX_STRING_LEN];

	strscpy(tmp, peer_list);

	for (start = tmp; '\0' != *start;)
	{
		if (NULL != (end = strchr(start, ',')))
			*end = '\0';

		if (NULL != (cidr_sep = strchr(start, '/')))
		{
			*cidr_sep = '\0';

			if (FAIL == validate_cidr(start, cidr_sep + 1, NULL))
			{
				*cidr_sep = '/';
				*error = nt_dsprintf(NULL, "\"%s\"", start);
				return FAIL;
			}
		}
		else if (FAIL == is_supported_ip(start) && FAIL == nt_validate_hostname(start))
		{
			*error = nt_dsprintf(NULL, "\"%s\"", start);
			return FAIL;
		}

		if (NULL != end)
			start = end + 1;
		else
			break;
	}

	return SUCCEED;
}

int	nt_tcp_check_allowed_peers(nt_socket_t *s, const char *peer_list)
{
	char	*start = NULL, *end = NULL, *cidr_sep, tmp[MAX_STRING_LEN];
	unsigned int	prefix_size = IPV4_MAX_CIDR_PREFIX;
	struct hostent	*hp;

	strscpy(tmp, peer_list);

	for (start = tmp; '\0' != *start;)
	{
		if (NULL != (end = strchr(start, ',')))
			*end = '\0';

		if (NULL != (cidr_sep = strchr(start, '/')))
		{
			*cidr_sep = '\0';

			if (SUCCEED != validate_cidr(start, cidr_sep + 1, &prefix_size))
				*cidr_sep = '/';
		}

		if (NULL != (hp = gethostbyname(start)))
		{
			int	i;

			for (i = 0; NULL != hp->h_addr_list[i]; i++)
			{
				if (SUCCEED == subnet_match(AF_INET, prefix_size,
						&((struct in_addr *)hp->h_addr_list[i])->s_addr,
						&s->peer_info.sin_addr.s_addr))
				{
					return SUCCEED;
				}
			}
		}
		if (NULL != end)
			start = end + 1;
		else
			break;
	}

	nt_set_socket_strerror("connection from \"%s\" rejected, allowed hosts: \"%s\"", s->peer, peer_list);

	return FAIL;
}

const char	*nt_tcp_connection_type_name(unsigned int type)
{
	switch (type)
	{
		case NT_TCP_SEC_UNENCRYPTED:
			return "unencrypted";
		case NT_TCP_SEC_TLS_CERT:
			return "TLS with certificate";
		case NT_TCP_SEC_TLS_PSK:
			return "TLS with PSK";
		default:
			return "unknown";
	}
}

int	nt_udp_connect(nt_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout)
{
	return nt_socket_create(s, SOCK_DGRAM, source_ip, ip, port, timeout, NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
}

int	nt_udp_send(nt_socket_t *s, const char *data, size_t data_len, int timeout)
{
	int	ret = SUCCEED;

	if (0 != timeout)
		nt_socket_timeout_set(s, timeout);

	if (NT_PROTO_ERROR == sendto(s->socket, data, data_len, 0, NULL, 0))
	{
		nt_set_socket_strerror("sendto() failed: %s", strerror_from_system(nt_socket_last_error()));
		ret = FAIL;
	}

	if (0 != timeout)
		nt_socket_timeout_cleanup(s);

	return ret;
}

int	nt_udp_recv(nt_socket_t *s, int timeout)
{
	char	buffer[65508];
	ssize_t	read_bytes;

	nt_socket_free(s);

	if (0 != timeout)
		nt_socket_timeout_set(s, timeout);

	if (NT_PROTO_ERROR == (read_bytes = recvfrom(s->socket, buffer, sizeof(buffer) - 1, 0, NULL, NULL)))
		nt_set_socket_strerror("recvfrom() failed: %s", strerror_from_system(nt_socket_last_error()));

	if (0 != timeout)
		nt_socket_timeout_cleanup(s);

	if (NT_PROTO_ERROR == read_bytes)
		return FAIL;

	if (sizeof(s->buf_stat) > (size_t)read_bytes)
	{
		s->buf_type = NT_BUF_TYPE_STAT;
		s->buffer = s->buf_stat;
	}
	else
	{
		s->buf_type = NT_BUF_TYPE_DYN;
		s->buffer = nt_malloc(s->buffer, read_bytes + 1);
	}

	buffer[read_bytes] = '\0';
	memcpy(s->buffer, buffer, read_bytes + 1);

	s->read_bytes = (size_t)read_bytes;

	return SUCCEED;
}

void	nt_udp_close(nt_socket_t *s)
{
	nt_socket_timeout_cleanup(s);

	nt_socket_free(s);
	nt_socket_close(s->socket);
}

int	nt_send_response_ext(nt_socket_t *sock, int result, const char *info, int protocol, int timeout)
{
	char	*response = NULL;
	int	ret = SUCCEED;

	if (0 != (protocol & NT_TCP_COMPONENT_VERSION))
		response = nt_dsprintf(NULL, "{\"response\":\"%s\",\"info\":\"%s\"}\n",
				(0 == result ? "sent" : "failed"), info);
	else if (0 != (protocol & NT_TCP_PROTOCOL))
		response = nt_dsprintf(NULL, "{\"response\":\"%s\",\"info\":\"%s\"}\n",
				(0 == result ? "success" : "failed"), info);
	else
		response = nt_dsprintf(NULL, "%s\n", (0 == result ? "OK" : info));

	if (FAIL == nt_tcp_send_ext(sock, response, strlen(response), protocol, timeout))
		ret = FAIL;

	nt_free(response);

	return ret;
}

int	nt_recv_response(nt_socket_t *sock, int timeout, char **error)
{
	ssize_t	received;

	if (FAIL == (received = nt_tcp_recv_ext(sock, 0, timeout)))
	{
		*error = nt_strdup(*error, nt_socket_strerror());
		return FAIL;
	}

	if (0 == strncmp(sock->buffer, "OK", 2))
		return SUCCEED;

	*error = nt_strdup(*error, sock->buffer);
	return FAIL;
}