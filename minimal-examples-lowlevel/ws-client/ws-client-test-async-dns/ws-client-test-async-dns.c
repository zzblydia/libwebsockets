// cmake .. -DCMAKE_BUILD_TYPE=DEBUG -DLWS_WITH_SYS_ASYNC_DNS=ON -DLWS_IPV6=ON -DLWS_WITH_MINIMAL_EXAMPLES=ON -DLWS_WITH_NETLINK=OFF

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <libwebsockets.h>

static volatile int exit_sig = 0;
#define MAX_PAYLOAD_SIZE 1024

#define CLIENT_NUM 300 // max connection num

void signal_handle(int sig) {
    lwsl_notice("receive signal %d", sig);
    exit_sig = 1;
}

struct session_data {
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
};

typedef struct GoodLuck_S {
    struct lws_context *context;
    int index;
    int established;
    time_t startTime;
} GoodLuck;

GoodLuck g_luckClient[CLIENT_NUM];

int client_simple_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

struct lws_protocols protocols[] = {
        {
                "ws", client_simple_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
        },
        LWS_PROTOCOL_LIST_TERM
};

int client_simple_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    int index = 0;
    struct lws_context *context = lws_get_context(wsi);
    if (context != NULL) {
        index = *(int *) lws_context_user(context);
    }

    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT: {
            lwsl_wsi_user(wsi, "LWS_CALLBACK_PROTOCOL_INIT index %d", index);
            char address[] = "goodluck.com";
            int port = 8001;
            char addr_port[256] = {0};
            sprintf(addr_port, "%s:%u", address, port & 65535);

            struct lws_client_connect_info conn_info = {0};
            conn_info.address = address;
            conn_info.port = port;
            conn_info.host = addr_port;
            conn_info.origin = addr_port;
            conn_info.protocol = protocols[0].name;
            conn_info.context = g_luckClient[index].context;
            lws_client_connect_via_info(&conn_info);
            break;
        }
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lwsl_wsi_user(wsi, "LWS_CALLBACK_CLIENT_ESTABLISHED index %d", index);
            g_luckClient[index].established = 1;
            break;
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_wsi_user(wsi, "LWS_CALLBACK_CLIENT_CONNECTION_ERROR index %d for %s", index, in ? (char *) in : "nil");
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            lwsl_wsi_user(wsi, "LWS_CALLBACK_CLIENT_CLOSED index %d", index);
            break;
        default:
            break;
    }
    return 0;
}

// show index when gdb
void main_poll(int index)
{
    lws_service(g_luckClient[index].context, -1);
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        printf("usage: ./ws-client-test-async-dns para1 para2\n");
        printf("para1: num of connections to be established max 300\n");
        printf("para2: rate of concurrent connections\n");
        exit(0);
    }

    int numConnections = atoi(argv[1]);
    if (numConnections > CLIENT_NUM) {
        numConnections = CLIENT_NUM;
    } else if (numConnections <= 0) {
        printf("numConnections 0, exit\n");
        exit(0);
    }

    int rate = atoi(argv[2]);
    if (rate > 10 ) {
        rate = 10;
    } else if (rate <= 0) {
        rate = 1;
    }

    printf("numConnections %d, rate %d\n", numConnections, rate);

    signal(SIGTERM, signal_handle);
    int logs = LLL_USER | LLL_ERR | LLL_WARN ;
    lws_set_log_level(logs, NULL);

    struct lws_context_creation_info ctx_info = {0};
    ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    ctx_info.iface = NULL;
    ctx_info.protocols = protocols;
    ctx_info.timeout_secs = 3;
    ctx_info.connect_timeout_secs = 3;

    for (int i = 0; i < CLIENT_NUM; i++) {
        g_luckClient[i].context = NULL;
        g_luckClient[i].index = i;
        g_luckClient[i].established = 0;
        g_luckClient[i].startTime = 0;
    }

    int count = 0;
    time_t timePast = 0;
    time_t timeNow;
    int caps = 0;
    for (int i = 0; i <= numConnections && !exit_sig; i++) {
        if (i == numConnections) {
            i = 0;
            count = 0;
            usleep(20 * 1000);
        }

        timeNow = time(NULL);
        // start new connection every second
        if (timeNow - timePast >= 1 && g_luckClient[i].context == NULL && g_luckClient[i].startTime == 0) {
            ctx_info.user = &(g_luckClient[i].index);
            g_luckClient[i].context = lws_create_context(&ctx_info);
            g_luckClient[i].startTime = timeNow;
            caps++;
            if (caps == rate) {
                timePast = timeNow;
                caps = 0;
            }
        }

        if (g_luckClient[i].context != NULL) {
            main_poll(i);
        }

        // destroy if connected >10s or can't connect in 4s
        if ((g_luckClient[i].established == 1 && timeNow - g_luckClient[i].startTime > 10) ||
            (g_luckClient[i].startTime != 0 && g_luckClient[i].established == 0 && timeNow - g_luckClient[i].startTime >= 4)) {
            if (g_luckClient[i].context != NULL) {
                lws_context_destroy(g_luckClient[i].context);
                g_luckClient[i].context = NULL;
            }
            count++;
        }

        if (count == numConnections) {
            break;
        }
    }

    return 0;
}
