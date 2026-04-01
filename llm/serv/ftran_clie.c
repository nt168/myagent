#include "ftran_clie.h"
#include <sys/stat.h>

int init_client_config(client_config_t *config)
{
    memset(config, 0, sizeof(client_config_t));
    
    config->server_port = DEFAULT_SERVER_PORT;
    config->timeout = DEFAULT_TIMEOUT;
    config->verbose = 0;
    config->show_progress = 1;
    
    return SUCCEED;
}

void print_client_usage(const char *prog_name)
{
    printf("Usage: %s [options] <server:port> <source_file> <dest_path>\n\n", prog_name);
    printf("Arguments:\n");
    printf("  <server:port>     Server IP and port (e.g., 192.168.1.100:17000 or just IP for default port)\n");
    printf("  <source_file>     Source file path on local machine\n");
    printf("  <dest_path>       Destination file path on server machine\n");
    printf("\nOptions:\n");
    printf("  -t, --timeout <seconds>  Connection timeout (default: %d)\n", DEFAULT_TIMEOUT);
    printf("  -P, --no-progress        Disable progress display\n");
    printf("  -v, --verbose            Verbose output\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s 192.168.1.100:17000 /local/file.txt /remote/file.txt\n", prog_name);
    printf("  %s 192.168.1.100 /local/file.txt /remote/file.txt\n", prog_name);
    printf("  %s -t 120 192.168.1.100:17000 /local/bigfile.iso /remote/bigfile.iso\n", prog_name);
}

int parse_client_args(int argc, char **argv, client_config_t *config)
{
    int i;
    int arg_count = 0;
    char *server_arg = NULL;
    char *source_arg = NULL;
    char *dest_arg = NULL;
    char *colon;
    
    for (i = 1; i < argc; i++) {
        if (0 == strcmp(argv[i], "-h") || 0 == strcmp(argv[i], "--help")) {
            print_client_usage(argv[0]);
            exit(0);
        }
        else if (0 == strcmp(argv[i], "-t") || 0 == strcmp(argv[i], "--timeout")) {
            if (i + 1 < argc) {
                config->timeout = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: --timeout requires an argument\n");
                return FAIL;
            }
        }
        else if (0 == strcmp(argv[i], "-v") || 0 == strcmp(argv[i], "--verbose")) {
            config->verbose = 1;
        }
        else if (0 == strcmp(argv[i], "-P") || 0 == strcmp(argv[i], "--no-progress")) {
            config->show_progress = 0;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return FAIL;
        }
        else {
            switch (arg_count) {
                case 0:
                    server_arg = argv[i];
                    break;
                case 1:
                    source_arg = argv[i];
                    break;
                case 2:
                    dest_arg = argv[i];
                    break;
                default:
                    fprintf(stderr, "Error: Too many arguments\n");
                    return FAIL;
            }
            arg_count++;
        }
    }
    
    if (arg_count < 3) {
        fprintf(stderr, "Error: Missing required arguments\n");
        fprintf(stderr, "Usage: %s <server:port> <source_file> <dest_path>\n", argv[0]);
        return FAIL;
    }
    
    colon = strchr(server_arg, ':');
    if (colon != NULL) {
        *colon = '\0';
        nt_strlcpy(config->server_ip, server_arg, sizeof(config->server_ip));
        config->server_port = (unsigned short)atoi(colon + 1);
        *colon = ':';
    } else {
        nt_strlcpy(config->server_ip, server_arg, sizeof(config->server_ip));
        config->server_port = DEFAULT_SERVER_PORT;
    }
    
    nt_strlcpy(config->source_file, source_arg, sizeof(config->source_file));
    nt_strlcpy(config->dest_path, dest_arg, sizeof(config->dest_path));
    
    return SUCCEED;
}

void print_progress(nt_uint64_t transferred, nt_uint64_t total)
{
    double percent = (total > 0) ? ((double)transferred / total * 100.0) : 0.0;
    
    printf("\rProgress: %.1f%% (%llu / %llu bytes)", 
           percent, 
           (unsigned long long)transferred, 
           (unsigned long long)total);
    
    if (transferred >= total) {
        printf("\n");
    }
    
    fflush(stdout);
}

