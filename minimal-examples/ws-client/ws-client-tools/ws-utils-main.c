/*
 * ws-utils-main.c
 *
 * 全量测试 ws-utils.c：
 *   - 接口参数校验  (NULL/越界/重复初始化)
 *   - 连接生命周期  (建链/异常/服务端关闭/调用方主动断开)
 *   - 收发功能      (文本/二进制/最大载荷/空写/连续多包)
 *   - 自定义头域    (握手附加 HTTP 头)
 *   - 多连接并发    (同进程多客户端)
 *   - TLS (wss)     (自签名证书 + LCCSCF_ALLOW_*)
 *   - 性能          (echo round-trip 计时)
 *
 * 测试通过内置 echo 服务端进行客户端/服务端通信，无需外部依赖。
 *
 * 写在 main 进程的单线程内：
 *   - 服务端 lws_context 一份
 *   - 客户端 ws-utils 全局 context 一份
 *   - 主循环交替 srv_poll() + WstPoll()，配合短 sleep 防 CPU 100%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <libwebsockets.h>

#include "ws-utils.h"

#define TEST_PORT_WS    17681
#define TEST_PORT_WSS   17682
#define MAX_TEST_CLIENTS 16

/* ===================== 通用工具 ===================== */

typedef long long msec_t;

static volatile int g_exit = 0;

static void on_sigint(int sig) { (void) sig; g_exit = 1; }

static msec_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (msec_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void log2file(int level, const char *line)
{
    /* 日志重定向回调，只输出 USER/ERR/WARN，避免 NOTICE 刷屏 */
    if (level & (LLL_ERR | LLL_WARN | LLL_USER)) {
        fputs(line, stdout);
    }
}

/* ===================== 内置 echo 服务端 ===================== */

#define SRV_RECV_MAX (MAX_PAYLOAD_SIZE * 2)

struct srv_session {
    unsigned char buf[LWS_PRE + SRV_RECV_MAX];
    int len;
    int is_binary;
    int has_pending;
    int close_after_send;
    int saw_x_test_header;
};

static int g_srv_total_connect = 0;
static int g_srv_x_test_seen = 0;
static int g_srv_total_recv_bytes = 0;

static int srv_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
{
    struct srv_session *pss = (struct srv_session *) user;
    char hdr[256];
    int n;

    switch (reason) {
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            /* 握手阶段，可读取自定义头 */
            int hlen = lws_hdr_custom_length(wsi, "x-test-header:", 14);
            if (hlen > 0 && hlen < (int) sizeof(hdr)) {
                if (lws_hdr_custom_copy(wsi, hdr, sizeof(hdr),
                                        "x-test-header:", 14) > 0) {
                    g_srv_x_test_seen++;
                    lwsl_wsi_user(wsi, "server saw x-test-header: %s", hdr);
                }
            }
            break;
        }
        case LWS_CALLBACK_ESTABLISHED:
            g_srv_total_connect++;
            memset(pss, 0, sizeof(*pss));
            lwsl_wsi_user(wsi, "server: established (#%d)", g_srv_total_connect);
            break;
        case LWS_CALLBACK_RECEIVE:
            if (!len) break;
            if (pss->len + (int) len > SRV_RECV_MAX) {
                lwsl_wsi_err(wsi, "server: oversize total %d+%zu", pss->len, len);
                return -1;
            }
            /* 支持分片：累积到一个完整消息后再 echo */
            memcpy(&pss->buf[LWS_PRE] + pss->len, in, len);
            pss->len += (int) len;
            pss->is_binary = lws_frame_is_binary(wsi);
            g_srv_total_recv_bytes += (int) len;
            if (lws_is_final_fragment(wsi) &&
                lws_remaining_packet_payload(wsi) == 0) {
                pss->has_pending = 1;
                /* 文本控制消息：CLOSE_ME 表示 echo 后断链 */
                if (!pss->is_binary && pss->len == 8 &&
                    !memcmp(&pss->buf[LWS_PRE], "CLOSE_ME", 8)) {
                    pss->close_after_send = 1;
                }
                lws_callback_on_writable(wsi);
            }
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (!pss->has_pending) break;
            n = lws_write(wsi, &pss->buf[LWS_PRE], (size_t) pss->len,
                          pss->is_binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);
            if (n != pss->len) {
                lwsl_wsi_err(wsi, "server: short write %d/%d", n, pss->len);
                return -1;
            }
            pss->has_pending = 0;
            pss->len = 0;
            if (pss->close_after_send) return -1;
            break;
        case LWS_CALLBACK_CLOSED:
            lwsl_wsi_user(wsi, "server: closed");
            break;
        default:
            break;
    }
    return 0;
}

