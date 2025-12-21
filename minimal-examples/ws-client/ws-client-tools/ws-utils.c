#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <libwebsockets.h>

#include "ws-utils.h"
#include "ws-client-interface.h"

#define LOG_PATH_LEN 128
#define WST_CONNECT_TIMEOUT_SECS 3

#define MAX_PAYLOAD_SIZE 10240
#define TRACE_ID_MAX_LEN (32 + 1)

struct session_data {
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
    int bufDataType;
    char traceId[TRACE_ID_MAX_LEN];
};

static EventCallback *g_eventCallback = NULL; // 自定义回调函数, 初始化设置

static struct lws_context_creation_info g_contextInfo = {0}; // 全局上下文信息
static struct lws_context g_context; = {0}; // 全局上下文, 一个进程内一个.

// GetAddrInfo在windows下重名
int ws_GetAddrInfo(int fd, int direction, char *ip, unsigned short *port) {
    if (ip == NULL || port == NULL) {
        return -1;
    }

    if (fd < 0) {
        printf("lws_get_socket_fd failed\n");
        return -1;
    }

    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);
    if (direction == 0) {
        getsockname(fd, (struct sockaddr *) &addr, &addr_len);
    } else {
        getpeername(fd, (struct sockaddr *) &addr, &addr_len);
    }

    if (addr.sa_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *) &addr;
        *port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ip, INET6_ADDRSTRLEN);
    } else { // AF_INET6
        struct sockaddr_in6 *s = (struct sockaddr_in6 *) &addr;
        *port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ip, INET6_ADDRSTRLEN);
    }
    return 0;
}


int ws_client_callback_protocol_init(struct lws *wsi, WstClient *wstClient) {
    struct lws_context *context = lws_get_context(wsi);

    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_INIT_SUCCESS);
    return WST_SUCCESSFUL;
}

int ws_client_callback_connect_error(struct lws *wsi, WstClient *wstClient, void *in, size_t len) {
    struct lws_context *context = lws_get_context(wsi);
    lwsl_cx_user(context, "connect error");

    wstClient->recvBuf = in;
    wstClient->recvBufLen = (int) len;
    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_CONNECT_ERROR);
    return WST_SUCCESSFUL;
}

int ws_client_callback_established(struct lws *wsi, WstClient *wstClient) {
    struct lws_context *context = lws_get_context(wsi);
    lwsl_cx_user(context, "established");

    if (wsi == NULL || wstClient == NULL) {
        lwsl_wsi_err(wsi, "wsi or wstClient is NULL");
        return WST_INPUT_NULL;
    }
    int fd = (int) lws_get_socket_fd(wsi);
    if (fd < 0) {
        lwsl_wsi_err(wsi, "lws_get_socket_fd failed");
        return -1;
    }
    int ret = ws_GetAddrInfo(fd, 0, wstClient->clientIp, &wstClient->clientPort);
    if (ret != 0) {
        lwsl_wsi_err(wsi, "lws_server_GetClientInfo failed");
        return -1;
    }

    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_CONNECT_SUCCESS);
    lwsl_cx_user(context, "client with %s:%u", wstClient->clientIp, wstClient->clientPort);
    return WST_SUCCESSFUL;
}

int ws_client_callback_received(struct lws *wsi, WstClient *wstClient, void *in, size_t len) {
    struct lws_context *context = lws_get_context(wsi);
    lwsl_cx_user(context, "receive len %lu, in %s", len, (char *) in);

    wstClient->recvBuf = in;
    wstClient->recvBufLen = (int) len;
    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_RECEIVED);
    return WST_SUCCESSFUL;
}

