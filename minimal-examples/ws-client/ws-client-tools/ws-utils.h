#ifndef WS_UTILS_H
#define WS_UTILS_H

#define WS_GENERAL_LEN_128 128
#define WS_GENERAL_LEN_256 256
#define MAX_PAYLOAD_SIZE 10240 // 接口单次接收最大数据长度.

typedef enum {
    URI_TYPE_IPV4,
    URI_TYPE_IPV6,
    URI_TYPE_DOMAIN,
    URI_TYPE_BUTT
} UriType;

typedef struct {
    unsigned short callbackIndex;   // 记录调用者序号,回调时返回,初始化以后不再变化.

    char clientIp[WS_GENERAL_LEN_128];  // 指定客户端ip或者建立连接后记录使用的本端ip.
    unsigned short clientPort;      // 记录使用的本端ip端口, 因为暂不支持指定客户端端口
    char serverIp[WS_GENERAL_LEN_128];  // 建立连接后记录使用的远端ip.
    unsigned short serverPort;      // 建立连接后记录使用的远端端口

    char serverUriType; // 0:ipv4 1:ipv6 2:domain(可能包含非标准端口)
    char serverUri[WS_GENERAL_LEN_128]; // 前缀必须为 wss/https/ws/http
    char certPath[WS_GENERAL_LEN_128];  // 双向认证时客户端证书的路径(pem或者ca)

    void *wsi;

    int bufDataType; // 发送或者接收的数据类型, 二进制还是文本

    int maxSendBufLen;  // 最大可发送数据长度
    char *sendBuf;      // 指向发送缓冲区
    int *sendBufLen;    // 发送缓冲区的实际大小

    char *recvBuf;  // 指向接收缓冲区
    int recvBufLen; // 实际接收的数据长度.
} WstClient;

typedef enum {
    WST_FAILED = -1,
    WST_SUCCESSFUL = 0,
    WST_INPUT_NULL,
    WST_DOUBLE_INIT,
    WST_CREATE_CONTEXT_FAILED,
    WST_CONNECT_FAILED,
    WST_BUTT,
} WstErrorCode;

typedef enum {
    WST_MSGTYPE_INIT_SUCCESS,
    WST_MSGTYPE_CONNECT_SUCCESS,
    WST_MSGTYPE_CONNECT_ERROR,
    WST_MSGTYPE_RECEIVED,
    WST_MSGTYPE_WRITEABLE,
    WST_MSGTYPE_DISCONNECTED,
} WstClientMsgType;

typedef int (*EventCallback)(unsigned short callbackIndex, int msgType);
typedef void (*Log2File)(int level, const char *line);

int WstInit(EventCallback eventCallback, Log2File log2file);

int WstConnect(WstClient *client);

void WstPoll(); // 需要调用者控制休眠以防止cpu 100%

int WstSend(WstClient *wstClient);

int WstDisconnect(WstClient *client);

#endif // WS_UTILS_H
