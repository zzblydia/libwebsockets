#include <libwebsockets.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int exit_sig = 0;
#define MAX_PAYLOAD_SIZE 10240
#define PAYLOAD_SIZE_EVERY_PACKET 10000

void signal_handle(int sig)
{
    lwsl_notice("receive signal %d", sig);
    exit_sig = 1;
}

struct session_data {
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
    FILE *fp;
    int offset;
};

static int server_fragment_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    struct session_data *data = (struct session_data *) user;
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_wsi_user(wsi, "client connect!");
            data->fp = fopen("./1.mp3", "rb");
            if (!data->fp) {
                return -1;
            }
            data->offset = 0;
            break;
        case LWS_CALLBACK_RECEIVE:
            lwsl_wsi_user(wsi, "received message:%s", (char *) in);
            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            data->len = (int) fread(data->buf + LWS_PRE, 1, PAYLOAD_SIZE_EVERY_PACKET, data->fp);
            if (data->len != 0) {
                lws_write(wsi, &data->buf[LWS_PRE], (size_t) data->len, LWS_WRITE_BINARY);
                data->offset += data->len;
            } else {
                lwsl_wsi_err(wsi, "writable message len err offset %d", data->offset);
                return -1; // 读完文件则断开连接
            }
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            if (data->fp) {
                fclose(data->fp);
                data->fp = NULL;
            }
            break;
        default:
            break;
    }

    return 0;
}

struct lws_protocols protocols[] = {
    {
        "ws", server_fragment_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
    },
    {
        "wss", server_fragment_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
    },
    LWS_PROTOCOL_LIST_TERM
};

int main(int argc, char **argv)
{
    signal(SIGTERM, signal_handle);

    struct lws_context_creation_info ctx_info = {0};
    ctx_info.port = 8101;
    ctx_info.iface = NULL;
    ctx_info.protocols = protocols;

    //ctx_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    //ctx_info.ssl_cert_filepath = "../libwebsockets-test-server.pem";
    //ctx_info.ssl_private_key_filepath = "../libwebsockets-test-server.key.pem";

    struct lws_context *context = lws_create_context(&ctx_info);
    while (!exit_sig) {
        lws_service(context, 0);
    }
    lws_context_destroy(context);

    return 0;
}