int ws_client_callback_writeable(struct lws *wsi, WstClient *wstClient, void *user) {
    struct lws_context *context = lws_get_context(wsi);
    struct session_data *data = (struct session_data *) user;

    if (data == NULL || wstClient == NULL || context == NULL) {
        lwsl_cx_err(context, "data or context is NULL");
        return WST_INPUT_NULL;
    }

    memset(data->buf, 0, sizeof(data->buf));
    data->len = 0;

    wstClient->sendBuf = (char *) &data->buf[LWS_PRE];
    wstClient->sendBufLen = &data->len;
    wstClient->sendBufType = &data->bufDataType;
    wstClient->maxSendBufLen = MAX_PAYLOAD_SIZE;

    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_WRITEABLE);
    lwsl_cx_user(context, "writeable len %d, dateType %d, data %s", data->len, data->bufDataType,
                 (char *) &data->buf[LWS_PRE]);

    if (data->len != 0) {
        lws_write(wsi, &data->buf[LWS_PRE], (size_t) data->len, data->bufDataType);
    }

    return WST_SUCCESSFUL;
}

int ws_client_callback_closed(struct lws *wsi, WstClient *wstClient) {
    struct lws_context *context = lws_get_context(wsi);
    lwsl_cx_user(context, "session closed");
    g_eventCallback(wstClient->callbackIndex, WST_MSGTYPE_DISCONNECTED);
    return WST_SUCCESSFUL;
}


static int ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
struct lws_protocols g_protocols[] = {
        {
                "ws", ws_client_callback, sizeof(struct session_data),
                MAX_PAYLOAD_SIZE, 0, NULL, 0
        },
        {
                "http", ws_client_callback, sizeof(struct session_data),
                MAX_PAYLOAD_SIZE, 0, NULL, 0
        },
        LWS_PROTOCOL_LIST_TERM
};

int ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    WstClient *wstClient = (WstClient *) lws_wsi_user(wsi);
    lwsl_cx_user(context, "reason %d", reason);
    

    if (context == NULL || wstClient == NULL || g_eventCallback == NULL) {
        lwsl_cx_err(context, "wstClient or eventCallback is NULL");
        return WST_FAILED;  // -1 可能断开连接?
    }

    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            ws_client_callback_protocol_init(wsi, wstClient);
            break;
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            ws_client_callback_connect_error(wsi, wstClient, in, len);
            break;
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            ws_client_callback_established(wsi, wstClient);
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            ws_client_callback_received(wsi, wstClient, in, len);
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            ws_client_callback_writeable(wsi, wstClient, user);
            break;
        case LWS_CALLBACK_CLOSED:
            ws_client_callback_closed(wsi, wstClient);
            break;
        default:
            break;
    }
    return 0;
}

// 以下为对外开放的函数
int WstInit(EventCallback eventCallback)
{
    if(eventCallback == NULL) {
        return WST_INPUT_NULL;        
    } 
    g_eventCallback = eventCallback;
    
    int logLevel = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;
    // 获取环境变量, 设置日志重定向
    const char *log_stdout = getenv("LWS_LOG_STDOUT");
    if (log_stdout) {
        lws_set_log_level(logLevel, NULL);
    } else {
        lws_set_log_level(logLevel, log2file);
    }

    // 设置全局context属性
    g_contextInfo.port = CONTEXT_PORT_NO_LISTEN;
    g_contextInfo.protocols = g_protocols;
    g_contextInfo.pt_serv_buf_size = 10240;
    g_contextInfo.connect_timeout_secs = WST_CONNECT_TIMEOUT_SECS; // 连接超时时间
    g_contextInfo.timeout_secs = WST_CONNECT_TIMEOUT_SECS; // 超时时间

    g_context = lws_create_context(&g_contextInfo);
    if (g_context == NULL) {
        lwsl_err("create context failed\n");
        return WST_CREATE_CONTEXT_FAILED;
    }

    return WST_SUCCESSFUL;
}

lws_vhost* ws_create_vhost(const char*vhost_name, const char* certPath)
{
    lws_vhost* vh = lws_get_vhost_by_name(g_context, vhost_name);
    if (vh != NULL) {
        return vh;
    }

    struct lws_context_creation_info create_info = g_contextInfo;
    create_info.vhost_name = vhost_name;
    create_info.client_ssl_cert_filepath = certPath;
    create_info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;

    return lws_create_vhost(g_context, create_info);
}

