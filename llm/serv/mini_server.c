#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFSIZE 8192

static int running = 1;
void handler(int sig) { running = 0; }

int main(int argc, char **argv) {
    int listenfd, connfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    int port = 17000;
    char buf[BUFSIZE];
    ssize_t n;
    char filename[256];
    FILE *fp;
    uint64_t filesize;
    
    if (argc > 1) port = atoi(argv[1]);
    
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    
    bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    listen(listenfd, 10);
    
    printf("Mini file server listening on port %d\n", port);
    printf("Save directory: /tmp/mini_uploads\n");
    
    mkdir("/tmp/mini_uploads", 0755);
    
    while (running) {
        clilen = sizeof(cliaddr);
        connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);
        if (connfd < 0) continue;
        
        printf("Client connected: %s\n", inet_ntoa(cliaddr.sin_addr));
        
        // 读取文件名长度
        uint32_t fnamelen;
        read(connfd, &fnamelen, sizeof(fnamelen));
        fnamelen = ntohl(fnamelen);
        
        // 读取文件名
        memset(filename, 0, sizeof(filename));
        read(connfd, filename, 256);
        filename[fnamelen] = '\0';
        
        // 读取文件大小
        read(connfd, &filesize, sizeof(filesize));
        
        printf("Receiving: %s (%lu bytes)\n", filename, (unsigned long)filesize);
        
        // 发送ACK
        int ack = 0;
        write(connfd, &ack, sizeof(ack));
        
        // 创建文件
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "/tmp/mini_uploads/%s", filename);
        fp = fopen(filepath, "wb");
        
        // 接收文件内容
        uint64_t received = 0;
        while (received < filesize) {
            uint32_t chunksize;
            read(connfd, &chunksize, sizeof(chunksize));
            chunksize = ntohl(chunksize);
            
            if (chunksize == 0) break;
            
            n = read(connfd, buf, chunksize);
            if (n > 0) {
                fwrite(buf, 1, n, fp);
                received += n;
            }
        }
        
        fclose(fp);
        
        // 发送最终ACK
        ack = 0;
        write(connfd, &ack, sizeof(ack));
        
        printf("File received: %s (%lu bytes)\n", filename, (unsigned long)received);
        
        close(connfd);
    }
    
    close(listenfd);
    return 0;
}
