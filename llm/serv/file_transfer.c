#include "file_transfer.h"
#include <sys/stat.h>
#include <dirent.h>

static int send_raw_bytes(nt_socket_t *sock, const char *buf, size_t len)
{
    size_t total = 0;
    ssize_t nbytes;
    
    while (total < len) {
        nbytes = write(sock->socket, buf + total, len - total);
        if (nbytes <= 0) {
            return FILE_TRANS_NETWORK_ERROR;
        }
        total += nbytes;
    }
    
    return FILE_TRANS_OK;
}

static int recv_raw_bytes(nt_socket_t *sock, char *buf, size_t len)
{
    size_t total = 0;
    ssize_t nbytes;
    
    while (total < len) {
        nbytes = read(sock->socket, buf + total, len - total);
        if (nbytes <= 0) {
            return FILE_TRANS_NETWORK_ERROR;
        }
        total += nbytes;
    }
    
    return FILE_TRANS_OK;
}

static const char *file_trans_error_messages[] = {
    "Success",
    "General error",
    "File not found",
    "File too large",
    "Permission denied",
    "Network error",
    "Invalid filename",
    "Disk full"
};

void file_trans_strerror(int error_code, char *buffer, size_t buf_len)
{
    int idx = -error_code;
    if (idx >= 0 && idx < (int)(sizeof(file_trans_error_messages) / sizeof(char *))) {
        nt_strlcpy(buffer, file_trans_error_messages[idx], buf_len);
    } else {
        nt_strlcpy(buffer, "Unknown error", buf_len);
    }
}

int send_file_header(nt_socket_t *sock, const char *filename, nt_uint64_t file_size)
{
    char header[MAX_STRING_LEN + 16];
    size_t filename_len;
    nt_uint32_t filename_len_net;
    nt_uint64_t file_size_net;
    int ret;
    
    if (NULL == filename || '\0' == *filename) {
        return FILE_TRANS_INVALID_FILENAME;
    }
    
    filename_len = strlen(filename);
    if (filename_len >= MAX_FILENAME_LEN) {
        return FILE_TRANS_INVALID_FILENAME;
    }
    
    memset(header, 0, sizeof(header));
    
    filename_len_net = htonl((nt_uint32_t)filename_len);
    memcpy(header, &filename_len_net, sizeof(filename_len_net));
    
    memcpy(header + sizeof(filename_len_net), filename, filename_len);
    
    file_size_net = nt_htole_uint64(file_size);
    memcpy(header + sizeof(filename_len_net) + MAX_FILENAME_LEN, &file_size_net, sizeof(file_size_net));
    
    ret = send_raw_bytes(sock, header, sizeof(filename_len_net) + MAX_FILENAME_LEN + sizeof(file_size_net));
    
    return (FILE_TRANS_OK == ret) ? FILE_TRANS_OK : FILE_TRANS_NETWORK_ERROR;
}

int recv_file_header(nt_socket_t *sock, char *filename, size_t filename_size, nt_uint64_t *file_size)
{
    char header[MAX_STRING_LEN + 16];
    nt_uint32_t filename_len;
    nt_uint64_t file_size_le;
    size_t header_size = sizeof(nt_uint32_t) + MAX_FILENAME_LEN + sizeof(nt_uint64_t);
    int ret;
    
    memset(header, 0, sizeof(header));
    
    ret = recv_raw_bytes(sock, header, header_size);
    if (FILE_TRANS_OK != ret) {
        return ret;
    }
    
    memcpy(&filename_len, header, sizeof(filename_len));
    filename_len = ntohl(filename_len);
    
    if (filename_len >= filename_size || filename_len >= MAX_FILENAME_LEN) {
        return FILE_TRANS_INVALID_FILENAME;
    }
    
    memcpy(filename, header + sizeof(filename_len), filename_len);
    filename[filename_len] = '\0';
    
    memcpy(&file_size_le, header + sizeof(filename_len) + MAX_FILENAME_LEN, sizeof(file_size_le));
    *file_size = nt_letoh_uint64(file_size_le);
    
    if (*file_size > MAX_FILE_SIZE) {
        return FILE_TRANS_FILE_TOO_LARGE;
    }
    
    return FILE_TRANS_OK;
}

