#include "websocket_server/websocket_server_runtime.h"

#include <inttypes.h>
#include <stdlib.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "websocket_server/websocket_server_handlers.h"

#if CONFIG_HTTPD_WS_SUPPORT

static esp_err_t websocket_server_validate_config(void)
{
    if (CONFIG_ESPESP_WS_SERVER_PATH[0] == '\0' || CONFIG_ESPESP_WS_SERVER_PATH[0] != '/') {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "websocket path must start with '/'");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static httpd_config_t websocket_server_default_httpd_config(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_ESPESP_WS_SERVER_PORT;
    config.ctrl_port = CONFIG_ESPESP_WS_SERVER_CTRL_PORT;
    config.max_open_sockets = CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS + WS_SERVER_HANDSHAKE_SOCKET_RESERVE;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC;
    config.send_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = websocket_server_close_session;
    return config;
}

static websocket_server_config_t websocket_server_default_server_config(void)
{
    return (websocket_server_config_t) {
        .path = CONFIG_ESPESP_WS_SERVER_PATH,
        .auth_token = CONFIG_ESPESP_WS_SERVER_AUTH_TOKEN,
        .publish_period_ms = CONFIG_ESPESP_WS_SERVER_PUBLISH_PERIOD_MS,
        .max_clients = CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS,
    };
}

static websocket_server_context_t *websocket_server_context_create(const websocket_server_config_t *config)
{
    if (config == NULL) {
        return NULL;
    }

    websocket_server_context_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->config = *config;
    return ctx;
}

static void websocket_server_runtime_stop(websocket_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    if (runtime->handle != NULL) {
        httpd_stop(runtime->handle);
    }

    runtime->handle = NULL;
    runtime->context = NULL;
    runtime->httpd_config.global_user_ctx = NULL;
    runtime->httpd_config.global_user_ctx_free_fn = NULL;
}

esp_err_t websocket_server_runtime_prepare(websocket_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = websocket_server_validate_config();
    if (ret != ESP_OK) {
        return ret;
    }

    runtime->handle = NULL;
    runtime->context = NULL;
    runtime->httpd_config = websocket_server_default_httpd_config();
    runtime->server_config = websocket_server_default_server_config();
    return ESP_OK;
}

esp_err_t websocket_server_runtime_start(websocket_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    websocket_server_context_t *ctx = websocket_server_context_create(&runtime->server_config);
    if (ctx == NULL) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "failed to allocate websocket server context");
        return ESP_ERR_NO_MEM;
    }

    runtime->context = ctx;
    runtime->httpd_config.global_user_ctx = ctx;
    runtime->httpd_config.global_user_ctx_free_fn = free;

    esp_err_t ret = httpd_start(&runtime->handle, &runtime->httpd_config);
    if (ret != ESP_OK) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "failed to start websocket server: %s", esp_err_to_name(ret));
        free(ctx);
        runtime->context = NULL;
        runtime->httpd_config.global_user_ctx = NULL;
        runtime->httpd_config.global_user_ctx_free_fn = NULL;
        return ret;
    }

    ctx->server = runtime->handle;

    ret = websocket_server_register_handlers(runtime->handle, ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "failed to register websocket route: %s", esp_err_to_name(ret));
        websocket_server_runtime_stop(runtime);
        return ret;
    }

    return ESP_OK;
}

void websocket_server_runtime_log_startup(const websocket_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    ESP_LOGI(WEBSOCKET_SERVER_TAG,
             "websocket server started on port %d, path=%s, max_clients=%zu, publish_period_ms=%" PRIu32,
             runtime->httpd_config.server_port,
             runtime->server_config.path,
             runtime->server_config.max_clients,
             runtime->server_config.publish_period_ms);
    if (runtime->server_config.auth_token != NULL && runtime->server_config.auth_token[0] != '\0') {
        ESP_LOGI(WEBSOCKET_SERVER_TAG, "websocket auth is enabled with Authorization: Bearer <token>");
    } else {
        ESP_LOGI(WEBSOCKET_SERVER_TAG, "websocket auth is disabled");
    }
}

#endif
