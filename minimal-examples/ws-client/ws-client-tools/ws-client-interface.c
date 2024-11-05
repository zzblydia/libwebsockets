#include "ws-client-core.h"
#include "ws-utils.h"
#include "ws-client-interface.h"

#define LOG_PATH_LEN 128
#define WST_CONNECT_TIMEOUT_SECS 3

int WstInit() {
    int logLevel = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;

    // 获取环境变量, 设置日志级别
    const char *log_level = getenv("LWS_LOG_LEVEL");
    if (log_level) {
        int level = atoi(log_level);
        if (0 <= level && level <= (1 << LLL_COUNT) - 1) {
            logLevel = level;
        }
    }

    // 获取环境变量, 设置日志重定向
    const char *log_stdout = getenv("LWS_LOG_STDOUT");
    if (log_stdout) {
        lws_set_log_level(logLevel, NULL);
    } else {
        lws_set_log_level(logLevel, log2file);
    }

    return WST_SUCCESSFUL;
}

int WstConnect(WstClient *wstClient) {
    if (wstClient == NULL) {
        lwsl_err("%s wstClient is NULL\n", __FUNCTION__);
        return WST_INPUT_NULL;
    }

    struct lws_context_creation_info ctx_info = {0};
    if (strlen(wstClient->clientIp)) {
        ctx_info.iface = wstClient->clientIp;
    } else {
        ctx_info.iface = NULL;
    }
    ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    ctx_info.protocols = g_protocols;
    ctx_info.user = wstClient; // 传递给回调函数的用户数据

    char log_filepath[LOG_PATH_LEN] = {0};
    sprintf(log_filepath, "./%u.log", wstClient->callbackIndex);
    ctx_info.log_filepath = log_filepath;
    ctx_info.connect_timeout_secs = WST_CONNECT_TIMEOUT_SECS; // 连接超时时间
    // ctx_info.timeout_secs = WST_CONNECT_TIMEOUT_SECS; // 超时时间

    if (wstClient->ssl) {
        ctx_info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT; // 如果使用SSL加密，需要设置此选项
        if (strlen(wstClient->certPath)) {
            ctx_info.client_ssl_cert_filepath = wstClient->certPath; // SSL单向认证无需客户端拥有证书
        }
    }

    struct lws_context *context = lws_create_context(&ctx_info);
    if (context == NULL) {
        lwsl_err("Create context failed\n");
        return WST_CREATE_CONTEXT_FAILED;
    }
    wstClient->context = context;
    return WST_SUCCESSFUL;
}

int WstPoll(WstClient *wstClient) {
    if (wstClient == NULL || wstClient->context == NULL) {
        lwsl_err("%s wstClient is NULL\n", __FUNCTION__);
        return WST_INPUT_NULL;
    }

    if (wstClient->callbackIndex == WST_CALLBACK_INDEX_NULL) {
        lws_context_destroy(wstClient->context);
        wstClient->context = NULL;
        return WST_SUCCESSFUL;
    }

    lws_service(wstClient->context, 0);
    return WST_SUCCESSFUL;
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
