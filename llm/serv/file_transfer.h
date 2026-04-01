#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "common.h"
#include "comms.h"

#define FILE_TRANSFER_PORT 17000
#define MAX_FILENAME_LEN MAX_STRING_LEN
#define FILE_CHUNK_SIZE 8192
#define MAX_FILE_SIZE (10ULL * 1024 * 1024 * 1024)

typedef enum
{
    FILE_TRANS_OK = 0,
    FILE_TRANS_ERROR = -1,
    FILE_TRANS_FILE_NOT_FOUND = -2,
    FILE_TRANS_FILE_TOO_LARGE = -3,
    FILE_TRANS_PERMISSION_DENIED = -4,
    FILE_TRANS_NETWORK_ERROR = -5,
    FILE_TRANS_INVALID_FILENAME = -6,
    FILE_TRANS_DISK_FULL = -7
}
file_trans_result_t;

typedef struct
{
    char filename[MAX_FILENAME_LEN];
    nt_uint64_t file_size;
    nt_uint64_t bytes_transferred;
    int error_code;
    char error_msg[MAX_STRING_LEN];
}
file_transfer_info_t;

void file_trans_strerror(int error_code, char *buffer, size_t buf_len);

int file_send(nt_socket_t *sock, const char *filepath, int timeout);

int file_recv(nt_socket_t *sock, const char *save_dir, int timeout);

int file_server_start(unsigned short port, const char *save_dir);

int file_client_send(const char *server_ip, unsigned short port, const char *filepath, int timeout);

int file_client_recv(const char *server_ip, unsigned short port, const char *filename, const char *save_path, int timeout);

int file_transfer_init(nt_socket_t *sock);

void file_transfer_cleanup(nt_socket_t *sock);

int send_file_chunk(nt_socket_t *sock, const char *data, size_t len);

int recv_file_chunk(nt_socket_t *sock, char *buffer, size_t buf_size, size_t *bytes_read);

int send_file_header(nt_socket_t *sock, const char *filename, nt_uint64_t file_size);

int recv_file_header(nt_socket_t *sock, char *filename, size_t filename_size, nt_uint64_t *file_size);

int send_ack(nt_socket_t *sock, int result, const char *message);

int recv_ack(nt_socket_t *sock, int *result, char *message, size_t msg_size);

#endif
