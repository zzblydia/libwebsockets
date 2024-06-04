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

/**
 * 某个协议下的连接发生事件时，执行的回调函数
 * wsi：指向webSocket实例的指针
 * reason：导致回调的事件类型
 * user 为每个WebSocket会话分配的内存空间
 * in 某些事件使用此参数，作为传入数据的指针
 * len 某些事件使用此参数，说明传入数据的长度
 */
int client_simple_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    struct session_data *data = (struct session_data *) user;
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:   // 连接到服务器后的回调
            lwsl_notice("Connected to server ok!\n");
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

                // 通过WebSocket发送文本消息
                lws_write(wsi, &data->buf[LWS_PRE], (size_t) data->len, LWS_WRITE_TEXT);

                data->msg_count++;
            }
            break;
        default:
            break;
    }
    return 0;
}

/**
 * 支持的WebSocket子协议数组
 * 子协议即JavaScript客户端WebSocket(url, protocols)第2参数数组的元素
 * 你需要为每种协议提供回调函数
 */
struct lws_protocols protocols[] = {
        {
            //协议名称，协议回调，接收缓冲区大小
            "ws", client_simple_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
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
    ctx_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT; // 如果使用SSL加密，需要设置此选项
    struct lws_context *context = lws_create_context(&ctx_info);

    char address[] = "127.0.0.1";
    int port = 8001;
    char addr_port[256] = {0};
    sprintf(addr_port, "%s:%u", address, port & 65535);

    // 客户端连接参数
    struct lws_client_connect_info conn_info = {0};
    conn_info.context = context;
    conn_info.address = address;
    conn_info.port = port;
    conn_info.host = addr_port;
    conn_info.origin = addr_port;
    conn_info.protocol = protocols[0].name;

    // 如果使用SSL加密，需要设置此选项
    conn_info.ssl_connection =
            LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_INSECURE;

    // 创建一个客户端连接
    struct lws *wsi = lws_client_connect_via_info(&conn_info);

    while (!exit_sig) {
        lws_service(context, 0);
        /**
         * 下面的调用的意义是：当连接可以接受新数据时，触发一次WRITEABLE事件回调
         * 当连接正在后台发送数据时，它不能接受新的数据写入请求，所有WRITEABLE事件回调不会执行
         */
        lws_callback_on_writable(wsi);
    }
    // 销毁上下文对象
    lws_context_destroy(context);

    return 0;
}
