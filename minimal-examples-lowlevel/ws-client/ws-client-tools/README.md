# ws-client-tools

基于 libwebsockets v4.3 封装的 WebSocket 客户端工具库，提供简洁的回调驱动接口。

## 文件列表

| 文件 | 说明 |
|------|------|
| `ws-utils.h` | 公共头文件，定义结构体、错误码、消息类型、函数接口，无其他依赖 |
| `ws-utils.c` | 工具实现 |
| `ws-utils-main.c` | 调用示例 + 全量自测（内置 echo 服务端，覆盖参数校验/收发/分片/TLS/并发/性能） |

## 构建

```bash
cmake .. -DCMAKE_BUILD_TYPE=DEBUG -DLWS_WITH_MINIMAL_EXAMPLES=ON -DLWS_WITH_NETLINK=OFF -DLWS_WITH_IPV6=ON -DLWS_WITH_ASYNC_DNS=ON
make ws-client-tools
```

## API 说明

### 初始化

```c
int WstInit(EventCallback eventCallback, Log2File log2file);
```

- `eventCallback`：消息回调函数，不可为 NULL。
- `log2file`：日志重定向函数；若环境变量 `LWS_LOG_STDOUT` 存在则忽略，日志直接输出到 stdout。
- 创建全局 context，一个进程只需调用一次。

### 建立连接

```c
int WstConnect(WstClient *client);
```

- 填写 `WstClient.serverUri`（完整 URI，如 `wss://ip:port/path`）后调用。
- TLS 连接（`wss://` / `https://`）会自动创建 vhost；同一 URI 的 vhost 只创建一次，可复用。
- 若需双向认证，填写 `WstClient.certPath`（pem 或 ca 路径）。
- 连接为异步，结果通过 `WST_MSGTYPE_CONNECT_SUCCESS` 或 `WST_MSGTYPE_CONNECT_ERROR` 回调通知。

### 事件驱动

```c
void WstPoll();
```

在主循环中反复调用，驱动事件处理（非阻塞）。

### 发送数据

```c
int WstSend(WstClient *wstClient);
```

触发可写回调。在收到 `WST_MSGTYPE_WRITEABLE` 时，向 `wstClient->sendBuf` 写入数据并设置 `*wstClient->sendBufLen`；`wstClient->maxSendBufLen` 为可用缓冲区上限。

### 断开连接

```c
int WstDisconnect(WstClient *client);
```

异步断开连接。也可在回调函数中返回 `-1` 触发断开。

## 回调消息类型

| 消息类型 | 说明 |
|----------|------|
| `WST_MSGTYPE_INIT_SUCCESS` | 保留，当前不触发 |
| `WST_MSGTYPE_CONNECT_SUCCESS` | 连接成功；`clientIp/Port`、`serverIp/Port` 已填写 |
| `WST_MSGTYPE_CONNECT_ERROR` | 连接失败；错误原因在 `recvBuf`，长度为 `recvBufLen` |
| `WST_MSGTYPE_RECEIVED` | 收到数据；数据在 `recvBuf`，长度为 `recvBufLen`，`bufDataType` 标识二进制或文本 |
| `WST_MSGTYPE_WRITEABLE` | 可以发送；向 `sendBuf` 填写数据并设置 `*sendBufLen`，不发送则置 0 |
| `WST_MSGTYPE_DISCONNECTED` | 连接已断开 |

## 使用示例

```c
int MyCallback(unsigned short callbackIndex, int msgType)
{
    WstClient *client = &g_client[callbackIndex];
    switch (msgType) {
        case WST_MSGTYPE_CONNECT_SUCCESS:
            WstSend(client);  // 触发一次可写回调
            break;
        case WST_MSGTYPE_WRITEABLE:
            // 填写要发送的数据
            memcpy(client->sendBuf, "hello", 5);
            *client->sendBufLen = 5;
            client->bufDataType = LWS_WRITE_TEXT;
            break;
        case WST_MSGTYPE_RECEIVED:
            // client->recvBuf / client->recvBufLen 含接收到的数据
            break;
        case WST_MSGTYPE_DISCONNECTED:
            return -1;  // 返回 -1 同样可触发断开
    }
    return 0;
}

// 初始化与连接
WstInit(MyCallback, NULL);
g_client[0].callbackIndex = 0;
snprintf(g_client[0].serverUri, WS_GENERAL_LEN, "wss://192.168.1.1:8001/ws");
WstConnect(&g_client[0]);

while (1) { WstPoll(); }
```

## 自测程序

本目录提供两个独立的自测 binary，互为补充。

### `ws-client-tools` — 自带内置 echo 服务端

`ws-utils-main.c` 在同进程内拉起一个 ws/wss 完整 echo 服务端，对 `ws-utils.c` 做端到端测试。直接运行 `./bin/ws-client-tools` 即可，期望 `Tests failed: 0`。覆盖：

- **参数校验**：`WstInit/WstConnect/WstSend/WstDisconnect` 的 NULL/越界场景
- **生命周期**：建链、不可达端口的 CONNECT_ERROR、非法 URI、主动断开、服务端发起断开
- **数据收发**：文本、二进制、最大载荷、空写（不填数据不报错）、子路径 + 查询串
- **自定义头域**：握手附加 HTTP 头并由服务端校验
- **多并发**：5 路连接独立 echo
- **TLS**：wss + 自签证书
- **性能**：100 次 round-trip 计时

服务端使用端口 17681 (ws) 与 17682 (wss)，证书自动从常见路径（`../share/libwebsockets-test-server/...`、`../libwebsockets-test-server.pem` 等）查找，找不到则跳过 wss 用例。

### `ws-client-tools-srv-tls` — 配合外部 wss 服务端

`ws-utils-srv-tls-main.c` 在 fork 出来的子进程里拉起 `ws-server-simple-send-recv-tls` (端口 8001, wss only)，专门测大 fragment / 边界条件，因为该服务端 `rx_buffer_size = 1024` 且每次 RECEIVE 直接覆盖缓冲、最后只回写"最后一个分块"。覆盖：

- 文本/二进制小消息 echo
- 边界 1023 / 1024 / 1025 字节
- 大消息 3KB / ~10KB（验证客户端发送不报错、连接不断、回写匹配原始末尾片段）
- 空 WRITEABLE
- 100 次小消息 + 30 次 2KB 性能 round-trip

运行前确保 `ws-server-simple-send-recv-tls` 已编译（与本 target 同一 cmake project，会一起编出）。
