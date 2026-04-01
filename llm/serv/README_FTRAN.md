# 文件传输工具 ftran_serv 和 ftran_clie

## 编译

```bash
cd /home/phy/nt-tst/net-tst
make clean && make
```

## 使用方法

### 服务器端

```bash
# 默认端口17000，默认保存目录 /var/ftran/uploads
./simple_server

# 指定端口和保存目录
./simple_server 18000 /tmp/uploads
```

### 客户端

```bash
# 传输文件格式: server:port 本地文件 远程路径
./ftran_clie 192.168.1.100:17000 /local/file.txt /remote/file.txt

# 使用默认端口
./ftran_clie 192.168.1.100 /local/file.txt /remote/file.txt

# 指定超时时间
./ftran_clie -t 120 192.168.1.100:17000 /bigfile.iso /bigfile.iso
```

## 测试示例

```bash
# 终端1: 启动服务器
./simple_server 17000 /tmp/uploads

# 终端2: 传输文件
echo "test content" > /tmp/test.txt
./ftran_clie 127.0.0.1:17000 /tmp/test.txt /received.txt

# 验证
ls -l /tmp/uploads/
cat /tmp/uploads/received.txt
```

## 配置文件 (ftran_serv)

复制示例配置：
```bash
cp ftran_serv.conf.example /etc/ftran_serv.conf
```

配置选项：
- ListenPort: 监听端口
- ListenIP: 监听地址
- SaveDir: 保存目录
- MaxConnections: 最大连接数
- LogLevel: 日志级别

## 注意事项

1. 确保保存目录存在且有写权限
2. 防火墙需要开放对应端口
3. 文件大小限制为100MB
4. 文件名长度限制256字符

## 故障排查

```bash
# 检查端口是否被占用
netstat -tlnp | grep 17000

# 查看服务器日志
tail -f /tmp/ftran_serv.log

# 测试连接
telnet 127.0.0.1 17000
```
