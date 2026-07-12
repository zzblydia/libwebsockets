#include <libwebsockets.h>
#include <signal.h>
#include <stdio.h>

static volatile int exit_sig = 0;
#define MAX_PAYLOAD_SIZE 1000

void signal_handle(int sig)
{
    lwsl_notice("receive signal %d", sig);
    exit_sig = 1;
}

struct session_data {
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
    FILE *fp;
    int msg_count;
    int offset;
    int recvTimes;
};

int g_fileCount = 0;

int fragment_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    struct session_data *data = user;
    lwsl_wsi_user(wsi, "fragment_callback: %d data %p", reason, data);

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lwsl_wsi_user(wsi, "Connected to server ok!");
            g_fileCount++;
            char fileName[128] = {0};
            sprintf(fileName, "./file_%d.mp3", g_fileCount);
            data->fp = fopen(fileName, "wb");
            if (!data->fp) {
                return -1;
            }
            data->msg_count = 0;
            data->offset = 0;
            data->recvTimes = 0;

            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            data->msg_count++;
            lwsl_wsi_user(wsi, "SEND times %d msg_count %d", data->msg_count, data->msg_count);
            char sendBuf[10] = {0};
            sprintf(sendBuf, "hello %d", data->msg_count);
            memcpy(&data->buf[LWS_PRE], sendBuf, strlen(sendBuf));
            lws_write(wsi, &data->buf[LWS_PRE], strlen(sendBuf), LWS_WRITE_TEXT);
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            data->recvTimes++;
            const int last = lws_is_final_fragment(wsi);
            const int binary = lws_frame_is_binary(wsi);
            if (binary) {
                data->offset += (int) fwrite(in, 1, len, data->fp);
                if (last) {
                    lwsl_wsi_user(wsi, "RECEIVE send times %d recv times %d sumLen %d",
                                  data->msg_count, data->recvTimes, data->offset);
                    lws_callback_on_writable(wsi);
                }
            }
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            lwsl_wsi_user(wsi, "Connection closed!");
            if (data->fp) {
                fclose(data->fp);
                data->fp = NULL;
            }
            break;
        default:
            break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

struct lws_protocols protocols[] = {
    {
        "ws", fragment_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
    },
    {
        "wss", fragment_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
    },
    LWS_PROTOCOL_LIST_TERM
};

int main()
{
    lws_set_log_level(4095, NULL);
    struct lws_context_creation_info ctx_info = {0};
    ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    ctx_info.iface = NULL;
    ctx_info.protocols = protocols;
    ctx_info.timeout_secs = 3;
    ctx_info.connect_timeout_secs = 3;
    //ctx_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    struct lws_context *context = lws_create_context(&ctx_info);
    if (context == NULL) {
        return -1;
    }

    struct lws_client_connect_info conn_info = {0};
    conn_info.address = "192.168.8.42";
    conn_info.port = 8101;
    conn_info.host = "192.168.8.42:8001";
    conn_info.origin = "192.168.8.42:8001";
    conn_info.protocol = "wss";
    conn_info.context = context;
    conn_info.ssl_connection = 0;
    struct lws *wsi = lws_client_connect_via_info(&conn_info);
    if (wsi == NULL) {
        return -1;
    }
    while (!exit_sig) {
        lws_service(context, 0);
    }

    lws_context_destroy(context);
    return 0;
}