int send_ack(nt_socket_t *sock, int result, const char *message)
{
    char ack_buffer[MAX_STRING_LEN];
    nt_int32_t result_net;
    nt_uint32_t msg_len;
    nt_uint32_t msg_len_net;
    
    memset(ack_buffer, 0, sizeof(ack_buffer));
    
    result_net = htonl((nt_int32_t)result);
    memcpy(ack_buffer, &result_net, sizeof(result_net));
    
    if (NULL != message) {
        msg_len = strlen(message);
        if (msg_len >= MAX_STRING_LEN) {
            msg_len = MAX_STRING_LEN - 1;
        }
    } else {
        msg_len = 0;
    }
    
    msg_len_net = htonl(msg_len);
    memcpy(ack_buffer + sizeof(result_net), &msg_len_net, sizeof(msg_len_net));
    
    if (msg_len > 0) {
        memcpy(ack_buffer + sizeof(result_net) + sizeof(msg_len_net), message, msg_len);
    }
    
    return send_raw_bytes(sock, ack_buffer, sizeof(result_net) + sizeof(msg_len_net) + msg_len);
}

int recv_ack(nt_socket_t *sock, int *result, char *message, size_t msg_size)
{
    nt_int32_t result_net;
    nt_uint32_t msg_len;
    nt_uint32_t msg_len_net;
    char ack_buf[8];
    int ret;
    
    ret = recv_raw_bytes(sock, ack_buf, sizeof(result_net) + sizeof(msg_len_net));
    if (FILE_TRANS_OK != ret) {
        return ret;
    }
    
    memcpy(&result_net, ack_buf, sizeof(result_net));
    *result = (int)ntohl(result_net);
    
    memcpy(&msg_len_net, ack_buf + sizeof(result_net), sizeof(msg_len_net));
    msg_len = ntohl(msg_len_net);
    
    if (msg_len > 0 && msg_len < msg_size) {
        ret = recv_raw_bytes(sock, message, msg_len);
        if (FILE_TRANS_OK != ret) {
            return ret;
        }
        message[msg_len] = '\0';
    } else {
        message[0] = '\0';
    }
    
    return FILE_TRANS_OK;
}

int send_file_chunk(nt_socket_t *sock, const char *data, size_t len)
{
    nt_uint32_t chunk_len;
    nt_uint32_t chunk_len_net;
    char header[8];
    int ret;
    
    chunk_len = (nt_uint32_t)len;
    chunk_len_net = htonl(chunk_len);
    memcpy(header, &chunk_len_net, sizeof(chunk_len_net));
    
    ret = send_raw_bytes(sock, header, sizeof(chunk_len_net));
    if (FILE_TRANS_OK != ret) {
        return ret;
    }
    
    if (len > 0) {
        ret = send_raw_bytes(sock, data, len);
        if (FILE_TRANS_OK != ret) {
            return ret;
        }
    }
    
    return FILE_TRANS_OK;
}

int recv_file_chunk(nt_socket_t *sock, char *buffer, size_t buf_size, size_t *bytes_read)
{
    nt_uint32_t chunk_len;
    nt_uint32_t chunk_len_net;
    int ret;
    
    *bytes_read = 0;
    
    ret = recv_raw_bytes(sock, (char *)&chunk_len_net, sizeof(chunk_len_net));
    if (FILE_TRANS_OK != ret) {
        return ret;
    }
    
    chunk_len = ntohl(chunk_len_net);
    
    if (chunk_len == 0) {
        *bytes_read = 0;
        return FILE_TRANS_OK;
    }
    
    if (chunk_len > buf_size) {
        return FILE_TRANS_ERROR;
    }
    
    ret = recv_raw_bytes(sock, buffer, chunk_len);
    if (FILE_TRANS_OK != ret) {
        return ret;
    }
    
    *bytes_read = chunk_len;
    
    return FILE_TRANS_OK;
}