lws* ws_create_connection(lws_vhost* vhost, const char* scheme, const char* authority, int httpType, void* userData)
{
    struct lws_client_connect_info conn_info = {0};

    conn_info.vhost = vhost;

    char address[256] = {0};
    char port[6] = {0};
    char *address_end = strstr(authority, ":");
    if (address_end) {
        strcpy(address, authority, address_end - authority);
        strcpy(port, address_end + 1);
    }

    lwsl_cx_user(g_context, "ws_create_connection server [%s:%s]", address, port);

    char addr_port[256] = {0};
    // 解析服务器地址前缀, 判断连接类型http(s)/ws(s)
    sprintf(addr_port, "%s:%u", wstClient->serverAddress, wstClient->serverPort);
    conn_info.context = g_context;
    conn_info.address = address;
    conn_info.port = atoi(port);
    conn_info.host = authority;
    conn_info.origin = authority;
    conn_info.opaque_user_data = userData;

    if(!strcmp(scheme, "wss")) {
        conn_info.protocol = "wss";
        conn_info.ssl_connection =
            LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
            LCCSCF_ALLOW_INSECURE;
    } else if (!strcmp(scheme, "ws")) {
        conn_info.protocol = "ws";
        conn_info.ssl_connection = 0;
    } else if (!strcmp(scheme, "https")) {
        conn_info.protocol = "https";
        conn_info.ssl_connection =
            LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
            LCCSCF_ALLOW_INSECURE;
    } else if (!strcmp(scheme, "http")) {
        conn_info.protocol = "http";
        conn_info.ssl_connection = 0;
    }


}

int WstConnect(WstClient *wstClient)
{
    if (wstClient == NULL) {
        lwsl_cx_err(g_context, "%s wstClient is NULL", __FUNCTION__);
        return WST_INPUT_NULL;
    }

    lwsl_cx_user(g_context, "WstConnect to %s with certPath %s", wstClient->serverAddress, wstClient->certPath);

    // url---> scheme://authority/path
    char scheme[16];
    char authority[128];
    char path[256];

    // 1. 提取 Scheme (直到 "://")
    const char *scheme_end = strstr(wstClient->serverAddress, "://");
    if (scheme_end) {
        strncpy(scheme, wstClient->serverAddress, scheme_end - wstClient->serverAddress);

        // 2. 提取 Authority (从 "://" 后开始，直到下一个 "/")
        const char *auth_start = scheme_end + 3;
        const char *path_start = strchr(auth_start, '/');
        
        if (path_start) {
            strncpy(authority, auth_start, path_start - auth_start);
            // 3. 提取 Path (从 "/" 开始到结束)
            strcpy(path, path_start);
        } else {
            // 处理没有路径的情况，如 wss://192.168.2.140:8001
            strcpy(authority, auth_start);
            strcpy(path, "/");
        }
    } else {
        return WST_FAILED;
    }

    lws_vhost* vhost = NULL;
    // 如果需要加密则创建vhost,因为vhost关联不同的ssl_ctx
    if (!strcmp(scheme, "wss") || !strcmp(scheme, "https")) {
        // ip:port或者域名为vhost_name
        vhost = ws_create_vhost(authority, wstClient->certPath);
    }

    wstClient->wsi = ws_create_connection(vhost, wstClient->serverAddress);
    if (wstClient->wsi == NULL) {
        lwsl_cx_err(g_context, "lws_client_connect_via_info failed\n");
        return WST_CONNECT_FAILED;
    }

    return WST_SUCCESSFUL;
}

void WstPoll() {
    lws_service(g_context, 0);
}

int WstSend(WstClient *wstClient) {
    if (wstClient == NULL || wstClient->context == NULL) {
        lwsl_err("%s wstClient is NULL\n", __FUNCTION__);
        return WST_INPUT_NULL;
    }
    lws_callback_on_writable_all_protocol(wstClient->context, &g_protocols[0]);
    return WST_SUCCESSFUL;
}

int WstDisconnect(WstClient *wstClient) {
    if (wstClient == NULL || wstClient->context == NULL) {
        lwsl_err("client is NULL\n");
        return WST_INPUT_NULL;
    }
    lws_context_destroy(wstClient->context);
    wstClient->context = NULL;
    return WST_SUCCESSFUL;
}
