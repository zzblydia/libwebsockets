#include <stdio.h>
#include <string.h>
#include <unistd.h> // usleep

#include "ws-utils.h"

WstClient g_client[100] = {0};

int MyCallback(unsigned short callbackIndex, int msgType) {
    WstClient *wstClient = &g_client[callbackIndex];

    switch (msgType) {
        case WST_MSGTYPE_INIT_SUCCESS:
            printf("MyCallback WST_MSGTYPE_INIT_SUCCESS\n");
            break;
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
            if (wstClient->msg_count < 3) {
                sprintf(wstClient->sendBuf, "Hello World! %d", wstClient->msg_count);
                *(wstClient->sendBufLen) = (int) strlen(wstClient->sendBuf);
                *(wstClient->sendBufType) = 0; // 0 for text, 1 for binary

                wstClient->msg_count++;
                WstSend(wstClient);
            } else {
                wstClient->callbackIndex = WST_CALLBACK_INDEX_NULL;
            }
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

char *g_serverAddress = "127.0.0.1";
unsigned short g_serverPort = 8001;

int main() {
    // 初始化
    WstInit();
    WstClient *client = &g_client[1];
    client->callbackIndex = 1;
    memcpy(client->serverAddress, g_serverAddress, strlen(g_serverAddress));
    client->serverPort = g_serverPort;
    client->eventCallback = MyCallback;
    client->ssl = 1;

    WstConnect(client);

    while (1) {
        WstPoll(client);
        usleep(100 * 1000);
    }
}