static struct lws_protocols srv_protocols[] = {
    { "ws",  srv_callback, sizeof(struct srv_session), MAX_PAYLOAD_SIZE, 0, NULL, 0 },
    { "wss", srv_callback, sizeof(struct srv_session), MAX_PAYLOAD_SIZE, 0, NULL, 0 },
    LWS_PROTOCOL_LIST_TERM
};

static struct lws_context *g_srv_ctx = NULL;
static int g_srv_has_wss = 0;

static int file_exists(const char *p)
{
    if (!p) return 0;
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static const char *find_cert(const char **candidates)
{
    for (int i = 0; candidates[i]; i++) {
        if (file_exists(candidates[i])) return candidates[i];
    }
    return NULL;
}

static int start_server(void)
{
    struct lws_context_creation_info info = {0};
    info.protocols = srv_protocols;
    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.pt_serv_buf_size = MAX_PAYLOAD_SIZE;

    g_srv_ctx = lws_create_context(&info);
    if (!g_srv_ctx) {
        lwsl_err("server: create context failed\n");
        return -1;
    }

    /* ws vhost */
    struct lws_context_creation_info vhi = {0};
    vhi.protocols = srv_protocols;
    vhi.vhost_name = "ws-vhost";
    vhi.port = TEST_PORT_WS;
    vhi.pt_serv_buf_size = MAX_PAYLOAD_SIZE;
    if (!lws_create_vhost(g_srv_ctx, &vhi)) {
        lwsl_err("server: ws vhost failed\n");
        return -1;
    }

    /* wss vhost (可选) */
    const char *cert_candidates[] = {
        "../share/libwebsockets-test-server/libwebsockets-test-server.pem",
        "../libwebsockets-test-server.pem",
        "./libwebsockets-test-server.pem",
        "/u01/libwebsockets/build/libwebsockets-test-server.pem",
        NULL
    };
    const char *key_candidates[] = {
        "../share/libwebsockets-test-server/libwebsockets-test-server.key.pem",
        "../libwebsockets-test-server.key.pem",
        "./libwebsockets-test-server.key.pem",
        "/u01/libwebsockets/build/libwebsockets-test-server.key.pem",
        NULL
    };
    const char *cert = find_cert(cert_candidates);
    const char *key = find_cert(key_candidates);
    if (cert && key) {
        struct lws_context_creation_info wssi = {0};
        wssi.protocols = srv_protocols;
        wssi.vhost_name = "wss-vhost";
        wssi.port = TEST_PORT_WSS;
        wssi.pt_serv_buf_size = MAX_PAYLOAD_SIZE;
        wssi.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        wssi.ssl_cert_filepath = cert;
        wssi.ssl_private_key_filepath = key;
        if (lws_create_vhost(g_srv_ctx, &wssi)) {
            g_srv_has_wss = 1;
            lwsl_user("server: wss enabled (cert=%s)", cert);
        } else {
            lwsl_warn("server: wss vhost failed, skipping wss tests\n");
        }
    } else {
        lwsl_warn("server: cert not found, skipping wss tests\n");
    }
    return 0;
}

static void srv_poll(void)
{
    /* -1 立即返回，由外部 usleep 控制节奏，避免 lws_service 按事件超时阻塞 */
    if (g_srv_ctx) lws_service(g_srv_ctx, -1);
}

static void stop_server(void)
{
    if (g_srv_ctx) {
        lws_context_destroy(g_srv_ctx);
        g_srv_ctx = NULL;
    }
}

/* ===================== 客户端测试上下文 ===================== */

typedef struct ClientCtx {
    int conn_success;
    int conn_error;
    int recv_count;
    int writeable_count;
    int disconnect_count;

    /* 待发送 */
    int pending_send;       /* 1 表示 WRITEABLE 时取 send_buf */
    unsigned char send_buf[MAX_PAYLOAD_SIZE];
    int send_len;
    int send_type;          /* LWS_WRITE_TEXT / LWS_WRITE_BINARY */

    /* 最近一次接收 */
    unsigned char last_recv[MAX_PAYLOAD_SIZE];
    int last_recv_len;
    int last_recv_type;     /* 0 文本 / 1 二进制 */
} ClientCtx;

static ClientCtx g_clientCtx[MAX_TEST_CLIENTS];
static WstClient g_clients[MAX_TEST_CLIENTS];

static int evt_callback(unsigned short idx, int msgType)
{
    if (idx >= MAX_TEST_CLIENTS) {
        printf("[ERR ] evt_callback: idx %u out of range\n", idx);
        return -1;
    }
    ClientCtx *c = &g_clientCtx[idx];
    WstClient *w = &g_clients[idx];

    switch (msgType) {
        case WST_MSGTYPE_CONNECT_SUCCESS:
            c->conn_success++;
            break;
        case WST_MSGTYPE_CONNECT_ERROR:
            c->conn_error++;
            break;
        case WST_MSGTYPE_RECEIVED:
            if (w->recvBuf && w->recvBufLen > 0 &&
                w->recvBufLen <= (int) sizeof(c->last_recv)) {
                memcpy(c->last_recv, w->recvBuf, (size_t) w->recvBufLen);
                c->last_recv_len = w->recvBufLen;
                c->last_recv_type = w->bufDataType;
            }
            c->recv_count++;
            break;
        case WST_MSGTYPE_WRITEABLE:
            c->writeable_count++;
            if (c->pending_send) {
                if (c->send_len > 0) {
                    memcpy(w->sendBuf, c->send_buf, (size_t) c->send_len);
                }
                *w->sendBufLen = c->send_len;
                w->bufDataType = c->send_type;
                c->pending_send = 0;
            } else {
                /* 调用方未填数据：保持 0，触发空写测试场景 */
                *w->sendBufLen = 0;
            }
            break;
        case WST_MSGTYPE_DISCONNECTED:
            c->disconnect_count++;
            break;
        default:
            break;
    }
    return 0;
}

static void poll_for(int ms)
{
    msec_t deadline = now_ms() + ms;
    while (!g_exit && now_ms() < deadline) {
        srv_poll();
        WstPoll();
        usleep(1000);
    }
}

static int wait_until(int *counter, int target, int timeout_ms)
{
    msec_t deadline = now_ms() + timeout_ms;
    while (!g_exit && now_ms() < deadline) {
        if (*counter >= target) return 1;
        srv_poll();
        WstPoll();
        usleep(1000);
    }
    return *counter >= target;
}

static void reset_client(int idx, const char *uri, char uri_type)
{
    memset(&g_clientCtx[idx], 0, sizeof(g_clientCtx[idx]));
    memset(&g_clients[idx], 0, sizeof(g_clients[idx]));
    g_clients[idx].callbackIndex = (unsigned short) idx;
    g_clients[idx].serverUriType = uri_type;
    snprintf(g_clients[idx].serverUri, WS_GENERAL_LEN_256, "%s", uri);
}

static void queue_send(int idx, const void *data, int len, int type)
{
    ClientCtx *c = &g_clientCtx[idx];
    if (len > (int) sizeof(c->send_buf)) len = (int) sizeof(c->send_buf);
    if (data && len > 0) memcpy(c->send_buf, data, (size_t) len);
    c->send_len = len;
    c->send_type = type;
    c->pending_send = 1;
}

/* ===================== 测试断言 ===================== */

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT(name, cond) do {                                 \
    if (cond) { printf("[PASS] %s\n", name); g_passed++; }      \
    else { printf("[FAIL] %s @ %s:%d\n", name, __FILE__, __LINE__); g_failed++; } \
} while (0)

/* ===================== 测试用例 ===================== */

static void test_init_param_null(void)
{
    /* WstInit 第一次调用前测试 NULL 入参 */
    int rc = WstInit(NULL, NULL);
    EXPECT("init: NULL eventCallback => WST_INPUT_NULL", rc == WST_INPUT_NULL);
}

static void test_init_normal(void)
{
    int rc = WstInit(evt_callback, log2file);
    EXPECT("init: 正常初始化 => WST_SUCCESSFUL", rc == WST_SUCCESSFUL);
}

static void test_init_double(void)
{
    int rc = WstInit(evt_callback, log2file);
    EXPECT("init: 重复初始化 => WST_DOUBLE_INIT", rc == WST_DOUBLE_INIT);
}

static void test_param_validation(void)
{
    int rc;
    rc = WstConnect(NULL);
    EXPECT("connect: NULL 入参 => WST_INPUT_NULL", rc == WST_INPUT_NULL);

    /* serverUriType 越界 */
    WstClient bad = {0};
    bad.callbackIndex = 0;
    bad.serverUriType = URI_TYPE_BUTT;
    snprintf(bad.serverUri, WS_GENERAL_LEN_256, "ws://127.0.0.1:%d/", TEST_PORT_WS);
    rc = WstConnect(&bad);
    EXPECT("connect: serverUriType 越界 => WST_CONNECT_FAILED_IP_TYPE_WRONG",
           rc == WST_CONNECT_FAILED_IP_TYPE_WRONG);

    rc = WstSend(NULL);
    EXPECT("send: NULL 入参 => WST_INPUT_NULL", rc == WST_INPUT_NULL);

    rc = WstDisconnect(NULL);
    EXPECT("disconnect: NULL 入参 => WST_INPUT_NULL", rc == WST_INPUT_NULL);

    /* wsi 为 NULL（未连接状态） */
    WstClient noconn = {0};
    rc = WstSend(&noconn);
    EXPECT("send: wsi 为 NULL => WST_INPUT_NULL", rc == WST_INPUT_NULL);
    rc = WstDisconnect(&noconn);
    EXPECT("disconnect: wsi 为 NULL => WST_INPUT_NULL", rc == WST_INPUT_NULL);
}

static void test_connect_error_unreachable(void)
{
    int idx = 0;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:1/");  /* 1 端口大概率拒接 */
    reset_client(idx, uri, URI_TYPE_IPV4);
    int rc = WstConnect(&g_clients[idx]);
    EXPECT("connect: 调用 WstConnect 返回成功（异步建链）", rc == WST_SUCCESSFUL);

    /* lws connect_timeout = 3s，留足 5s */
    int ok = wait_until(&g_clientCtx[idx].conn_error, 1, 6000);
    EXPECT("connect_error: 不可达端口触发 CONNECT_ERROR", ok);
    EXPECT("connect_error: 未触发 CONNECT_SUCCESS",
           g_clientCtx[idx].conn_success == 0);
}

static void test_invalid_uri(void)
{
    int idx = 0;
    /* 极长的 host 段，触发 ParseServerUrl 失败 */
    char uri[WS_GENERAL_LEN_256];
    char host[WS_GENERAL_LEN_128 + 16];
    memset(host, 'a', sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    snprintf(uri, sizeof(uri), "ws://%s/", host);
    reset_client(idx, uri, URI_TYPE_IPV4);
    int rc = WstConnect(&g_clients[idx]);
    EXPECT("connect: 非法 URI（host 过长）=> WST_CONNECT_FAILED",
           rc == WST_CONNECT_FAILED);
}

static int do_connect_and_wait(int idx, const char *uri, char uri_type, int timeout_ms)
{
    reset_client(idx, uri, uri_type);
    if (WstConnect(&g_clients[idx]) != WST_SUCCESSFUL) return 0;
    return wait_until(&g_clientCtx[idx].conn_success, 1, timeout_ms);
}

static int do_send_and_wait_echo(int idx, const void *data, int len, int type, int timeout_ms)
{
    int target = g_clientCtx[idx].recv_count + 1;
    queue_send(idx, data, len, type);
    if (WstSend(&g_clients[idx]) != WST_SUCCESSFUL) return 0;
    return wait_until(&g_clientCtx[idx].recv_count, target, timeout_ms);
}

static void test_echo_text(void)
{
    int idx = 0;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/echo", TEST_PORT_WS);

    int ok = do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 3000);
    EXPECT("echo-text: 建链成功", ok);
    if (!ok) return;
    EXPECT("echo-text: serverIp 已填充",
           g_clients[idx].serverIp[0] != '\0');
    EXPECT("echo-text: clientIp 已填充",
           g_clients[idx].clientIp[0] != '\0');

    const char *msg = "Hello, ws-utils!";
    ok = do_send_and_wait_echo(idx, msg, (int) strlen(msg), LWS_WRITE_TEXT, 3000);
    EXPECT("echo-text: 收到回显", ok);
    if (ok) {
        ClientCtx *c = &g_clientCtx[idx];
        EXPECT("echo-text: 数据长度匹配", c->last_recv_len == (int) strlen(msg));
        EXPECT("echo-text: 数据内容匹配",
               c->last_recv_len == (int) strlen(msg) &&
               !memcmp(c->last_recv, msg, (size_t) c->last_recv_len));
        EXPECT("echo-text: 数据类型为文本", c->last_recv_type == 0);
    }

    WstDisconnect(&g_clients[idx]);
    EXPECT("echo-text: 主动断开 => DISCONNECTED",
           wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000));
}

