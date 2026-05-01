#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include <libwebsockets.h>

#include "ws-utils.h"

#define WST_CONNECT_TIMEOUT_SECS 3

#define WS_SCHEME_WSS   "wss"
#define WS_SCHEME_WS    "ws"
#define WS_SCHEME_HTTPS "https"
#define WS_SCHEME_HTTP  "http"

typedef struct session_data {
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
    int bufDataType;

    unsigned char cache[MAX_MESSAGE_SIZE];
    int cacheLen;
} SessionData;

static EventCallback g_eventCallback = NULL; // 自定义回调函数, 初始化设置
static struct lws_context_creation_info g_contextInfo = {0}; // 全局上下文信息
static struct lws_context *g_context = NULL; // 全局上下文, 一个进程内一个.

typedef enum {
    DIR_LOCAL,
    DIR_PEER,
    DIR_BUTT
} AddrInfoDirection;

// GetAddrInfo在windows下重名
int ws_GetAddrInfo(int fd, int direction, char *ip, unsigned short *port)
{
    if (ip == NULL || port == NULL) {
        return WST_FAILED;
    }

    if (fd < 0) {
        lwsl_cx_err(g_context, "fd is invalid");
        return WST_FAILED;
    }

    // sockaddr_storage 可容纳 IPv4(16B) 和 IPv6(28B)，避免 IPv6 时栈溢出
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int ret;
    if (direction == DIR_LOCAL) {
        ret = getsockname(fd, (struct sockaddr *) &addr, &addr_len);
    } else {
        ret = getpeername(fd, (struct sockaddr *) &addr, &addr_len);
    }
    if (ret < 0) {
        return WST_FAILED;
    }

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *) &addr;
        *port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ip, INET6_ADDRSTRLEN);
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *) &addr;
        *port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ip, INET6_ADDRSTRLEN);
    } else {
        return WST_FAILED;
    }
    return WST_SUCCESSFUL;
}


int ws_client_callback_append_header(struct lws *wsi, WstClient *wstClient, void *in, size_t len)
{
    if (wsi == NULL) {
        lwsl_cx_err(g_context, "wsi is NULL");
        return WST_FAILED;
    }
    if (wstClient == NULL) {
        return WST_SUCCESSFUL; // 无自定义头域，正常跳过
    }

    unsigned char **p = (unsigned char **) in, *end = (*p) + len;
    for (int i = 0; i < WST_MAX_CUSTOM_HEADERS; i++) {
        if (wstClient->customHeaders[i].name[0] == '\0') break;
        if (lws_add_http_header_by_name(wsi,
                                        (const unsigned char *) wstClient->customHeaders[i].name,
                                        (const unsigned char *) wstClient->customHeaders[i].value,
                                        (int) strlen(wstClient->customHeaders[i].value), p, end)) {
            lwsl_wsi_err(wsi, "append header failed: %s", wstClient->customHeaders[i].name);
            return WST_FAILED;
        }
        lwsl_wsi_user(wsi, "append header: %s", wstClient->customHeaders[i].name);
    }
    return WST_SUCCESSFUL;
}

int ws_client_callback_connect_error(struct lws *wsi, WstClient *wstClient, void *in, size_t len)
{
    if (wsi == NULL) {
        lwsl_cx_err(g_context, "wsi is NULL");
        return WST_FAILED;
    }

    // wstClient 为 NULL 表示连接在 opaque_user_data 设置前就已失败（如 DNS 解析失败等极早期错误），
    // 此时无法通知上层，返回 WST_FAILED 触发 CLOSED 回调。
    if (wstClient == NULL) {
        lwsl_wsi_err(wsi, "wstClient is NULL");
        return WST_FAILED;
    }
    wstClient->recvBuf = in;
    wstClient->recvBufLen = (int) len;

    int ret = ws_GetAddrInfo(lws_get_socket_fd(wsi), DIR_LOCAL, wstClient->clientIp, &wstClient->clientPort);
    if (ret != WST_SUCCESSFUL) {
        lwsl_wsi_warn(wsi, "ws_GetAddrInfo client failed"); // 失败只是影响获取本端信息
    }

    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_CONNECT_ERROR);

    lwsl_wsi_user(wsi, "connect_error with %s:%u len %zu, reason %s", wstClient->clientIp, wstClient->clientPort,
                  len, (char *) in);
    return WST_SUCCESSFUL;
}

