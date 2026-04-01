#include "common.h"
#include "comms.h"
#include <stdio.h>
#include <signal.h>

static int running = 1;
void handler(int sig) { running = 0; }

int main(int argc, char **argv) {
    nt_socket_t listen_sock, client_sock;
    int ret;
    unsigned short port = 18000;
    
    if (argc > 1) port = (unsigned short)atoi(argv[1]);
    
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    
    ret = nt_tcp_listen(&listen_sock, "0.0.0.0", port);
    if (SUCCEED != ret) {
        fprintf(stderr, "Listen failed: %s\n", nt_socket_strerror());
        return 1;
    }
    
    printf("Echo server listening on port %d\n", port);
    
    while (running) {
        ret = nt_tcp_accept(&client_sock, NT_TCP_SEC_UNENCRYPTED);
        if (SUCCEED != ret) continue;
        
        printf("Client connected: %s\n", client_sock.peer);
        
        // 接收数据
        ret = nt_tcp_recv_ext(&client_sock, 0, 10);
        if (ret > 0) {
            printf("Received %zd bytes: %s\n", client_sock.read_bytes, client_sock.buffer);
            // 回送
            nt_tcp_send(&client_sock, client_sock.buffer);
        }
        
        nt_tcp_close(&client_sock);
        printf("Client disconnected\n");
    }
    
    nt_tcp_close(&listen_sock);
    return 0;
}
