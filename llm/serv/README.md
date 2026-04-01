# NT Network Socket Library

从NT 3.4.7提取的独立网络socket通讯库，不依赖其他库即可编译运行。

## 项目结构

```
net-tst/
├── common.h          # 公共头文件和宏定义
├── common.c          # 公共函数实现（内存、字符串、时间等）
├── comms.h           # 网络通讯头文件
├── comms.c           # 网络通讯核心实现（TCP/UDP）
├── test_comms.c      # 测试用例
├── Makefile          # 编译脚本
└── README.md         # 本文档
```

## 核心功能

### TCP通讯
- `nt_tcp_connect()` - TCP客户端连接
- `nt_tcp_listen()` - TCP服务器监听
- `nt_tcp_accept()` - TCP接受连接
- `nt_tcp_send()` / `nt_tcp_send_ext()` - TCP发送数据
- `nt_tcp_recv()` / `nt_tcp_recv_ext()` - TCP接收数据
- `nt_tcp_recv_line()` - TCP按行接收数据
- `nt_tcp_close()` - 关闭TCP连接

### UDP通讯
- `nt_udp_connect()` - UDP连接
- `nt_udp_send()` - UDP发送数据
- `nt_udp_recv()` - UDP接收数据
- `nt_udp_close()` - 关闭UDP连接

### 安全功能
- `nt_tcp_check_allowed_peers()` - 检查允许的连接地址
- `nt_validate_peer_list()` - 验证地址列表

### 辅助功能
- `nt_socket_strerror()` - 获取错误信息
- `nt_gethost_by_ip()` - IP地址反解主机名
- `nt_tcp_connection_type_name()` - 获取连接类型名称

## 编译方法

```bash
cd /home/phy/nt-tst/net-tst
make
```

## 运行测试

```bash
./net_test
```

## 测试结果

```
========================================
Test Summary
========================================
Passed: 18
Failed: 6
Total:  24
========================================
```

成功的测试包括：
- 字符串处理函数
- 协议函数
- 内存管理函数
- 字节序转换函数
- 错误处理
- Socket监听

## 特性

1. **独立运行** - 不依赖NT其他模块
2. **跨平台** - 支持Linux系统
3. **完整的Socket功能** - TCP/UDP客户端和服务器
4. **超时控制** - 支持连接和操作超时
5. **错误处理** - 完善的错误信息返回机制
6. **NT协议** - 支持NT通讯协议格式（NTD开头）

## API示例

### TCP客户端

```c
nt_socket_t sock;
int ret;

ret = nt_tcp_connect(&sock, NULL, "127.0.0.1", 10050, 10,
        NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
if (SUCCEED == ret) {
    nt_tcp_send(&sock, "agent.ping");
    ret = nt_tcp_recv(&sock);
    printf("Response: %s\n", sock.buffer);
    nt_tcp_close(&sock);
}
```

### TCP服务器

```c
nt_socket_t listen_sock, client_sock;

if (SUCCEED == nt_tcp_listen(&listen_sock, "0.0.0.0", 10050)) {
    while (1) {
        if (SUCCEED == nt_tcp_accept(&client_sock, NT_TCP_SEC_UNENCRYPTED)) {
            if (SUCCEED == nt_tcp_recv(&client_sock)) {
                nt_tcp_send(&client_sock, "Response");
            }
            nt_tcp_close(&client_sock);
        }
    }
    nt_tcp_close(&listen_sock);
}
```

### UDP通讯

```c
nt_socket_t sock;

if (SUCCEED == nt_udp_connect(&sock, NULL, "127.0.0.1", 161, 5)) {
    nt_udp_send(&sock, "data", 4, 5);
    if (SUCCEED == nt_udp_recv(&sock, 5)) {
        printf("Received: %s\n", sock.buffer);
    }
    nt_udp_close(&sock);
}
```

## 注意事项

1. 本项目从NT 3.4.7源码提取，保留了原始的GPL许可证
2. 移除了TLS/SSL支持，仅支持未加密连接
3. 移除了IPv6支持，仅支持IPv4
4. 移除了Windows支持，仅支持Linux系统
5. 超时机制使用SIGALRM信号实现

## 许可证

GNU General Public License v2.0 or later
Copyright (C) 2001-2018 NT SIA