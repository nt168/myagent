#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define BUFSIZE 8192

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in servaddr;
    struct stat st;
    FILE *fp;
    char buf[BUFSIZE];
    char *server_ip, *local_file, *remote_path;
    int port = 17000;
    
    if (argc < 4) {
        printf("Usage: %s server:port local_file remote_path\n", argv[0]);
        return 1;
    }
    
    // 解析server:port
    server_ip = strdup(argv[1]);
    char *colon = strchr(server_ip, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }
    local_file = argv[2];
    remote_path = argv[3];
    
    // 检查文件
    if (stat(local_file, &st) != 0) {
        fprintf(stderr, "File not found: %s\n", local_file);
        return 1;
    }
    
    printf("Connecting to %s:%d\n", server_ip, port);
    
    // 连接服务器
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(server_ip);
    servaddr.sin_port = htons(port);
    
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        fprintf(stderr, "Connection failed\n");
        return 1;
    }
    
    printf("Connected. Sending file: %s (%lu bytes)\n", remote_path, (unsigned long)st.st_size);
    
    // 发送文件名长度
    uint32_t fnamelen = strlen(remote_path);
    uint32_t net_fnamelen = htonl(fnamelen);
    write(sockfd, &net_fnamelen, sizeof(net_fnamelen));
    
    // 发送文件名
    char fnamebuf[256];
    memset(fnamebuf, 0, sizeof(fnamebuf));
    strncpy(fnamebuf, remote_path, sizeof(fnamebuf) - 1);
    write(sockfd, fnamebuf, sizeof(fnamebuf));
    
    // 发送文件大小
    uint64_t filesize = st.st_size;
    write(sockfd, &filesize, sizeof(filesize));
    
    // 等待ACK
    int ack;
    read(sockfd, &ack, sizeof(ack));
    
    // 发送文件内容
    fp = fopen(local_file, "rb");
    size_t sent = 0;
    while (sent < st.st_size) {
        size_t toread = (st.st_size - sent > BUFSIZE) ? BUFSIZE : (st.st_size - sent);
        size_t n = fread(buf, 1, toread, fp);
        
        if (n > 0) {
            uint32_t chunksize = htonl(n);
            write(sockfd, &chunksize, sizeof(chunksize));
            write(sockfd, buf, n);
            sent += n;
        }
    }
    
    fclose(fp);
    
    // 发送结束标记
    uint32_t endmarker = 0;
    write(sockfd, &endmarker, sizeof(endmarker));
    
    // 等待最终ACK
    read(sockfd, &ack, sizeof(ack));
    
    printf("File sent successfully: %lu bytes\n", (unsigned long)sent);
    
    close(sockfd);
    free(server_ip);
    
    return 0;
}
