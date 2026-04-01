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

#ifndef NT_COMMS_H
#define NT_COMMS_H

#include "common.h"

#define NT_TCP_WRITE(s, b, bl)	((ssize_t)write((s), (b), (bl)))
#define NT_TCP_READ(s, b, bl)	((ssize_t)read((s), (b), (bl)))
#define nt_socket_close(s)	if (NT_SOCKET_ERROR != (s)) close(s)
#define nt_socket_last_error()	errno

#define NT_PROTO_AGAIN		EINTR
#define NT_PROTO_ERROR		-1
#define NT_SOCKET_ERROR		-1
#define NT_SOCKET_TO_INT(s)	(s)

#define NT_CONST_STRLEN(str) (sizeof("" str) - 1)

typedef int	NT_SOCKET;

#define NT_SOCKADDR struct sockaddr_in

typedef enum
{
	NT_BUF_TYPE_STAT = 0,
	NT_BUF_TYPE_DYN
}
nt_buf_type_t;

#define NT_SOCKET_COUNT	256
#define NT_STAT_BUF_LEN	2048

typedef struct
{
	NT_SOCKET			socket;
	NT_SOCKET			socket_orig;
	size_t				read_bytes;
	char				*buffer;
	char				*next_line;
	unsigned int 			connection_type;
	int				timeout;
	nt_buf_type_t			buf_type;
	unsigned char			accepted;
	int				num_socks;
	NT_SOCKET			sockets[NT_SOCKET_COUNT];
	char				buf_stat[NT_STAT_BUF_LEN];
	NT_SOCKADDR			peer_info;
	char				peer[MAX_NT_DNSNAME_LEN + 1];
}
nt_socket_t;

const char	*nt_socket_strerror(void);

void nt_set_socket_strerror(const char *fmt, ...);
void nt_socket_clean(nt_socket_t *s);

void	nt_gethost_by_ip(const char *ip, char *host, size_t hostlen);

int	nt_tcp_connect(nt_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout,
		unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2);

#define NT_TCP_PROTOCOL		0x01
#define NT_TCP_COMPONENT_VERSION	0x02

#define NT_TCP_SEC_UNENCRYPTED		1
#define NT_TCP_SEC_TLS_PSK		2
#define NT_TCP_SEC_TLS_CERT		4
#define NT_TCP_SEC_UNENCRYPTED_TXT	"unencrypted"
#define NT_TCP_SEC_TLS_PSK_TXT		"psk"
#define NT_TCP_SEC_TLS_CERT_TXT	"cert"

const char	*nt_tcp_connection_type_name(unsigned int type);

#define nt_tcp_send(s, d)				nt_tcp_send_ext((s), (d), strlen(d), NT_TCP_PROTOCOL, 0)
#define nt_tcp_send_to(s, d, timeout)			nt_tcp_send_ext((s), (d), strlen(d), NT_TCP_PROTOCOL, timeout)
#define nt_tcp_send_bytes_to(s, d, len, timeout)	nt_tcp_send_ext((s), (d), len, NT_TCP_PROTOCOL, timeout)
#define nt_tcp_send_raw(s, d)				nt_tcp_send_ext((s), (d), strlen(d), 0, 0)

int	nt_tcp_send_ext(nt_socket_t *s, const char *data, size_t len, unsigned char flags, int timeout);

void	nt_tcp_close(nt_socket_t *s);

int	nt_tcp_listen(nt_socket_t *s, const char *listen_ip, unsigned short listen_port);

int	nt_tcp_accept(nt_socket_t *s, unsigned int tls_accept);
void	nt_tcp_unaccept(nt_socket_t *s);

#define NT_TCP_READ_UNTIL_CLOSE 0x01

#define SUCCEED_OR_FAIL(result) (FAIL != (result) ? SUCCEED : FAIL)

#define	nt_tcp_recv(s) 		SUCCEED_OR_FAIL(nt_tcp_recv_ext(s, 0, 0))
#define	nt_tcp_recv_to(s, timeout) 	SUCCEED_OR_FAIL(nt_tcp_recv_ext(s, 0, timeout))

ssize_t		nt_tcp_recv_ext(nt_socket_t *s, unsigned char flags, int timeout);
const char	*nt_tcp_recv_line(nt_socket_t *s);

int	nt_validate_peer_list(const char *peer_list, char **error);
int	nt_tcp_check_allowed_peers(nt_socket_t *s, const char *peer_list);

int	nt_udp_connect(nt_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout);
int	nt_udp_send(nt_socket_t *s, const char *data, size_t data_len, int timeout);
int	nt_udp_recv(nt_socket_t *s, int timeout);
void	nt_udp_close(nt_socket_t *s);

#define NT_DEFAULT_FTP_PORT		21
#define NT_DEFAULT_SSH_PORT		22
#define NT_DEFAULT_TELNET_PORT		23
#define NT_DEFAULT_SMTP_PORT		25
#define NT_DEFAULT_DNS_PORT		53
#define NT_DEFAULT_HTTP_PORT		80
#define NT_DEFAULT_POP_PORT		110
#define NT_DEFAULT_NNTP_PORT		119
#define NT_DEFAULT_NTP_PORT		123
#define NT_DEFAULT_IMAP_PORT		143
#define NT_DEFAULT_LDAP_PORT		389
#define NT_DEFAULT_HTTPS_PORT		443
#define NT_DEFAULT_AGENT_PORT		10050
#define NT_DEFAULT_SERVER_PORT		10051
#define NT_DEFAULT_GATEWAY_PORT	10052

#define NT_DEFAULT_AGENT_PORT_STR	"10050"
#define NT_DEFAULT_SERVER_PORT_STR	"10051"

int	nt_send_response_ext(nt_socket_t *sock, int result, const char *info, int protocol, int timeout);

#define nt_send_response(sock, result, info, timeout) \
		nt_send_response_ext(sock, result, info, NT_TCP_PROTOCOL, timeout)

#define nt_send_proxy_response(sock, result, info, timeout) \
		nt_send_response_ext(sock, result, info, NT_TCP_PROTOCOL | NT_TCP_COMPONENT_VERSION , timeout)

#define nt_send_response_raw(sock, result, info, timeout) \
		nt_send_response_ext(sock, result, info, 0, timeout)

int	nt_recv_response(nt_socket_t *sock, int timeout, char **error);

#endif