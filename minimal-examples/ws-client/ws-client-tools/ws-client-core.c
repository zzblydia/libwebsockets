#include "ws-client-interface.h" // for WstClient
#include "ws-utils.h"
#include "ws-client-core.h"

#define MAX_PAYLOAD_SIZE 10240
#define TRACE_ID_MAX_LEN (32 + 1)

struct session_data {
    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    int len;
    int bufDataType;
    char traceId[TRACE_ID_MAX_LEN];
};

int ws_client_callback_protocol_init(struct lws *wsi, WstClient *wstClient) {
    struct lws_context *context = lws_get_context(wsi);
    lwsl_cx_user(context, "protocol init");

    char addr_port[256] = {0};
    sprintf(addr_port, "%s:%u", wstClient->serverAddress, wstClient->serverPort);

    struct lws_client_connect_info conn_info = {0};
    conn_info.context = context;
    conn_info.address = wstClient->serverAddress;
    conn_info.port = wstClient->serverPort;
    conn_info.host = addr_port;
    conn_info.origin = addr_port;
    conn_info.protocol = g_protocols[0].name;

    if (wstClient->ssl) {
        conn_info.ssl_connection =
                LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                LCCSCF_ALLOW_INSECURE;
    }

    struct lws *lws_i = lws_client_connect_via_info(&conn_info);
    if (lws_i == NULL) {
        lwsl_cx_err(context, "lws_client_connect_via_info failed\n");
        return WST_CONNECT_FAILED;
    }
    wstClient->eventCallback(wstClient->callbackIndex, WST_MSGTYPE_INIT_SUCCESS);
    return WST_SUCCESSFUL;
}

int ws_client_callback_connect_error(struct lws *wsi, WstClient *wstClient, void *in, size_t len) {
    struct lws_context *context = lws_get_context(wsi);
    lwsl_cx_user(context, "connect error");

    wstClient->recvBuf = in;
    wstClient->recvBufLen = (int) len;
    wstClient->eventCallback(wstClient->callbackIndex, WST_MSGTYPE_CONNECT_ERROR);
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

    wstClient->eventCallback(wstClient->callbackIndex, WST_MSGTYPE_CONNECT_SUCCESS);
    lwsl_cx_user(context, "client with %s:%u", wstClient->clientIp, wstClient->clientPort);
    return WST_SUCCESSFUL;
}

int ws_client_callback_received(struct lws *wsi, WstClient *wstClient, void *in, size_t len) {
    struct lws_context *context = lws_get_context(wsi);
    lwsl_cx_user(context, "receive len %lu, in %s", len, (char *) in);

    wstClient->recvBuf = in;
    wstClient->recvBufLen = (int) len;
    wstClient->eventCallback(wstClient->callbackIndex, WST_MSGTYPE_RECEIVED);
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

    wstClient->eventCallback(wstClient->callbackIndex, WST_MSGTYPE_WRITEABLE);
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
    wstClient->eventCallback(wstClient->callbackIndex, WST_MSGTYPE_DISCONNECTED);
    return WST_SUCCESSFUL;
}

int ws_client_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    struct lws_context *context = lws_get_context(wsi);
    WstClient *wstClient = (WstClient *) lws_context_user(context);
    lwsl_cx_user(context, "reason %d", reason);

    if (context == NULL || wstClient == NULL || wstClient->eventCallback == NULL) {
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

struct lws_protocols g_protocols[] = {
        {
                "ws", ws_client_callback, sizeof(struct session_data),
                MAX_PAYLOAD_SIZE, 0, NULL, 0
        },
        LWS_PROTOCOL_LIST_TERM
};