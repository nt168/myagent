#include "ftran_serv.h"
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

static int running = 1;

static void signal_handler(int sig)
{
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            log_server_message(1, "Received signal %d, shutting down", sig);
            running = 0;
            break;
        case SIGHUP:
            log_server_message(1, "Received SIGHUP, reloading configuration");
            break;
    }
}

void log_server_message(int level, const char *format, ...)
{
    va_list args;
    char buffer[MAX_STRING_LEN];
    char time_buf[64];
    time_t now;
    struct tm *tm_info;
    static FILE *log_fp = NULL;
    static char log_file_path[MAX_STRING_LEN] = {0};
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    now = time(NULL);
    tm_info = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("[%s] [%d] %s\n", time_buf, level, buffer);
    
    if (log_file_path[0] == '\0') {
        char *log_file = getenv("FTRAN_LOG_FILE");
        if (log_file) {
            strncpy(log_file_path, log_file, sizeof(log_file_path) - 1);
            log_file_path[sizeof(log_file_path) - 1] = '\0';
        } else {
            strncpy(log_file_path, "/tmp/ftran_serv.log", sizeof(log_file_path) - 1);
            log_file_path[sizeof(log_file_path) - 1] = '\0';
        }
    }
    
    if (log_fp == NULL && log_file_path[0] != '\0') {
        log_fp = fopen(log_file_path, "a");
    }
    
    if (log_fp != NULL) {
        fprintf(log_fp, "[%s] [%d] %s\n", time_buf, level, buffer);
        fflush(log_fp);
    }
}

int init_server_config(server_config_t *config)
{
    memset(config, 0, sizeof(server_config_t));
    
    config->listen_port = DEFAULT_PORT;
    nt_strlcpy(config->listen_ip, "0.0.0.0", sizeof(config->listen_ip));
    nt_strlcpy(config->save_dir, DEFAULT_SAVE_DIR, sizeof(config->save_dir));
    config->max_connections = MAX_CONNECTIONS;
    config->log_level = 1;
    config->daemon_mode = 0;
    nt_strlcpy(config->log_file, LOG_FILE, sizeof(config->log_file));
    nt_strlcpy(config->pid_file, "/var/run/ftran_serv.pid", sizeof(config->pid_file));
    
    return SUCCEED;
}

int load_server_config(const char *config_file, server_config_t *config)
{
    FILE *fp;
    char line[MAX_STRING_LEN];
    char *key, *value;
    
    if (NULL == config_file || '\0' == *config_file) {
        return FAIL;
    }
    
    fp = fopen(config_file, "r");
    if (NULL == fp) {
        return FAIL;
    }
    
while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *ptr = line;
        while (*ptr && isspace(*ptr)) ptr++;
        
        if (*ptr == '#' || *ptr == '\n' || *ptr == '\0')
        {
            continue;
        }
        
        key = strtok(ptr, "=\n");
        value = strtok(NULL, "\n");
        
        if (key == NULL) continue;
        
        while (*key && isspace(*key)) key++;
        char *end = key + strlen(key) - 1;
        while (end > key && isspace(*end)) *end-- = '\0';
        
        if (value != NULL) {
            while (*value && isspace(*value)) value++;
            end = value + strlen(value) - 1;
            while (end > value && isspace(*end)) *end-- = '\0';
        }
        
        if (0 == strcmp(key, "ListenPort")) {
            if (value) config->listen_port = (unsigned short)atoi(value);
        }
        else if (0 == strcmp(key, "ListenIP")) {
            if (value) nt_strlcpy(config->listen_ip, value, sizeof(config->listen_ip));
        }
        else if (0 == strcmp(key, "SaveDir")) {
            if (value) nt_strlcpy(config->save_dir, value, sizeof(config->save_dir));
        }
        else if (0 == strcmp(key, "MaxConnections")) {
            if (value) config->max_connections = atoi(value);
        }
        else if (0 == strcmp(key, "LogLevel")) {
            if (value) config->log_level = atoi(value);
        }
        else if (0 == strcmp(key, "LogFile")) {
            if (value) nt_strlcpy(config->log_file, value, sizeof(config->log_file));
        }
        else if (0 == strcmp(key, "PidFile")) {
            if (value) nt_strlcpy(config->pid_file, value, sizeof(config->pid_file));
        }
    }
    
    fclose(fp);
    return SUCCEED;
}

void print_server_usage(const char *prog_name)
{
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -p, --port <port>        Listen port (default: %d)\n", DEFAULT_PORT);
    printf("  -i, --ip <ip>            Listen IP address (default: 0.0.0.0)\n");
    printf("  -d, --dir <directory>    Directory to save received files (default: %s)\n", DEFAULT_SAVE_DIR);
    printf("  -c, --config <file>      Configuration file\n");
    printf("  -D, --daemon             Run as daemon\n");
    printf("  -l, --log-level <level>  Log level (0-5, default: 1)\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExample:\n");
    printf("  %s -p 17000 -d /var/ftran/uploads\n", prog_name);
    printf("  %s -c /etc/ftran_serv.conf\n", prog_name);
}

