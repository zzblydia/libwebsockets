#ifndef WS_UTILS_H
#define WS_UTILS_H

#define WS_GENERAL_LEN 128
#define WST_CALLBACK_INDEX_NULL 0xFFFF

typedef struct {
    unsigned short callbackIndex;   // 回调指示连接序号,初始化以后不再变化.

    char clientIp[WS_GENERAL_LEN];  // 指定客户端ip或者建立连接后使用的本端ip
    unsigned short clientPort;      // 记录真实的客户端端口, 因为暂不支持指定客户端端口

    char serverAddressType; // 0:ipv4 1:ipv6 2:domain
    char serverAddress[WS_GENERAL_LEN]; // 前缀必须为 wss/https/ws/http
    char certPath[WS_GENERAL_LEN];  // pem cert path or ca cert path

    void *wsi;

    char *sendBuf;
    int *sendBufLen;
    int *sendBufType;
    int maxSendBufLen;

    char *recvBuf;
    int recvBufLen;

    int msg_count;
} WstClient;

typedef enum {
    WST_FAILED = -1,
    WST_SUCCESSFUL = 0,
    WST_INPUT_NULL = 1,
    WST_CREATE_CONTEXT_FAILED,
    WST_CONNECT_FAILED,
    WST_BUTT,
} ErrorCode;

typedef enum {
    WST_MSGTYPE_INIT_SUCCESS,
    WST_MSGTYPE_CONNECT_SUCCESS,
    WST_MSGTYPE_CONNECT_ERROR,
    WST_MSGTYPE_RECEIVED,
    WST_MSGTYPE_WRITEABLE,
    WST_MSGTYPE_DISCONNECTED,
} WstClientMsgType;

typedef int (*EventCallback)(unsigned short callbackIndex, int msgType);

int WstInit(EventCallback eventCallback);

int WstConnect(WstClient *client);

void WstPoll();

int WstSend(WstClient *wstClient);

int WstDisconnect(WstClient *client);

#endif // WS_CLIENT_INTERFACE_H