int ws_client_callback_established(struct lws *wsi, WstClient *wstClient)
{
    if (wsi == NULL) {
        lwsl_cx_err(g_context, "wsi is NULL");
        return WST_FAILED;
    }

    if (wstClient == NULL) {
        lwsl_wsi_err(wsi, "wstClient is NULL");
        return WST_FAILED;
    }

    lwsl_wsi_user(wsi, "connect established");

    int ret = ws_GetAddrInfo(lws_get_socket_fd(wsi), DIR_LOCAL, wstClient->clientIp, &wstClient->clientPort);
    if (ret != 0) {
        lwsl_wsi_warn(wsi, "ws_GetAddrInfo client failed");
    }
    ret = ws_GetAddrInfo(lws_get_socket_fd(wsi), DIR_PEER, wstClient->serverIp, &wstClient->serverPort);
    if (ret != 0) {
        lwsl_wsi_warn(wsi, "ws_GetAddrInfo server failed");
    }

    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_CONNECT_SUCCESS);
    lwsl_wsi_user(wsi, "client with %s:%u to server %s:%u", wstClient->clientIp, wstClient->clientPort,
                  wstClient->serverIp, wstClient->serverPort);
    return WST_SUCCESSFUL;
}

int ws_client_callback_received(struct lws *wsi, WstClient *wstClient, void *in, size_t len, void *user)
{
    if (wsi == NULL) {
        lwsl_cx_err(g_context, "wsi is NULL");
        return WST_FAILED;
    }
    if (wstClient == NULL) {
        lwsl_wsi_err(wsi, "wstClient is NULL");
        return WST_FAILED;
    }
    if (user == NULL) {
        lwsl_wsi_err(wsi, "user is NULL");
        return WST_FAILED;
    }

    SessionData *data = (SessionData *) user;
    int first = lws_is_first_fragment(wsi);
    int last = lws_is_final_fragment(wsi);
    int is_binary = lws_frame_is_binary(wsi);
    size_t remain_len = lws_remaining_packet_payload(wsi);

    lwsl_wsi_user(wsi, "received len %zu, first %d, final %d, binary %d, remain %zu, cached %d",
                  len, first, last, is_binary, remain_len, data->cacheLen);

    if (is_binary) {
        // 二进制不需要等分片完整，上层按块处理
        wstClient->recvBuf = (char *) in;
        wstClient->recvBufLen = (int) len;
        wstClient->bufDataType = LWS_WRITE_BINARY;
        g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_RECEIVED);
        return WST_SUCCESSFUL;
    }

    // 文本需要等所有分片及同一帧的所有 lws 拆包全部到达才回调
    // 原因：文本数据可能是 base64 或 JSON 格式，截断会导致上层解析失败
    if (first) {
        data->cacheLen = 0;
    }

    if ((int) (len + remain_len) > (int) sizeof(data->cache)) {
        lwsl_wsi_warn(wsi, "text msg frame too large: %zu + %zu > %zu, ignoring",
                      len, remain_len, sizeof(data->cache));
        data->cacheLen = 0;
        return WST_SUCCESSFUL;
    }

    if (data->cacheLen + (int) len > (int) sizeof(data->cache)) {
        lwsl_wsi_warn(wsi, "text msg too large: cached %d + %zu > %zu, ignoring",
                      data->cacheLen, len, sizeof(data->cache));
        data->cacheLen = 0;
        return WST_SUCCESSFUL;
    }

    memcpy(data->cache + data->cacheLen, in, len);
    data->cacheLen += (int) len;

    if (!last) {
        return WST_SUCCESSFUL;
    }

    wstClient->recvBuf = (char *) data->cache;
    wstClient->recvBufLen = data->cacheLen;
    wstClient->bufDataType = LWS_WRITE_TEXT;
    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_RECEIVED);
    return WST_SUCCESSFUL;
}

