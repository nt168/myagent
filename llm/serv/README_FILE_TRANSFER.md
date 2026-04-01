# File Transfer Library Based on NT Network Socket

基于NT网络socket库实现的文件传输接口，支持可靠的网络文件传输。

## 功能概述

### 文件传输API

#### 高级接口
```c
// 发送文件到服务器
int file_send(nt_socket_t *sock, const char *filepath, int timeout);

// 从服务器接收文件
int file_recv(nt_socket_t *sock, const char *save_dir, int timeout);

// 客户端发送文件
int file_client_send(const char *server_ip, unsigned short port, 
                     const char *filepath, int timeout);

// 服务器端启动
int file_server_start(unsigned short port, const char *save_dir);
```

#### 底层接口
```c
// 发送文件头
int send_file_header(nt_socket_t *sock, const char *filename, 
                     nt_uint64_t file_size);

// 接收文件头
int recv_file_header(nt_socket_t *sock, char *filename, 
                     size_t filename_size, nt_uint64_t *file_size);

// 发送数据块
int send_file_chunk(nt_socket_t *sock, const char *data, size_t len);

// 接收数据块
int recv_file_chunk(nt_socket_t *sock, char *buffer, 
                    size_t buf_size, size_t *bytes_read);

// 发送/接收确认
int send_ack(nt_socket_t *sock, int result, const char *message);
int recv_ack(nt_socket_t *sock, int *result, char *message, size_t msg_size);
```

## 协议设计

### 文件传输协议格式

```
[文件头]
├─ 文件名长度 (4 bytes, network byte order)
├─ 文件名 (256 bytes, fixed size)
└─ 文件大小 (8 bytes, little endian)

[确认消息]
├─ 结果码 (4 bytes, network byte order)
├─ 消息长度 (4 bytes, network byte order)
└─ 消息内容 (variable)

[数据块]
├─ 数据长度 (4 bytes, network byte order)
└─ 数据内容 (variable, max 8192 bytes)

[结束标记]
└─ 数据长度 = 0
```

## 错误代码

| 错误代码 | 常量名 | 说明 |
|---------|--------|------|
| 0 | FILE_TRANS_OK | 成功 |
| -1 | FILE_TRANS_ERROR | 一般错误 |
| -2 | FILE_TRANS_FILE_NOT_FOUND | 文件不存在 |
| -3 | FILE_TRANS_FILE_TOO_LARGE | 文件过大 |
| -4 | FILE_TRANS_PERMISSION_DENIED | 权限拒绝 |
| -5 | FILE_TRANS_NETWORK_ERROR | 网络错误 |
| -6 | FILE_TRANS_INVALID_FILENAME | 无效文件名 |
| -7 | FILE_TRANS_DISK_FULL | 磁盘满 |

## 编译和测试

### 编译所有项目
```bash
cd /home/phy/nt-tst/net-tst
make clean && make
```

### 运行测试
```bash
# 运行网络库测试
./net_test

# 运行文件传输测试
./simple_test
```

### 测试结果

#### 网络库测试
```
Passed: 18 tests
Failed: 6 tests
Total:  24 tests
```

#### 文件传输测试
```
Passed: 13 tests
Failed: 5 tests (需要socket连接)
Total:  18 tests
```

## 使用示例

### 服务器端
```c
nt_socket_t listen_sock, client_sock;

// 监听端口
if (SUCCEED == nt_tcp_listen(&listen_sock, "0.0.0.0", 17000)) {
    while (1) {
        // 接受连接
        if (SUCCEED == nt_tcp_accept(&client_sock, NT_TCP_SEC_UNENCRYPTED)) {
            // 接收文件
            if (FILE_TRANS_OK == file_recv(&client_sock, "/data/uploads", 30)) {
                printf("File received successfully\n");
            }
            nt_tcp_close(&client_sock);
        }
    }
    nt_tcp_close(&listen_sock);
}
```

### 客户端
```c
nt_socket_t sock;

// 连接服务器
if (SUCCEED == nt_tcp_connect(&sock, NULL, "192.168.1.100", 17000, 10,
        NT_TCP_SEC_UNENCRYPTED, NULL, NULL)) {
    
    // 发送文件
    if (FILE_TRANS_OK == file_send(&sock, "/path/to/file.txt", 30)) {
        printf("File sent successfully\n");
    } else {
        printf("Send failed: %s\n", nt_socket_strerror());
    }
    
    nt_tcp_close(&sock);
}
```

## 特性

### 可靠性
- 完整的文件头验证
- 文件大小检查（最大100MB）
- 数据完整性验证
- 错误消息传递机制

### 安全性
- 文件名验证（防止路径遍历）
- 文件大小限制
- 保存目录验证

### 性能
- 分块传输（8KB per chunk）
- 支持大文件传输
- 超时控制

## 文件结构

```
net-tst/
├── common.h/c          # 公共库函数
├── comms.h/c           # 网络通讯库
├── file_transfer.h/c   # 文件传输实现
├── test_comms.c        # 网络库测试
├── test_file_simple.c  # 文件传输简单测试
├── test_file_transfer.c # 文件传输完整测试
├── net_test            # 网络测试可执行文件
├── file_test           # 文件测试可执行文件
├── simple_test         # 简单测试可执行文件
├── Makefile            # 编译脚本
└── README.md           # 本文档
```

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| FILE_TRANSFER_PORT | 17000 | 默认传输端口 |
| MAX_FILENAME_LEN | 256 | 最大文件名长度 |
| FILE_CHUNK_SIZE | 8192 | 数据块大小 |
| MAX_FILE_SIZE | 100MB | 最大文件大小 |

## 许可证

GNU General Public License v2.0 or later
Copyright (C) 2001-2018 NT SIA