#ifndef FILE_TRANSFER_CLIENT_H
#define FILE_TRANSFER_CLIENT_H

#include "common.h"
#include "comms.h"
#include "file_transfer.h"

#define DEFAULT_SERVER_PORT 17000
#define DEFAULT_TIMEOUT 60

typedef struct {
    char server_ip[MAX_STRING_LEN];
    unsigned short server_port;
    char source_file[MAX_STRING_LEN];
    char dest_path[MAX_STRING_LEN];
    int timeout;
    int verbose;
    int show_progress;
}
client_config_t;

int init_client_config(client_config_t *config);
int parse_client_args(int argc, char **argv, client_config_t *config);
void print_client_usage(const char *prog_name);
int run_client(client_config_t *config);
void print_progress(nt_uint64_t transferred, nt_uint64_t total);

#endif