static void test_echo_binary(void)
{
    int idx = 1;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/", TEST_PORT_WS);
    if (!do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 3000)) {
        EXPECT("echo-binary: 建链", 0);
        return;
    }

    unsigned char payload[256];
    for (int i = 0; i < (int) sizeof(payload); i++) payload[i] = (unsigned char) (i ^ 0xA5);

    int ok = do_send_and_wait_echo(idx, payload, sizeof(payload), LWS_WRITE_BINARY, 3000);
    EXPECT("echo-binary: 收到回显", ok);
    if (ok) {
        ClientCtx *c = &g_clientCtx[idx];
        EXPECT("echo-binary: 数据长度匹配", c->last_recv_len == (int) sizeof(payload));
        EXPECT("echo-binary: 数据内容匹配",
               !memcmp(c->last_recv, payload, sizeof(payload)));
        EXPECT("echo-binary: 数据类型为二进制", c->last_recv_type == 1);
    }
    WstDisconnect(&g_clients[idx]);
    wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000);
}

static void test_echo_large(void)
{
    int idx = 2;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/", TEST_PORT_WS);
    if (!do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 3000)) {
        EXPECT("echo-large: 建链", 0);
        return;
    }

    /* 取接近 MAX_PAYLOAD_SIZE 的载荷（留余量给协议头） */
    int len = MAX_PAYLOAD_SIZE - 256;
    unsigned char *buf = malloc((size_t) len);
    if (!buf) { EXPECT("echo-large: malloc", 0); return; }
    for (int i = 0; i < len; i++) buf[i] = (unsigned char) (i & 0xFF);

    ClientCtx *c = &g_clientCtx[idx];
    int ok = do_send_and_wait_echo(idx, buf, len, LWS_WRITE_BINARY, 5000);
    EXPECT("echo-large: 收到完整 echo", ok);
    if (ok) {
        EXPECT("echo-large: 长度匹配", c->last_recv_len == len);
        EXPECT("echo-large: 内容匹配",
               c->last_recv_len == len && !memcmp(c->last_recv, buf, (size_t) len));
    }

    free(buf);
    WstDisconnect(&g_clients[idx]);
    wait_until(&c->disconnect_count, 1, 3000);
}

