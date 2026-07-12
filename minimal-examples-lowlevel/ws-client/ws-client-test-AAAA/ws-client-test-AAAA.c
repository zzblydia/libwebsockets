#include <signal.h>
#include <libwebsockets.h>

static volatile int exit_sig = 0;
#define MAX_PAYLOAD_SIZE 1024

// 测试开关(启动参数 argv[1] 传入): 0=都不设(对照组) 1=vhost级DISABLE_IPV6 2=context级DISABLE_IPV6

void signal_handle(int sig) {
    lwsl_notice("receive signal %d", sig);
    exit_sig = 1;
}

struct session_data {
    int msg_count;
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
};

int client_callback(struct lws *wsi, enum lws_callback_reasons reason,
                    void *user, void *in, size_t len) {
    struct session_data *data = (struct session_data *) user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lwsl_notice("Connected to server ok!\n");
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            lwsl_notice("receive: %s\n", (char *) in);
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (data->msg_count < 3) {
                memset(data->buf, 0, sizeof(data->buf));
                char *msg = (char *) &data->buf[LWS_PRE];
                data->len = sprintf(msg, "hello %d", data->msg_count);
                lwsl_notice("send: %s\n", msg);
                lws_write(wsi, &data->buf[LWS_PRE],
                          (size_t) data->len, LWS_WRITE_TEXT);

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
                "ws", client_callback,
                sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0
        },
        LWS_PROTOCOL_LIST_TERM
};

void call_context(struct lws_context *context, int count)
{
    lws_service(context, 0);
}

int main(int argc, char **argv) {
    signal(SIGTERM, signal_handle);
    int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;
    lws_set_log_level(logs, NULL);

    // 从启动参数读取测试开关, 缺省 0(对照组)
    int test_disable_ipv6_at = (argc > 1) ? atoi(argv[1]) : 0;
    lwsl_notice("TEST_DISABLE_IPV6_AT=%d (0=none 1=vhost 2=context)\n",
                test_disable_ipv6_at);

    struct lws_context_creation_info ctx_info = {0};
    ctx_info.port              = CONTEXT_PORT_NO_LISTEN;
    ctx_info.iface             = NULL;
    ctx_info.protocols         = protocols;
    ctx_info.options          |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    if (test_disable_ipv6_at == 2)
        ctx_info.options      |= LWS_SERVER_OPTION_DISABLE_IPV6;    // context 级禁用 IPv6
    if (test_disable_ipv6_at == 1)
        ctx_info.options      |= LWS_SERVER_OPTION_EXPLICIT_VHOSTS; // 仅 vhost 场景才手动建 vhost

    struct lws_context *context = lws_create_context(&ctx_info);
    if (context == NULL) {
        printf("context null, exit");
        exit(-1);
    }

    struct lws_vhost *vhost;
    if (test_disable_ipv6_at == 1) {
        // vhost 级测试: 手动建 vhost, 在此单独设 DISABLE_IPV6
        struct lws_context_creation_info vh_info = {0};
        vh_info.port      = CONTEXT_PORT_NO_LISTEN;
        vh_info.protocols = protocols;
        // 裸建 vhost 需自带此选项, 否则不为其创建 client SSL 上下文(wss 会 null ssl ctx)
        vh_info.options  |= LWS_SERVER_OPTION_DISABLE_IPV6 |
                            LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

        vhost = lws_create_vhost(context, &vh_info);
        if (vhost == NULL) {
            lwsl_err("vhost create failed\n");
            exit(-1);
        }
    } else {
        // S0 / S2: 直接用 context 建好的隐式默认 vhost(已继承 ctx 的 SSL ctx)
        vhost = lws_get_vhost_by_name(context, "default");
        if (vhost == NULL) {
            lwsl_err("default vhost not found\n");
            exit(-1);
        }
    }
    lwsl_notice("vhost ready, TEST_DISABLE_IPV6_AT=%d\n", test_disable_ipv6_at);

    // 直接用上面创建的 vhost 发起连接(不再走 PROTOCOL_INIT 回调)
    struct lws_client_connect_info conn_info = {0};
    conn_info.context  = context;
    conn_info.vhost    = vhost;                   // 关键: 直接用局部 vhost
    conn_info.address  = "www.goodluck.com";      // 目标域名
    conn_info.port     = 8001;                    // 目标端口
    conn_info.host     = "goodluck.com";          // TLS SNI / Host 头
    conn_info.origin   = "goodluck.com";
    conn_info.protocol = "ws";                    // 与 server 保持一致
    conn_info.ssl_connection =
            LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED |
            LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
            LCCSCF_ALLOW_INSECURE;

    // 连接在 service 循环之前发起, 由后续 lws_service 驱动异步 DNS 与握手完成
    if (lws_client_connect_via_info(&conn_info) == NULL) {
        lwsl_err("client connect failed\n");
        lws_context_destroy(context);
        exit(-1);
    }

    int count = 0;
    while (!exit_sig) {
        count++;
        call_context(context, count);
    }

    lws_context_destroy(context);

    return 0;
}