int ws_client_callback_writeable(struct lws *wsi, WstClient *wstClient, void *user)
{
    if (wsi == NULL) {
        lwsl_cx_err(g_context, "wsi is NULL");
        return WST_FAILED;
    }
    if (wstClient == NULL || user == NULL) {
        lwsl_wsi_err(wsi, "wstClient or user is NULL");
        return WST_FAILED;
    }

    struct session_data *data = (struct session_data *) user;
    data->len = 0;

    wstClient->sendBuf = (char *) &data->buf[LWS_PRE];
    wstClient->sendBufLen = &data->len;
    wstClient->maxSendBufLen = MAX_PAYLOAD_SIZE;
    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_WRITEABLE); // 回调获取要发送的数据
    data->bufDataType = wstClient->bufDataType;

    if (data->bufDataType == LWS_WRITE_TEXT) {
        lwsl_wsi_user(wsi, "send text len %d, data %s", data->len, (char *) &data->buf[LWS_PRE]);
    } else {
        lwsl_wsi_user(wsi, "send binary len %d", data->len);
    }

    if (data->len > 0) {
        int n = lws_write(wsi, &data->buf[LWS_PRE], (size_t) data->len, data->bufDataType);
        if (n != data->len) {
            lwsl_wsi_err(wsi, "lws_write failed: wrote %d of %d", n, data->len);
            return WST_FAILED;
        }
    }

    return WST_SUCCESSFUL;
}

int ws_client_callback_closed(struct lws *wsi, WstClient *wstClient)
{
    if (wsi == NULL) {
        lwsl_cx_err(g_context, "wsi is NULL");
        return WST_FAILED;
    }

    // wstClient 为 NULL 表示连接在 opaque_user_data 设置前就已关闭（极早期失败），
    // 此时无法通知上层，返回 WST_FAILED。
    if (wstClient == NULL) {
        lwsl_wsi_err(wsi, "wstClient is NULL");
        return WST_FAILED;
    }

    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_DISCONNECTED);
    return WST_SUCCESSFUL;
}


static int ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

struct lws_protocols g_protocols[] = {
    {WS_SCHEME_WSS, ws_client_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0},
    {WS_SCHEME_HTTPS, ws_client_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0},
    {WS_SCHEME_WS, ws_client_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0},
    {WS_SCHEME_HTTP, ws_client_callback, sizeof(struct session_data), MAX_PAYLOAD_SIZE, 0, NULL, 0},
    LWS_PROTOCOL_LIST_TERM
};

// 注意此函数只能返回0或-1(表示断开)
int ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    if (g_eventCallback == NULL) {
        lwsl_wsi_err(wsi, "eventCallback is NULL");
        return WST_FAILED;
    }
    WstClient *wstClient = (WstClient *) lws_get_opaque_user_data(wsi);

    lwsl_wsi_user(wsi, "reason %d", reason);
    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            lwsl_wsi_notice(wsi, "protocol_init");
            break;
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
            return ws_client_callback_append_header(wsi, wstClient, in, len);
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            return ws_client_callback_connect_error(wsi, wstClient, in, len);
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            return ws_client_callback_established(wsi, wstClient);
        case LWS_CALLBACK_CLIENT_RECEIVE:
            return ws_client_callback_received(wsi, wstClient, in, len, user);
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            return ws_client_callback_writeable(wsi, wstClient, user);
        case LWS_CALLBACK_CLIENT_CLOSED:
            return ws_client_callback_closed(wsi, wstClient);
        default:
            break;
    }
    return 0;
}

int WstInit(EventCallback eventCallback, Log2File log2file)
{
    if (eventCallback == NULL) {
        lwsl_err("WstInit eventCallback NULL\n");
        return WST_INPUT_NULL;
    }

    if (g_context != NULL) {
        lwsl_err("WstInit double init\n");
        return WST_DOUBLE_INIT;
    }

    g_eventCallback = eventCallback;
    int logLevel = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;
    if (!log2file) {
        lws_set_log_level(logLevel, NULL);
    } else {
        lws_set_log_level(logLevel, log2file);
    }

    // 设置全局context属性
    g_contextInfo.port = CONTEXT_PORT_NO_LISTEN;
    g_contextInfo.protocols = g_protocols;
    g_contextInfo.pt_serv_buf_size = MAX_PAYLOAD_SIZE;
    g_contextInfo.connect_timeout_secs = WST_CONNECT_TIMEOUT_SECS; // 连接超时时间
    g_contextInfo.timeout_secs = WST_CONNECT_TIMEOUT_SECS; // 超时时间

    g_context = lws_create_context(&g_contextInfo);
    if (g_context == NULL) {
        lwsl_err("create context failed\n");
        return WST_CREATE_CONTEXT_FAILED;
    }

    return WST_SUCCESSFUL;
}