static void test_empty_writeable(void)
{
    int idx = 3;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/", TEST_PORT_WS);
    if (!do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 3000)) {
        EXPECT("empty-writeable: 建链", 0);
        return;
    }

    /* 不调用 queue_send：WRITEABLE 回调里 *sendBufLen 会被设 0，
     * 此场景下 ws-utils.c 应跳过 lws_write 不报错也不断链。 */
    int target_w = g_clientCtx[idx].writeable_count + 1;
    int rc = WstSend(&g_clients[idx]);
    EXPECT("empty-writeable: WstSend 返回 SUCCESS", rc == WST_SUCCESSFUL);
    EXPECT("empty-writeable: WRITEABLE 回调被触发",
           wait_until(&g_clientCtx[idx].writeable_count, target_w, 2000));

    /* 再发一条正常消息，确认连接仍存活 */
    const char *msg = "still alive";
    int ok = do_send_and_wait_echo(idx, msg, (int) strlen(msg), LWS_WRITE_TEXT, 3000);
    EXPECT("empty-writeable: 后续消息仍可正常 echo", ok);

    WstDisconnect(&g_clients[idx]);
    wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000);
}

static void test_server_initiated_close(void)
{
    int idx = 4;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/", TEST_PORT_WS);
    if (!do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 3000)) {
        EXPECT("server-close: 建链", 0);
        return;
    }
    int ok = do_send_and_wait_echo(idx, "CLOSE_ME", 8, LWS_WRITE_TEXT, 3000);
    EXPECT("server-close: 收到最后一次 echo", ok);
    /* 服务端 echo 后 return -1 触发关闭，客户端应收到 DISCONNECTED */
    EXPECT("server-close: 收到 DISCONNECTED 通知",
           wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000));
}