int file_send(nt_socket_t *sock, const char *filepath, int timeout)
{
    FILE *fp = NULL;
    struct stat st;
    char chunk[FILE_CHUNK_SIZE];
    size_t bytes_read;
    nt_uint64_t total_sent = 0;
    nt_uint64_t file_size;
    const char *filename;
    int ret, ack_result;
    char ack_msg[MAX_STRING_LEN];
    char error_msg[MAX_STRING_LEN];
    
    NT_UNUSED(timeout);
    
    if (NULL == filepath || '\0' == *filepath) {
        nt_set_socket_strerror("Invalid filepath");
        return FILE_TRANS_INVALID_FILENAME;
    }
    
    if (0 != stat(filepath, &st)) {
        snprintf(error_msg, sizeof(error_msg), "File not found: %s", filepath);
        nt_set_socket_strerror("%s", error_msg);
        return FILE_TRANS_FILE_NOT_FOUND;
    }
    
    if (!S_ISREG(st.st_mode)) {
        nt_set_socket_strerror("Not a regular file: %s", filepath);
        return FILE_TRANS_ERROR;
    }
    
    file_size = (nt_uint64_t)st.st_size;
    
    if (file_size > MAX_FILE_SIZE) {
        nt_set_socket_strerror("File too large: %llu bytes", (unsigned long long)file_size);
        return FILE_TRANS_FILE_TOO_LARGE;
    }
    
    fp = fopen(filepath, "rb");
    if (NULL == fp) {
        nt_set_socket_strerror("Cannot open file: %s", strerror(errno));
        return FILE_TRANS_PERMISSION_DENIED;
    }
    
    filename = strrchr(filepath, '/');
    if (NULL == filename) {
        filename = filepath;
    } else {
        filename++;
    }
    
    ret = send_file_header(sock, filename, file_size);
    if (FILE_TRANS_OK != ret) {
        fclose(fp);
        return ret;
    }
    
    ret = recv_ack(sock, &ack_result, ack_msg, sizeof(ack_msg));
    if (FILE_TRANS_OK != ret) {
        fclose(fp);
        return ret;
    }
    
    if (FILE_TRANS_OK != ack_result) {
        fclose(fp);
        nt_set_socket_strerror("Server rejected: %s", ack_msg);
        return ack_result;
    }
    
    while ((bytes_read = fread(chunk, 1, FILE_CHUNK_SIZE, fp)) > 0) {
        ret = send_file_chunk(sock, chunk, bytes_read);
        if (FILE_TRANS_OK != ret) {
            fclose(fp);
            return ret;
        }
        
        total_sent += bytes_read;
    }
    
    ret = send_file_chunk(sock, NULL, 0);
    
    fclose(fp);
    
    ret = recv_ack(sock, &ack_result, ack_msg, sizeof(ack_msg));
    if (FILE_TRANS_OK != ret) {
        return ret;
    }
    
    if (FILE_TRANS_OK != ack_result) {
        nt_set_socket_strerror("Transfer failed: %s", ack_msg);
        return ack_result;
    }
    
    return FILE_TRANS_OK;
}

