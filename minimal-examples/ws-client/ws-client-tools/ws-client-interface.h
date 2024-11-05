#ifndef WS_CLIENT_INTERFACE_H
#define WS_CLIENT_INTERFACE_H

typedef int (*EventCallback)(unsigned short callbackIndex, int msgType);

#define WS_GENERAL_LEN 128
#define WST_CALLBACK_INDEX_NULL 0xFFFF
typedef struct {
    unsigned short callbackIndex;   // 为WST_CALLBACK_INDEX_NULL时退出轮询, 销毁上下文

    char clientIp[WS_GENERAL_LEN];  // 指定客户端ip或者配置域名时记录真实的客户端ip
    unsigned short clientPort;      // 记录真实的客户端端口, 因为暂不支持指定客户端端口
    int ssl;                        // 0 for no ssl, 1 for ssl with pem cert, 2 for ssl with ca cert
    char certPath[WS_GENERAL_LEN];  // pem cert path or ca cert path

    char serverAddress[WS_GENERAL_LEN];
    unsigned short serverPort;
    char serverPath[WS_GENERAL_LEN];
    void *context;

    char *sendBuf;
    int *sendBufLen;
    int *sendBufType;
    int maxSendBufLen;

    char *recvBuf;
    int recvBufLen;

    EventCallback eventCallback;

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

int WstInit();

int WstConnect(WstClient *client);

int WstPoll(WstClient *client);

int WstSend(WstClient *wstClient);

int WstDisconnect(WstClient *client);

#endif // WS_CLIENT_INTERFACE_H