static void test_custom_headers(void)
{
    int idx = 5;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/", TEST_PORT_WS);

    int prev_seen = g_srv_x_test_seen;

    reset_client(idx, uri, URI_TYPE_IPV4);
    snprintf(g_clients[idx].customHeaders[0].name,
             sizeof(g_clients[idx].customHeaders[0].name), "x-test-header:");
    snprintf(g_clients[idx].customHeaders[0].value,
             sizeof(g_clients[idx].customHeaders[0].value), "ws-utils-tools-1");
    snprintf(g_clients[idx].customHeaders[1].name,
             sizeof(g_clients[idx].customHeaders[1].name), "x-extra:");
    snprintf(g_clients[idx].customHeaders[1].value,
             sizeof(g_clients[idx].customHeaders[1].value), "extra-value");

    int rc = WstConnect(&g_clients[idx]);
    EXPECT("custom-headers: WstConnect 返回 SUCCESS", rc == WST_SUCCESSFUL);
    EXPECT("custom-headers: 建链成功",
           wait_until(&g_clientCtx[idx].conn_success, 1, 3000));
    EXPECT("custom-headers: 服务端观察到 x-test-header",
           g_srv_x_test_seen > prev_seen);

    WstDisconnect(&g_clients[idx]);
    wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000);
}

