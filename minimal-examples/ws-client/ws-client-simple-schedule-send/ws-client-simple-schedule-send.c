#include <libwebsockets.h>
#include <signal.h>

static volatile int exit_sig = 0;
#define MAX_PAYLOAD_SIZE 1024
int direct_send = 0; // 立即发送还是延时发送

void signal_handle(int sig) {
    lwsl_notice("receive signal %d", sig);
    exit_sig = 1;
}

struct session_data {
    int msg_count;
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
};

struct per_vhost_data {
    struct lws_context *context;
    lws_sorted_usec_list_t sul;
    struct lws_client_connect_info connInfo;
    struct lws *client_wsi;
    char established;
};

static void sul_connect_attempt(struct lws_sorted_usec_list *sul) {
    struct per_vhost_data *vhd = lws_container_of(sul, struct per_vhost_data, sul);
    vhd->connInfo.context = vhd->context;
    vhd->connInfo.address = "127.0.0.1";
    vhd->connInfo.port = 8000;
    vhd->connInfo.host = vhd->connInfo.address;
    vhd->connInfo.origin = vhd->connInfo.address;
    vhd->connInfo.ssl_connection = 0;
    vhd->connInfo.protocol = "ws";
    vhd->connInfo.pwsi = &vhd->client_wsi;

    if (!lws_client_connect_via_info(&vhd->connInfo)) {
        lws_sul_schedule(vhd->context, 0, &vhd->sul, sul_connect_attempt, 3 * LWS_US_PER_SEC);
    }
}

static void sul_send_data(struct lws_sorted_usec_list *sul) {
    struct per_vhost_data *vhd = lws_container_of(sul, struct per_vhost_data, sul);
    if (vhd && vhd->client_wsi && vhd->established) {
        lws_callback_on_writable(vhd->client_wsi);
    }
}

int client_simple_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    struct per_vhost_data *vhd = (struct per_vhost_data *) lws_protocol_vh_priv_get(lws_get_vhost(wsi),
                                                                                    lws_get_protocol(wsi));

    struct session_data *data = (struct session_data *) user;
    lwsl_wsi_info(wsi, "reason: %d", reason);
    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            lwsl_notice("lws_callback_client_init\n");
            vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi),
                                              sizeof(struct per_vhost_data));
            vhd->context = lws_get_context(wsi);
            vhd->established = 0;
            sul_connect_attempt(&vhd->sul);
            break;
        case LWS_CALLBACK_CLIENT_ESTABLISHED:   // 连接到服务器后的回调
            lwsl_notice("Connected to server ok!\n");
            vhd->established = 1;

            if (vhd->client_wsi && vhd->established) {
                lws_callback_on_writable(vhd->client_wsi);
            }

            vhd->established = 1;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:       // 接收到服务器数据后的回调，数据为in，其长度为len
            lwsl_notice("receive: %s\n", (char *) in);
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:     // 当此客户端可以发送数据时的回调
            if (data->msg_count < 3) {
                // 前面LWS_PRE个字节必须留给LWS
                memset(data->buf, 0, sizeof(data->buf));
                char *msg = (char *) &data->buf[LWS_PRE];
                data->len = sprintf(msg, "hello %d", data->msg_count);
                lwsl_notice("send: %s\n", msg);
                lws_write(wsi, &data->buf[LWS_PRE], data->len, LWS_WRITE_TEXT);
                data->msg_count++;

                if (direct_send) {
                    sul_send_data(&vhd->sul);
                } else {
                    // 定时100ms后再次发送数据
                    lws_sul_schedule(vhd->context, 0, &vhd->sul, sul_send_data, 1000 * LWS_US_PER_MS);
                }
            }
            break;
        default:
            break;
    }
    return 0;
}

struct lws_protocols protocols[] = {
        {
                "ws", client_simple_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE,
        },
        {
                NULL, NULL,                   0
        }
};

int main() {
    signal(SIGTERM, signal_handle);

    struct lws_context_creation_info ctx_info = {0};
    ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    ctx_info.iface = NULL;
    ctx_info.protocols = protocols;
    ctx_info.gid = -1;
    ctx_info.uid = -1;

    struct lws_context *context = lws_create_context(&ctx_info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return -1;
    }

    while (!exit_sig) {
        lws_service(context, 0);
    }

    lws_context_destroy(context);

    return 0;
}