struct lws_vhost *create_vhost(const char *vhost_name, const char *certPath, char serverUriType)
{
    struct lws_vhost *vh = lws_get_vhost_by_name(g_context, vhost_name);
    if (vh != NULL) {
        return vh;
    }

    struct lws_context_creation_info create_info = g_contextInfo;
    create_info.vhost_name = vhost_name;
    create_info.options |= LWS_SERVER_OPTION_EXPLICIT_VHOSTS;

    // 域名场景禁用 IPv6, 避免触发 AAAA DNS 请求
    if (serverUriType == URI_TYPE_DOMAIN) {
        create_info.options |= LWS_SERVER_OPTION_DISABLE_IPV6;
    }

    // wss/https 需要 SSL global init, 否则客户端 SSL_CTX 为空导致 SSL_new 失败
    if (!strncmp(vhost_name, WS_SCHEME_WSS, strlen(WS_SCHEME_WSS)) ||
        !strncmp(vhost_name, WS_SCHEME_HTTPS, strlen(WS_SCHEME_HTTPS))) {
        create_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }

    if (certPath != NULL && certPath[0] != '\0') {
        create_info.client_ssl_cert_filepath = certPath;
    }

    return lws_create_vhost(g_context, &create_info);
}

int ParseServerUrl(const char *serverUri, char *addr, int *port, char *path)
{
    if (!serverUri || !addr || !port || !path) { return -1; }

    const char *pos = serverUri;

    int default_port = 80; // 跳过协议头，同时确定默认端口
    const char *protocol_sep = strstr(pos, "://");
    if (protocol_sep) {
        if (!strncmp(serverUri, WS_SCHEME_WSS, strlen(WS_SCHEME_WSS)) ||
            !strncmp(serverUri, WS_SCHEME_HTTPS, strlen(WS_SCHEME_HTTPS))) {
            default_port = 443; // wss/https 默认端口为 443
        }
        pos = protocol_sep + 3;
    }

    // 2. 查找 Host 和 Path 的分界点
    const char *path_start = strchr(pos, '/');
    size_t host_len;
    if (path_start) {
        host_len = (size_t) (path_start - pos);
        snprintf(path, WS_GENERAL_LEN_256, "%s", path_start);
    } else {
        host_len = strlen(pos);
        snprintf(path, WS_GENERAL_LEN_256, "/");
    }

    // 3. 在 Host 部分查找端口号
    char host_tmp[WS_GENERAL_LEN_128] = {0};
    if (host_len >= sizeof(host_tmp)) return -1;
    memcpy(host_tmp, pos, host_len);

    if (host_tmp[0] == '[') {
        // IPv6 格式：[addr] 或 [addr]:port
        char *bracket_end = strchr(host_tmp, ']');
        if (!bracket_end) return -1;
        *bracket_end = '\0';
        snprintf(addr, WS_GENERAL_LEN_128, "%s", host_tmp + 1); // 去掉前置 '['
        char *p = strchr(bracket_end + 1, ':');
        *port = p ? atoi(p + 1) : default_port;
    } else {
        char *port_sep = strchr(host_tmp, ':');
        if (port_sep) {
            *port_sep = '\0';
            *port = atoi(port_sep + 1);
        } else {
            *port = default_port;
        }
        snprintf(addr, WS_GENERAL_LEN_128, "%s", host_tmp);
    }

    return 0;
}

struct lws *create_ws_connection(struct lws_vhost *vhost, const char *serverUri, void *userData)
{
    char address[WS_GENERAL_LEN_128] = {0};
    int port = 0;
    char path[WS_GENERAL_LEN_256] = {0};
    char authority[WS_GENERAL_LEN_256] = {0};

    if (ParseServerUrl(serverUri, address, &port, path) != 0) {
        lwsl_cx_err(g_context, "ParseServerUrl failed for %s", serverUri);
        return NULL;
    }

    lwsl_cx_user(g_context, "create_connection server [%s:%u]", address, port);
    snprintf(authority, sizeof(authority), "%s:%d", address, port);

    struct lws_client_connect_info conn_info = {0};
    conn_info.vhost = vhost;
    conn_info.context = g_context;
    conn_info.address = address;
    conn_info.port = port;
    conn_info.path = path;
    conn_info.host = authority;
    conn_info.origin = authority;
    conn_info.opaque_user_data = userData;

