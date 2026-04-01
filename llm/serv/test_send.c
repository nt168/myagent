#include "common.h"
#include "comms.h"
#include <stdio.h>

int main() {
    nt_socket_t sock;
    int ret;
    
    printf("Connecting...\n");
    ret = nt_tcp_connect(&sock, NULL, "127.0.0.1", 18000, 5, 
                          NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    if (SUCCEED != ret) {
        printf("Connect failed: %s\n", nt_socket_strerror());
        return 1;
    }
    
    printf("Connected! Sending data...\n");
    
    // 直接发送简单数据
    char data[] = "HELLO WORLD";
    ret = nt_tcp_send_bytes_to(&sock, data, strlen(data), 5);
    printf("Send result: %d\n", ret);
    
    nt_tcp_close(&sock);
    return 0;
}