static int send_file_with_path(nt_socket_t *sock, const char *src_file, const char *dest_path, int timeout)
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
    
    if (0 != stat(src_file, &st)) {
        fprintf(stderr, "Error: File not found: %s\n", src_file);
        return FILE_TRANS_FILE_NOT_FOUND;
    }
    
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "Error: Not a regular file: %s\n", src_file);
        return FILE_TRANS_ERROR;
    }
    
    file_size = (nt_uint64_t)st.st_size;
    
    if (file_size > MAX_FILE_SIZE) {
        fprintf(stderr, "Error: File too large: %llu bytes (max: %llu bytes)\n", 
                (unsigned long long)file_size, (unsigned long long)MAX_FILE_SIZE);
        return FILE_TRANS_FILE_TOO_LARGE;
    }
    
    fp = fopen(src_file, "rb");
    if (NULL == fp) {
        fprintf(stderr, "Error: Cannot open file: %s\n", strerror(errno));
        return FILE_TRANS_PERMISSION_DENIED;
    }
    
    filename = dest_path;
    if (filename[0] == '\0') {
        filename = strrchr(src_file, '/');
        if (NULL == filename) {
            filename = src_file;
        } else {
            filename++;
        }
    }
    
    printf("Sending file: %s\n", src_file);
    printf("Remote path:  %s\n", filename);
    printf("File size:    %llu bytes\n", (unsigned long long)file_size);
    
    ret = send_file_header(sock, filename, file_size);
    if (FILE_TRANS_OK != ret) {
        fclose(fp);
        fprintf(stderr, "Error: Failed to send file header\n");
        return ret;
    }
    
    ret = recv_ack(sock, &ack_result, ack_msg, sizeof(ack_msg));
    if (FILE_TRANS_OK != ret) {
        fclose(fp);
        fprintf(stderr, "Error: Failed to receive acknowledgment\n");
        return ret;
    }
    
    if (FILE_TRANS_OK != ack_result) {
        fclose(fp);
        fprintf(stderr, "Error: Server rejected: %s\n", ack_msg);
        return ack_result;
    }
    
    printf("Transferring...\n");
    
    while ((bytes_read = fread(chunk, 1, FILE_CHUNK_SIZE, fp)) > 0) {
        ret = send_file_chunk(sock, chunk, bytes_read);
        if (FILE_TRANS_OK != ret) {
            fclose(fp);
            fprintf(stderr, "\nError: Failed to send data chunk\n");
            return ret;
        }
        
        total_sent += bytes_read;
        
        if (file_size > 0) {
            print_progress(total_sent, file_size);
        }
    }
    
    ret = send_file_chunk(sock, NULL, 0);
    if (FILE_TRANS_OK != ret) {
        fclose(fp);
        fprintf(stderr, "Error: Failed to send EOF marker\n");
        return ret;
    }
    
    fclose(fp);
    
    ret = recv_ack(sock, &ack_result, ack_msg, sizeof(ack_msg));
    if (FILE_TRANS_OK != ret) {
        fprintf(stderr, "Error: Failed to receive final acknowledgment\n");
        return ret;
    }
    
    if (FILE_TRANS_OK != ack_result) {
        fprintf(stderr, "Error: Transfer failed: %s\n", ack_msg);
        return ack_result;
    }
    
    printf("File transfer completed successfully\n");
    
    return FILE_TRANS_OK;
}

int run_client(client_config_t *config)
{
    nt_socket_t sock;
    int ret;
    char error_msg[MAX_STRING_LEN];
    
    printf("Connecting to %s:%d...\n", config->server_ip, config->server_port);
    
    ret = nt_tcp_connect(&sock, NULL, config->server_ip, config->server_port, 
                          config->timeout, NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    if (SUCCEED != ret) {
        fprintf(stderr, "Error: Cannot connect to %s:%d: %s\n", 
                config->server_ip, config->server_port, nt_socket_strerror());
        return FAIL;
    }
    
    printf("Connected to server\n");
    
    ret = send_file_with_path(&sock, config->source_file, config->dest_path, config->timeout);
    
    if (FILE_TRANS_OK != ret) {
        file_trans_strerror(ret, error_msg, sizeof(error_msg));
        fprintf(stderr, "Error: %s\n", error_msg);
    }
    
    nt_tcp_close(&sock);
    
    return (FILE_TRANS_OK == ret) ? SUCCEED : FAIL;
}

int main(int argc, char **argv)
{
    client_config_t config;
    int ret;
    
    init_client_config(&config);
    
    ret = parse_client_args(argc, argv, &config);
    if (SUCCEED != ret) {
        return 1;
    }
    
    ret = run_client(&config);
    
    return (SUCCEED == ret) ? 0 : 1;
}