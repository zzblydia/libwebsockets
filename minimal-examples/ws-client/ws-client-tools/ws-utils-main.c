#include <stdio.h>
#include <string.h>
#include <unistd.h> // usleep

#include "ws-utils.h"

#define MAX_CLIENT_NUM 100
WstClient g_client[MAX_CLIENT_NUM] = {0};

void log2file(int level, const char *line)
{
    printf("log2file: level=%d, line=%s", level, line);
}

int MyCallback(unsigned short callbackIndex, int msgType)
{
    if (callbackIndex >= MAX_CLIENT_NUM) {
        printf("MyCallback callbackIndex %u out of range\n", callbackIndex);
        return -1;
    }

    WstClient *wstClient = &g_client[callbackIndex];

    switch (msgType) {
        case WST_MSGTYPE_CONNECT_SUCCESS:
            printf("MyCallback WST_MSGTYPE_CONNECT_SUCCESS\n");
            WstSend(wstClient);
            break;
        case WST_MSGTYPE_CONNECT_ERROR:
            printf("MyCallback WST_MSGTYPE_CONNECT_ERROR %s\n", wstClient->recvBuf);
            break;
        case WST_MSGTYPE_RECEIVED:
            printf("MyCallback WST_MSGTYPE_RECEIVED\n");
            break;
        case WST_MSGTYPE_WRITEABLE:
            printf("MyCallback WST_MSGTYPE_WRITEABLE\n");
            break;
        case WST_MSGTYPE_DISCONNECTED:
            printf("MyCallback WST_MSGTYPE_DISCONNECTED\n");
            break;
        default:
            printf("MyCallback callbackIndex=%d, msgType=%d\n", callbackIndex, msgType);
            break;
    }
    return 0;
}

char *g_serverUrl = "wss://192.168.8.42:8001/test";

int main()
{
    // 初始化
    WstInit(MyCallback, log2file);

    for (unsigned short i = 0; i < MAX_CLIENT_NUM; i++) {
        g_client[i].callbackIndex = i;
    }

    g_client[0].callbackIndex = 0;
    g_client[0].serverUriType = URI_TYPE_IPV4;
    memcpy(g_client[0].serverUri, g_serverUrl, strlen(g_serverUrl));

    // 连接
    WstConnect(&g_client[0]);

    while (1) {
        WstPoll();
    }

    WstDisconnect(&g_client[0]);
}