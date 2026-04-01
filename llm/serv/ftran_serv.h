#ifndef FILE_TRANSFER_SERVER_H
#define FILE_TRANSFER_SERVER_H

#include "common.h"
#include "comms.h"
#include "file_transfer.h"

#define DEFAULT_PORT 17000
#define MAX_CONNECTIONS 10
#define CONFIG_FILE "/etc/ftran_serv.conf"
#define LOG_FILE "/var/log/ftran_serv.log"
#define DEFAULT_SAVE_DIR "/var/ftran/uploads"

typedef struct {
    unsigned short listen_port;
    char listen_ip[MAX_STRING_LEN];
    char save_dir[MAX_STRING_LEN];
    int max_connections;
    int log_level;
    int daemon_mode;
    char log_file[MAX_STRING_LEN];
    char pid_file[MAX_STRING_LEN];
}
server_config_t;

int load_server_config(const char *config_file, server_config_t *config);
int init_server_config(server_config_t *config);
int parse_server_args(int argc, char **argv, server_config_t *config);
void print_server_usage(const char *prog_name);
int run_server(server_config_t *config);
void log_server_message(int level, const char *format, ...);

#endif