int file_recv(nt_socket_t *sock, const char *save_dir, int timeout)
{
    char filename[MAX_FILENAME_LEN];
    char filepath[MAX_STRING_LEN];
    char chunk[FILE_CHUNK_SIZE];
    FILE *fp = NULL;
    nt_uint64_t file_size;
    nt_uint64_t total_received = 0;
    size_t bytes_read;
    int ret;
    char *dir_path;
    char cmd[MAX_STRING_LEN - 64];
    
    NT_UNUSED(timeout);
    NT_UNUSED(save_dir);
    
    ret = recv_file_header(sock, filename, sizeof(filename), &file_size);
    if (FILE_TRANS_OK != ret) {
        send_ack(sock, ret, "Failed to receive file header");
        return ret;
    }
    
    if (strstr(filename, "..") != NULL) {
        send_ack(sock, FILE_TRANS_INVALID_FILENAME, "Path traversal not allowed");
        return FILE_TRANS_INVALID_FILENAME;
    }
    
    if (filename[0] == '/') {
        nt_strlcpy(filepath, filename, sizeof(filepath));
    } else if (save_dir != NULL && save_dir[0] != '\0') {
        if (snprintf(filepath, sizeof(filepath), "%s/%s", save_dir, filename) >= (int)sizeof(filepath)) {
            send_ack(sock, FILE_TRANS_INVALID_FILENAME, "File path too long");
            return FILE_TRANS_INVALID_FILENAME;
        }
    } else {
        nt_strlcpy(filepath, filename, sizeof(filepath));
    }
    
    dir_path = strdup(filepath);
    if (dir_path != NULL) {
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            if (strlen(dir_path) + 12 < sizeof(cmd)) {
                snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", dir_path);
                if (system(cmd) != 0) {
                    fprintf(stderr, "Warning: Cannot create directory %s\n", dir_path);
                }
            }
        }
        free(dir_path);
    }
    
    fp = fopen(filepath, "wb");
    if (NULL == fp) {
        send_ack(sock, FILE_TRANS_PERMISSION_DENIED, "Cannot create file");
        return FILE_TRANS_PERMISSION_DENIED;
    }
    
    ret = send_ack(sock, FILE_TRANS_OK, "Ready to receive file");
    if (SUCCEED != ret) {
        fclose(fp);
        return FILE_TRANS_NETWORK_ERROR;
    }
    
    while (total_received < file_size) {
        ret = recv_file_chunk(sock, chunk, sizeof(chunk), &bytes_read);
        if (FILE_TRANS_OK != ret) {
            fclose(fp);
            remove(filepath);
            return ret;
        }
        
        if (0 == bytes_read) {
            break;
        }
        
        if (fwrite(chunk, 1, bytes_read, fp) != bytes_read) {
            fclose(fp);
            remove(filepath);
            send_ack(sock, FILE_TRANS_DISK_FULL, "Failed to write file");
            return FILE_TRANS_DISK_FULL;
        }
        
        total_received += bytes_read;
    }
    
    fclose(fp);
    
    if (total_received != file_size) {
        remove(filepath);
        send_ack(sock, FILE_TRANS_ERROR, "Incomplete transfer");
        return FILE_TRANS_ERROR;
    }
    
    ret = send_ack(sock, FILE_TRANS_OK, "File received successfully");
    if (SUCCEED != ret) {
        return FILE_TRANS_NETWORK_ERROR;
    }
    
    return FILE_TRANS_OK;
}

int file_client_send(const char *server_ip, unsigned short port, const char *filepath, int timeout)
{
    nt_socket_t sock;
    int ret;
    
    ret = nt_tcp_connect(&sock, NULL, server_ip, port, timeout, NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    if (SUCCEED != ret) {
        return FILE_TRANS_NETWORK_ERROR;
    }
    
    ret = file_send(&sock, filepath, timeout);
    
    nt_tcp_close(&sock);
    
    return ret;
}

int file_client_recv(const char *server_ip, unsigned short port, const char *filename, const char *save_path, int timeout)
{
    nt_socket_t sock;
    int ret;
    
    NT_UNUSED(filename);
    NT_UNUSED(save_path);
    
    ret = nt_tcp_connect(&sock, NULL, server_ip, port, timeout, NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    if (SUCCEED != ret) {
        return FILE_TRANS_NETWORK_ERROR;
    }
    
    nt_tcp_close(&sock);
    
    return FILE_TRANS_ERROR;
}

int file_transfer_init(nt_socket_t *sock)
{
    nt_socket_clean(sock);
    return FILE_TRANS_OK;
}

void file_transfer_cleanup(nt_socket_t *sock)
{
    if (NULL != sock) {
        nt_tcp_close(sock);
    }
}