# ws-client-tools

基于 libwebsockets v4.3 封装的 WebSocket 客户端工具库，提供简洁的回调驱动接口。

## 文件列表

| 文件 | 说明 |
|------|------|
| `ws-utils.h` | 公共头文件，定义结构体、错误码、消息类型、函数接口，无其他依赖 |
| `ws-utils.c` | 工具实现 |
| `ws-utils-main.c` | 调用示例 |

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