static void test_concurrent_connections(void)
{
    const int N = 5;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/", TEST_PORT_WS);

    int base = 6;  /* 使用 idx 6..10 */
    int prev_total = g_srv_total_connect;

    for (int i = 0; i < N; i++) {
        reset_client(base + i, uri, URI_TYPE_IPV4);
        int rc = WstConnect(&g_clients[base + i]);
        if (rc != WST_SUCCESSFUL) {
            EXPECT("concurrent: WstConnect", 0);
            return;
        }
    }

    int all_up = 1;
    for (int i = 0; i < N; i++) {
        if (!wait_until(&g_clientCtx[base + i].conn_success, 1, 5000))
            all_up = 0;
    }
    EXPECT("concurrent: 所有连接建链成功", all_up);
    EXPECT("concurrent: 服务端记录到 N 个新连接",
           g_srv_total_connect - prev_total >= N);

    /* 各自发一条独立消息，验证 callbackIndex 路由正确 */
    int all_echoed = 1;
    for (int i = 0; i < N; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "client-%d", i);
        if (!do_send_and_wait_echo(base + i, msg, (int) strlen(msg),
                                   LWS_WRITE_TEXT, 3000)) {
            all_echoed = 0;
            break;
        }
        ClientCtx *c = &g_clientCtx[base + i];
        if (c->last_recv_len != (int) strlen(msg) ||
            memcmp(c->last_recv, msg, (size_t) c->last_recv_len) != 0) {
            all_echoed = 0;
            break;
        }
    }
    EXPECT("concurrent: 各路由独立 echo 正确", all_echoed);

    for (int i = 0; i < N; i++) WstDisconnect(&g_clients[base + i]);
    int all_down = 1;
    for (int i = 0; i < N; i++) {
        if (!wait_until(&g_clientCtx[base + i].disconnect_count, 1, 3000))
            all_down = 0;
    }
    EXPECT("concurrent: 全部正常断开", all_down);
}

