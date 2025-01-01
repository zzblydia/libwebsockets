#include <signal.h>
#include <libwebsockets.h>

static volatile int exit_sig = 0;
#define MAX_PAYLOAD_SIZE 1024

void signal_handle(int sig) {
    lwsl_notice("receive signal %d", sig);
    exit_sig = 1;
}

struct session_data {
    int msg_count;
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
};

int ws_client_callback_protocol_init(struct lws *wsi) {

    char address[] = "127.0.0.1";
    int port = 8001;
    char addr_port[256] = {0};
    sprintf(addr_port, "%s:%u", address, port & 65535);

    struct lws_client_connect_info conn_info = {0};
    conn_info.context = lws_get_context(wsi);;
    conn_info.address = address;
    conn_info.port = port;
    conn_info.host = addr_port;
    conn_info.origin = addr_port;
    conn_info.protocol = "ws"; // 与服务端保持一致
    conn_info.vhost = lws_get_vhost(wsi);

    conn_info.ssl_connection =
            LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
            LCCSCF_ALLOW_INSECURE;

    struct lws *nwsi = lws_client_connect_via_info(&conn_info);
    if (nwsi == NULL) {
        return -1;
    }
    return 0;
}

int client_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    struct session_data *data = (struct session_data *) user;

    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            ws_client_callback_protocol_init(wsi);
            break;
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lwsl_notice("Connected to server ok!\n");
            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            lwsl_notice("receive: %s\n", (char *) in);
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (data->msg_count < 3) {
                // 前面LWS_PRE个字节必须留给LWS
                memset(data->buf, 0, sizeof(data->buf));
                char *msg = (char *) &data->buf[LWS_PRE];
                data->len = sprintf(msg, "hello %d", data->msg_count);
                lwsl_notice("send: %s\n", msg);

                // 通过WebSocket发送文本消息
                lws_write(wsi, &data->buf[LWS_PRE], (size_t) data->len, LWS_WRITE_TEXT);

                data->msg_count++;
                lws_callback_on_writable(wsi);
            }
            break;
        default:
            break;
    }
    return 0;
}

struct lws_protocols protocols[] = {
        {
                "ws", client_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
        },
        LWS_PROTOCOL_LIST_TERM
};

int main() {
    signal(SIGTERM, signal_handle);
    int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;
    lws_set_log_level(logs, NULL);

    struct lws_context_creation_info ctx_info = {0};
    ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    ctx_info.iface = NULL;
    ctx_info.protocols = protocols;
    ctx_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    struct lws_context *context = lws_create_context(&ctx_info);
    if (context == NULL) {
        printf("context null, exit");
        exit(-1);
    }

    while (!exit_sig) {
        lws_service(context, 0);
    }

    lws_context_destroy(context);

    return 0;
}
