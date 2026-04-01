#include "common.h"
#include "comms.h"
#include "file_transfer.h"
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>

static int running = 1;

void signal_handler(int sig) {
    printf("Received signal %d\n", sig);
    running = 0;
}

int main(int argc, char **argv) {
    nt_socket_t listen_sock, client_sock;
    int ret;
    unsigned short port = 18000;
    char *save_dir = "/tmp/ft_uploads";
    
    if (argc > 1) port = (unsigned short)atoi(argv[1]);
    if (argc > 2) save_dir = argv[2];
    
    printf("Starting server on port %d\n", port);
    printf("Save directory: %s\n", save_dir);
    
    mkdir(save_dir, 0755);
    
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    ret = nt_tcp_listen(&listen_sock, "0.0.0.0", port);
    if (SUCCEED != ret) {
        fprintf(stderr, "Failed to listen: %s\n", nt_socket_strerror());
        return 1;
    }
    
    printf("Server listening on port %d\n", port);
    
    while (running) {
        ret = nt_tcp_accept(&client_sock, NT_TCP_SEC_UNENCRYPTED);
        if (SUCCEED != ret) {
            if (running) fprintf(stderr, "Accept failed: %s\n", nt_socket_strerror());
            continue;
        }
        
        printf("Accepted connection from %s\n", client_sock.peer);
        
        ret = file_recv(&client_sock, save_dir, 60);
        if (FILE_TRANS_OK == ret) {
            printf("File received successfully\n");
        } else {
            fprintf(stderr, "File receive failed\n");
        }
        
        nt_tcp_close(&client_sock);
    }
    
    nt_tcp_close(&listen_sock);
    printf("Server stopped\n");
    
    return 0;
}