    /* 待确认
     当前 g_protocols 用 "wss"/"ws"/"https"/"http" 作为协议处理器名，conn_info.protocol 也赋相同值。lws 会将 conn_info.protocol 作为 Sec-WebSocket-Protocol 请求头发给服务端：
     Sec-WebSocket-Protocol: wss
     服务端若不认识这个子协议（大多数服务端不接受），握手会失败。正确做法是两个名字分开：本地处理器名用 conn_info.local_protocol_name，远端子协议名用 conn_info.protocol（不需要协商时传
      NULL）。
    */

    if (!strncmp(serverUri, WS_SCHEME_WSS, strlen(WS_SCHEME_WSS))) {
        conn_info.local_protocol_name = WS_SCHEME_WSS;
        conn_info.protocol = NULL; // 不发送 Sec-WebSocket-Protocol，兼容不声明子协议的服务端
        conn_info.ssl_connection =
                LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                LCCSCF_ALLOW_INSECURE;
    } else if (!strncmp(serverUri, WS_SCHEME_WS, strlen(WS_SCHEME_WS))) {
        conn_info.local_protocol_name = WS_SCHEME_WS;
        conn_info.protocol = NULL;
        conn_info.ssl_connection = 0;
    } else if (!strncmp(serverUri, WS_SCHEME_HTTPS, strlen(WS_SCHEME_HTTPS))) {
        conn_info.local_protocol_name = WS_SCHEME_HTTPS;
        conn_info.protocol = NULL;
        conn_info.ssl_connection =
                LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                LCCSCF_ALLOW_INSECURE;
    } else if (!strncmp(serverUri, WS_SCHEME_HTTP, strlen(WS_SCHEME_HTTP))) {
        conn_info.local_protocol_name = WS_SCHEME_HTTP;
        conn_info.protocol = NULL;
        conn_info.ssl_connection = 0;
    }

    return lws_client_connect_via_info(&conn_info);
}

int WstConnect(WstClient *wstClient)
{
    if (wstClient == NULL) {
        lwsl_cx_err(g_context, "%s wstClient is NULL", __FUNCTION__);
        return WST_INPUT_NULL;
    }

    lwsl_cx_user(g_context, "connect to server %s with certPath %s", wstClient->serverUri, wstClient->certPath);

    if (wstClient->serverUriType >= URI_TYPE_BUTT) {
        lwsl_cx_warn(g_context, "serverUriType invalid (%d), treated as URI_TYPE_IPV4", wstClient->serverUriType);
        return WST_CONNECT_FAILED_IP_TYPE_WRONG;
    }

    // 创建vhost控制域名场景禁用IPV6,否则只有tls场景下需要创建vhost
    struct lws_vhost *vhost = create_vhost(wstClient->serverUri, wstClient->certPath, wstClient->serverUriType);
    if (vhost == NULL) {
        lwsl_cx_err(g_context, "create_vhost failed");
        return WST_CONNECT_FAILED;
    }

    struct lws *wsi = create_ws_connection(vhost, wstClient->serverUri, wstClient);
    if (wsi == NULL) {
        lwsl_cx_err(g_context, "lws_client_connect_via_info failed");
        return WST_CONNECT_FAILED;
    }
    wstClient->wsi = wsi;

    return WST_SUCCESSFUL;
}

void WstPoll()
{
    // -1表示立即返回;0表示智能阻塞
    lws_service(g_context, -1);
}

int WstSend(WstClient *wstClient)
{
    if (wstClient == NULL || wstClient->wsi == NULL) {
        lwsl_cx_err(g_context, "%s: wstClient or wsi is NULL", __FUNCTION__);
        return WST_INPUT_NULL;
    }
    lws_callback_on_writable(wstClient->wsi);
    return WST_SUCCESSFUL;
}

int WstDisconnect(WstClient *wstClient)
{
    if (wstClient == NULL || wstClient->wsi == NULL) {
        lwsl_cx_err(g_context, "wstClient or wsi is NULL");
        return WST_INPUT_NULL;
    }
    lws_set_timeout(wstClient->wsi, PENDING_TIMEOUT_CLOSE_ACK, LWS_TO_KILL_ASYNC);
    wstClient->wsi = NULL;
    return WST_SUCCESSFUL;
}
