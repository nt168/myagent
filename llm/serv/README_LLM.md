# llm_serv 使用说明

`llm_serv` 基于 `comms.c` 的 TCP 能力实现，启动后会拉起 `engine/mnn/llm_demo` 子进程，并完成下面两条桥接：

- 网络客户端发来的文本 -> `llm_demo` 的标准输入
- `llm_demo` 的标准输出/标准错误 -> 网络客户端

仓库中同时提供了两个客户端程序：

- `client`：本次新增的测试客户端
- `llm_client`：与 `client` 功能等价的旧名称版本

## 编译

在 `serv` 目录执行：

```bash
cd /home/nt/myagent/llm/serv
make llm_serv client llm_client
```

## 运行服务

默认模型启动命令等价于：

```bash
./llm_serv \
  -h 0.0.0.0 \
  -p 18001 \
  -e /home/nt/myagent/llm/engine/mnn/llm_demo \
  -c /home/nt/myagent/llm/models/Qwen35-08b_mnn/config.json
```

如果希望更明确，推荐直接使用绝对路径：

```bash
cd /home/nt/myagent/llm/serv
./llm_serv \
  -h 0.0.0.0 \
  -p 18001 \
  -e /home/nt/myagent/llm/engine/mnn/llm_demo \
  -c /home/nt/myagent/llm/models/Qwen35-08b_mnn/config.json \
  -v
```

如果 `llm_demo` 需要在特定目录下运行，可附加：

```bash
-w /home/nt/myagent/llm
```

## 交互测试

启动服务端后，在另一个终端连接：

```bash
cd /home/nt/myagent/llm/serv
./client 127.0.0.1 18001
```

连接成功后直接输入内容并回车，服务端会把内容转发给 `llm_demo`，模型的实时输出会回传到当前终端。

退出命令：

```text
/quit
```

或：

```text
/exit
```

## 联调排查

如果连接失败，优先检查：

```bash
ls -l /home/nt/myagent/llm/engine/mnn/llm_demo
ls -l /home/nt/myagent/llm/models/Qwen35-08b_mnn/config.json
```

查看端口监听：

```bash
ss -ltnp | grep 18001
```

如果服务启动后马上退出，通常说明：

- `llm_demo` 路径不对
- `config.json` 路径不对
- `llm_demo` 运行时依赖缺失
- 模型加载失败导致子进程提前退出

可先直接单独验证模型程序：

```bash
/home/nt/myagent/llm/engine/mnn/llm_demo \
  /home/nt/myagent/llm/models/Qwen35-08b_mnn/config.json
```

## 已处理的问题

- 修正了 `llm_serv` 对 `nt_tcp_accept()` 的使用方式，改为按 `comms.c` 的监听 socket 生命周期正确执行 `accept/unaccept`
- 新增 `client.c`