int parse_server_args(int argc, char **argv, server_config_t *config)
{
    int i;
    
    for (i = 1; i < argc; i++) {
        if (0 == strcmp(argv[i], "-h") || 0 == strcmp(argv[i], "--help")) {
            print_server_usage(argv[0]);
            exit(0);
        }
        else if (0 == strcmp(argv[i], "-p") || 0 == strcmp(argv[i], "--port")) {
            if (i + 1 < argc) {
                config->listen_port = (unsigned short)atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: --port requires an argument\n");
                return FAIL;
            }
        }
        else if (0 == strcmp(argv[i], "-i") || 0 == strcmp(argv[i], "--ip")) {
            if (i + 1 < argc) {
                nt_strlcpy(config->listen_ip, argv[++i], sizeof(config->listen_ip));
            } else {
                fprintf(stderr, "Error: --ip requires an argument\n");
                return FAIL;
            }
        }
        else if (0 == strcmp(argv[i], "-d") || 0 == strcmp(argv[i], "--dir")) {
            if (i + 1 < argc) {
                nt_strlcpy(config->save_dir, argv[++i], sizeof(config->save_dir));
            } else {
                fprintf(stderr, "Error: --dir requires an argument\n");
                return FAIL;
            }
        }
        else if (0 == strcmp(argv[i], "-c") || 0 == strcmp(argv[i], "--config")) {
            if (i + 1 < argc) {
                load_server_config(argv[++i], config);
            } else {
                fprintf(stderr, "Error: --config requires an argument\n");
                return FAIL;
            }
        }
        else if (0 == strcmp(argv[i], "-D") || 0 == strcmp(argv[i], "--daemon")) {
            config->daemon_mode = 1;
        }
        else if (0 == strcmp(argv[i], "-l") || 0 == strcmp(argv[i], "--log-level")) {
            if (i + 1 < argc) {
                config->log_level = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: --log-level requires an argument\n");
                return FAIL;
            }
        }
        else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return FAIL;
        }
    }
    
    return SUCCEED;
}

static int create_save_directory(const char *path)
{
    struct stat st;
    
    if (0 == stat(path, &st)) {
        if (S_ISDIR(st.st_mode)) {
            return SUCCEED;
        } else {
            fprintf(stderr, "Error: %s is not a directory\n", path);
            return FAIL;
        }
    }
    
    char cmd[MAX_STRING_LEN];
    size_t path_len = strlen(path);
    if (path_len + 12 >= MAX_STRING_LEN) {
        fprintf(stderr, "Error: Path too long\n");
        return FAIL;
    }
    
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    
    if (0 != system(cmd)) {
        fprintf(stderr, "Error: Cannot create directory %s\n", path);
        return FAIL;
    }
    
    return SUCCEED;
}

static void handle_client(nt_socket_t *client_sock, server_config_t *config)
{
    char remote_ip[MAX_NT_DNSNAME_LEN + 1];
    int ret;
    
    nt_strlcpy(remote_ip, client_sock->peer, sizeof(remote_ip));
    
    log_server_message(1, "Handling connection from %s", remote_ip);
    
    ret = file_recv(client_sock, config->save_dir, 60);
    
    if (FILE_TRANS_OK == ret) {
        log_server_message(1, "File received successfully from %s", remote_ip);
    } else {
        char error_msg[MAX_STRING_LEN];
        file_trans_strerror(ret, error_msg, sizeof(error_msg));
        log_server_message(2, "File receive failed from %s: %s (%s)", 
                          remote_ip, error_msg, nt_socket_strerror());
    }
    
    shutdown(client_sock->socket, SHUT_RDWR);
    close(client_sock->socket);
    log_server_message(1, "Connection from %s closed", remote_ip);
}

int run_server(server_config_t *config)
{
    nt_socket_t listen_sock;
    int ret;
    pid_t pid;
    
    log_server_message(1, "Starting file transfer server on %s:%d", 
                      config->listen_ip, config->listen_port);
    log_server_message(1, "Save directory: %s", config->save_dir);
    
    if (FAIL == create_save_directory(config->save_dir)) {
        log_server_message(3, "Failed to create save directory: %s", config->save_dir);
        return FAIL;
    }
    
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    
    ret = nt_tcp_listen(&listen_sock, config->listen_ip, config->listen_port);
    if (SUCCEED != ret) {
        log_server_message(3, "Failed to listen on %s:%d: %s", 
                          config->listen_ip, config->listen_port, nt_socket_strerror());
        return FAIL;
    }
    
    log_server_message(1, "Server listening on %s:%d (sockets: %d)", 
                      config->listen_ip, config->listen_port, listen_sock.num_socks);
    
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    
    while (running) {
        ret = nt_tcp_accept(&listen_sock, NT_TCP_SEC_UNENCRYPTED);
        
        if (SUCCEED != ret) {
            if (running) {
                log_server_message(2, "Accept failed: %s", nt_socket_strerror());
            }
            continue;
        }
        
        log_server_message(1, "Accepted connection from %s", listen_sock.peer);
        
        pid = fork();
        if (0 == pid) {
            for (int i = 0; i < listen_sock.num_socks; i++) {
                close(listen_sock.sockets[i]);
            }
            
            handle_client(&listen_sock, config);
            
            exit(0);
        }
        else if (pid > 0) {
            close(listen_sock.socket);
            listen_sock.accepted = 0;
        }
        else {
            log_server_message(2, "Failed to fork: %s", strerror(errno));
            nt_tcp_unaccept(&listen_sock);
        }
    }
    
    log_server_message(1, "Server shutting down");
    nt_tcp_close(&listen_sock);
    
    return SUCCEED;
}

int main(int argc, char **argv)
{
    server_config_t config;
    int ret;
    
    init_server_config(&config);
    
    ret = parse_server_args(argc, argv, &config);
    if (SUCCEED != ret) {
        return 1;
    }
    
    ret = run_server(&config);
    
    return (SUCCEED == ret) ? 0 : 1;
}