static void test_throughput(void)
{
    int idx = 11;
    char uri[64];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/", TEST_PORT_WS);
    if (!do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 3000)) {
        EXPECT("throughput: 建链", 0);
        return;
    }

    const int N = 100;
    const char *msg = "PERFTEST";
    int len = (int) strlen(msg);
    msec_t t0 = now_ms();
    int ok = 1;
    for (int i = 0; i < N; i++) {
        if (!do_send_and_wait_echo(idx, msg, len, LWS_WRITE_TEXT, 5000)) {
            ok = 0;
            break;
        }
    }
    msec_t elapsed = now_ms() - t0;
    EXPECT("throughput: 100 次 round-trip 完成", ok);
    if (ok) {
        printf("[INFO] throughput: %d round-trips, %lld ms (avg %.3f ms/op)\n",
               N, elapsed, (double) elapsed / N);
    }

    WstDisconnect(&g_clients[idx]);
    wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000);
}

static void test_tls_echo(void)
{
    if (!g_srv_has_wss) {
        printf("[SKIP] tls-echo: wss 未启用\n");
        return;
    }
    int idx = 12;
    char uri[64];
    snprintf(uri, sizeof(uri), "wss://127.0.0.1:%d/", TEST_PORT_WSS);
    if (!do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 5000)) {
        EXPECT("tls-echo: 建链", 0);
        return;
    }
    const char *msg = "hello-over-tls";
    int ok = do_send_and_wait_echo(idx, msg, (int) strlen(msg), LWS_WRITE_TEXT, 3000);
    EXPECT("tls-echo: 收到 echo", ok);
    if (ok) {
        ClientCtx *c = &g_clientCtx[idx];
        EXPECT("tls-echo: 内容匹配",
               c->last_recv_len == (int) strlen(msg) &&
               !memcmp(c->last_recv, msg, (size_t) c->last_recv_len));
    }
    WstDisconnect(&g_clients[idx]);
    wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000);
}

static void test_path_with_subpath(void)
{
    int idx = 13;
    char uri[96];
    snprintf(uri, sizeof(uri), "ws://127.0.0.1:%d/some/sub/path?x=1", TEST_PORT_WS);
    int ok = do_connect_and_wait(idx, uri, URI_TYPE_IPV4, 3000);
    EXPECT("path-subpath: 子路径 + 查询串建链成功", ok);
    if (ok) {
        const char *msg = "ping-subpath";
        EXPECT("path-subpath: 子路径下 echo 正常",
               do_send_and_wait_echo(idx, msg, (int) strlen(msg),
                                     LWS_WRITE_TEXT, 3000));
        WstDisconnect(&g_clients[idx]);
        wait_until(&g_clientCtx[idx].disconnect_count, 1, 3000);
    }
}

/* ===================== 测试编排 ===================== */

static void run_all_tests(void)
{
    /* 这些必须在 WstInit 之前 */
    test_init_param_null();
    test_init_normal();
    test_init_double();

    /* 启动服务端必须晚于 WstInit？不，服务端独立 context，与初始化顺序无关。
     * 但参数校验测试需要 g_context 已存在（WstConnect/WstSend/WstDisconnect 都会
     * 走 lwsl_cx_err 输出），先 init 再做这些测试更合理。 */
    test_param_validation();

    /* 需要服务端的用例 */
    test_connect_error_unreachable();
    test_invalid_uri();
    test_echo_text();
    test_echo_binary();
    test_echo_large();
    test_empty_writeable();
    test_server_initiated_close();
    test_custom_headers();
    test_path_with_subpath();
    test_concurrent_connections();
    test_throughput();
    test_tls_echo();
}

int main(void)
{
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    /* 服务端 context 独立，先于 WstInit 也无所谓 */
    if (start_server() != 0) {
        printf("[FATAL] 启动内置服务端失败\n");
        return 2;
    }
    /* 让服务端 listen 完成 */
    poll_for(50);

    run_all_tests();

    printf("\n========================================\n");
    printf("Tests passed: %d\n", g_passed);
    printf("Tests failed: %d\n", g_failed);
    printf("Server total connects: %d\n", g_srv_total_connect);
    printf("Server total recv bytes: %d\n", g_srv_total_recv_bytes);
    printf("========================================\n");

    stop_server();
    return g_failed == 0 ? 0 : 1;
}
