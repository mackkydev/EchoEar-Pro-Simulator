#include "echoear_ai_gateway_mock.h"

#include "echoear_ai_gateway.h"
#include "echoear_network_health.h"
#include "echoear_runtime_network.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    MOCK_CONNECT_SUCCESS = 0,
    MOCK_CONNECT_DNS_FAILED,
    MOCK_CONNECT_FAILED,
    MOCK_CONNECT_TLS_FAILED,
    MOCK_CONNECT_SERVER_UNAVAILABLE,
    MOCK_CONNECT_PENDING
} mock_connect_result_t;

typedef enum {
    MOCK_AUTH_SUCCESS = 0,
    MOCK_AUTH_FAILED,
    MOCK_AUTH_RATE_LIMITED,
    MOCK_AUTH_SERVER_UNAVAILABLE,
    MOCK_AUTH_PROTOCOL,
    MOCK_AUTH_PENDING
} mock_auth_result_t;

typedef struct {
    bool enabled;
    bool auto_connect;
    char gateway_url[ECHOEAR_AI_GATEWAY_URL_MAX];
    char device_id[ECHOEAR_AI_GATEWAY_DEVICE_ID_MAX];
    bool credential_ready;
    bool auto_tick;
    uint32_t now_ms;
    mock_connect_result_t connect_result;
    mock_auth_result_t auth_result;
    bool connect_trigger;
    bool session_expire_trigger;
} mock_config_t;

static mock_config_t s_mock;
static bool s_initialized;
static bool s_loaded_once;
static bool s_prev_connect_trigger;
static bool s_prev_session_expire_trigger;
static uint32_t s_auto_now_ms;
static bool s_prev_network_valid;
static bool s_prev_network_ready;
static bool s_prev_internet_ready;
static char s_last_config_signature[512];

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t n;
    if (dst == NULL || dst_size == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = '\0';
    return s;
}

static bool parse_bool(const char *value)
{
    return value != NULL &&
           (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
            strcmp(value, "yes") == 0 || strcmp(value, "on") == 0);
}

static uint32_t parse_u32(const char *value)
{
    if (value == NULL || value[0] == '\0') return 0;
    return (uint32_t)strtoul(value, NULL, 10);
}

static mock_connect_result_t parse_connect_result(const char *value)
{
    if (value == NULL || strcmp(value, "success") == 0) return MOCK_CONNECT_SUCCESS;
    if (strcmp(value, "dns_failed") == 0) return MOCK_CONNECT_DNS_FAILED;
    if (strcmp(value, "connect_failed") == 0) return MOCK_CONNECT_FAILED;
    if (strcmp(value, "tls_failed") == 0) return MOCK_CONNECT_TLS_FAILED;
    if (strcmp(value, "server_unavailable") == 0) return MOCK_CONNECT_SERVER_UNAVAILABLE;
    if (strcmp(value, "pending") == 0) return MOCK_CONNECT_PENDING;
    return MOCK_CONNECT_SUCCESS;
}

static mock_auth_result_t parse_auth_result(const char *value)
{
    if (value == NULL || strcmp(value, "success") == 0) return MOCK_AUTH_SUCCESS;
    if (strcmp(value, "auth_failed") == 0) return MOCK_AUTH_FAILED;
    if (strcmp(value, "rate_limited") == 0) return MOCK_AUTH_RATE_LIMITED;
    if (strcmp(value, "server_unavailable") == 0) return MOCK_AUTH_SERVER_UNAVAILABLE;
    if (strcmp(value, "protocol") == 0) return MOCK_AUTH_PROTOCOL;
    if (strcmp(value, "pending") == 0) return MOCK_AUTH_PENDING;
    return MOCK_AUTH_SUCCESS;
}

static const char *connect_result_name(mock_connect_result_t result)
{
    switch (result) {
        case MOCK_CONNECT_SUCCESS: return "success";
        case MOCK_CONNECT_DNS_FAILED: return "dns_failed";
        case MOCK_CONNECT_FAILED: return "connect_failed";
        case MOCK_CONNECT_TLS_FAILED: return "tls_failed";
        case MOCK_CONNECT_SERVER_UNAVAILABLE: return "server_unavailable";
        case MOCK_CONNECT_PENDING: return "pending";
        default: return "unknown";
    }
}

static const char *auth_result_name(mock_auth_result_t result)
{
    switch (result) {
        case MOCK_AUTH_SUCCESS: return "success";
        case MOCK_AUTH_FAILED: return "auth_failed";
        case MOCK_AUTH_RATE_LIMITED: return "rate_limited";
        case MOCK_AUTH_SERVER_UNAVAILABLE: return "server_unavailable";
        case MOCK_AUTH_PROTOCOL: return "protocol";
        case MOCK_AUTH_PENDING: return "pending";
        default: return "unknown";
    }
}

static echoear_ai_gateway_error_t connect_result_error(mock_connect_result_t result)
{
    switch (result) {
        case MOCK_CONNECT_DNS_FAILED: return ECHOEAR_AI_GATEWAY_ERROR_DNS_FAILED;
        case MOCK_CONNECT_FAILED: return ECHOEAR_AI_GATEWAY_ERROR_CONNECT_FAILED;
        case MOCK_CONNECT_TLS_FAILED: return ECHOEAR_AI_GATEWAY_ERROR_TLS_FAILED;
        case MOCK_CONNECT_SERVER_UNAVAILABLE: return ECHOEAR_AI_GATEWAY_ERROR_SERVER_UNAVAILABLE;
        default: return ECHOEAR_AI_GATEWAY_ERROR_NONE;
    }
}

static echoear_ai_gateway_error_t auth_result_error(mock_auth_result_t result)
{
    switch (result) {
        case MOCK_AUTH_FAILED: return ECHOEAR_AI_GATEWAY_ERROR_AUTH_FAILED;
        case MOCK_AUTH_RATE_LIMITED: return ECHOEAR_AI_GATEWAY_ERROR_RATE_LIMITED;
        case MOCK_AUTH_SERVER_UNAVAILABLE: return ECHOEAR_AI_GATEWAY_ERROR_SERVER_UNAVAILABLE;
        case MOCK_AUTH_PROTOCOL: return ECHOEAR_AI_GATEWAY_ERROR_PROTOCOL;
        default: return ECHOEAR_AI_GATEWAY_ERROR_NONE;
    }
}

static bool runtime_network_ready(void)
{
    echoear_runtime_network_t *network = echoear_runtime_network_get();
    return network != NULL &&
           network->associated &&
           network->ip_ready;
}

static bool runtime_internet_ready(void)
{
    echoear_network_health_t *health = echoear_network_health_get();
    return health != NULL &&
           health->internet_reachable;
}

static void build_signature(char *buffer, size_t size, const mock_config_t *cfg)
{
    snprintf(buffer, size, "%d|%d|%s|%s|%d",
             cfg->enabled ? 1 : 0,
             cfg->auto_connect ? 1 : 0,
             cfg->gateway_url,
             cfg->device_id,
             cfg->credential_ready ? 1 : 0);
}

static void apply_config_if_changed(void)
{
    char signature[512];
    echoear_ai_gateway_config_t config;

    build_signature(signature, sizeof(signature), &s_mock);
    if (s_loaded_once && strcmp(signature, s_last_config_signature) == 0) return;

    copy_string(s_last_config_signature, sizeof(s_last_config_signature), signature);
    s_loaded_once = true;

    config = echoear_ai_gateway_default_config();
    config.enabled = s_mock.enabled;
    config.auto_connect = s_mock.auto_connect;
    copy_string(config.gateway_url, sizeof(config.gateway_url), s_mock.gateway_url);

    (void)echoear_ai_gateway_configure(&config);
    (void)echoear_ai_gateway_set_device_id(s_mock.device_id);
    echoear_ai_gateway_set_credential_ready(s_mock.credential_ready);

    printf("[AIGatewayConfig] enabled=%d auto_connect=%d url=%s device_id=%s credential=%d max_retries=%u base_delay=%u max_delay=%u\n",
           config.enabled ? 1 : 0,
           config.auto_connect ? 1 : 0,
           config.gateway_url[0] ? config.gateway_url : "<none>",
           s_mock.device_id[0] ? s_mock.device_id : "<none>",
           s_mock.credential_ready ? 1 : 0,
           (unsigned)config.max_retries,
           (unsigned)config.retry_base_delay_ms,
           (unsigned)config.retry_max_delay_ms);
}

static void apply_connectivity_if_changed(void)
{
    bool network_ready = runtime_network_ready();
    bool internet_ready = runtime_internet_ready();

    if (!s_prev_network_valid ||
        network_ready != s_prev_network_ready ||
        internet_ready != s_prev_internet_ready) {
        echoear_ai_gateway_set_connectivity(network_ready, internet_ready);
        s_prev_network_valid = true;
        s_prev_network_ready = network_ready;
        s_prev_internet_ready = internet_ready;
    }
}

static void run_transport_attempt(void)
{
    echoear_ai_gateway_t *gateway = echoear_ai_gateway_get();

    if (!echoear_ai_gateway_should_connect()) return;
    if (!echoear_ai_gateway_begin_connect()) return;

    printf("[AIGatewayConnect] attempt=%u result=%s now=%u\n",
           (unsigned)gateway->connect_count,
           connect_result_name(s_mock.connect_result),
           (unsigned)s_mock.now_ms);

    if (s_mock.connect_result == MOCK_CONNECT_PENDING) return;
    if (s_mock.connect_result != MOCK_CONNECT_SUCCESS) {
        echoear_ai_gateway_fail(connect_result_error(s_mock.connect_result));
        return;
    }

    echoear_ai_gateway_transport_connected();

    printf("[AIGatewayAuth] attempt=%u result=%s now=%u\n",
           (unsigned)gateway->auth_count,
           auth_result_name(s_mock.auth_result),
           (unsigned)s_mock.now_ms);

    if (s_mock.auth_result == MOCK_AUTH_PENDING) return;
    if (s_mock.auth_result != MOCK_AUTH_SUCCESS) {
        echoear_ai_gateway_fail(auth_result_error(s_mock.auth_result));
        return;
    }

    echoear_ai_gateway_auth_succeeded();
    printf("[AIGatewayReady] session=%u now=%u\n",
           (unsigned)gateway->session_count,
           (unsigned)s_mock.now_ms);
}

static void print_snapshot(void)
{
    echoear_ai_gateway_t *gateway = echoear_ai_gateway_get();
    printf("[AIGateway] state=%s error=%s network=%d internet=%d transport=%d authenticated=%d session=%d connect_request=%d retry=%u/%u delay=%u due=%u now=%u connects=%u auth=%u sessions=%u failures=%u generation=%u\n",
           echoear_ai_gateway_state_name(gateway->state),
           echoear_ai_gateway_error_name(gateway->error),
           gateway->network_ready ? 1 : 0,
           gateway->internet_ready ? 1 : 0,
           gateway->transport_ready ? 1 : 0,
           gateway->authenticated ? 1 : 0,
           gateway->session_ready ? 1 : 0,
           gateway->connect_request ? 1 : 0,
           (unsigned)gateway->retry_attempt,
           (unsigned)gateway->config.max_retries,
           (unsigned)gateway->retry_delay_ms,
           (unsigned)gateway->retry_due_ms,
           (unsigned)gateway->now_ms,
           (unsigned)gateway->connect_count,
           (unsigned)gateway->auth_count,
           (unsigned)gateway->session_count,
           (unsigned)gateway->failure_count,
           (unsigned)gateway->generation);
}

void echoear_ai_gateway_mock_init(void)
{
    memset(&s_mock, 0, sizeof(s_mock));
    s_mock.enabled = true;
    s_mock.auto_connect = true;
    copy_string(s_mock.gateway_url, sizeof(s_mock.gateway_url), "https://gateway.echoear.local/v1");
    copy_string(s_mock.device_id, sizeof(s_mock.device_id), "echoear-sim-a82f");
    s_mock.credential_ready = true;
    s_mock.auto_tick = true;
    s_mock.connect_result = MOCK_CONNECT_SUCCESS;
    s_mock.auth_result = MOCK_AUTH_SUCCESS;

    s_initialized = true;
    s_loaded_once = false;
    s_prev_connect_trigger = false;
    s_prev_session_expire_trigger = false;
    s_auto_now_ms = 0;
    s_prev_network_valid = false;
    s_last_config_signature[0] = '\0';
}

void echoear_ai_gateway_mock_load(const char *path)
{
    FILE *fp;
    char line[512];
    uint32_t effective_now;

    if (!s_initialized) echoear_ai_gateway_mock_init();

    fp = fopen(path, "rb");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            char *key;
            char *value;
            char *eq;

            key = trim(line);
            if (key[0] == '\0' || key[0] == '#') continue;
            eq = strchr(key, '=');
            if (eq == NULL) continue;

            *eq = '\0';
            value = trim(eq + 1);
            key = trim(key);

            if (strcmp(key, "ai_gateway_enabled") == 0) s_mock.enabled = parse_bool(value);
            else if (strcmp(key, "ai_gateway_auto_connect") == 0) s_mock.auto_connect = parse_bool(value);
            else if (strcmp(key, "ai_gateway_url") == 0) copy_string(s_mock.gateway_url, sizeof(s_mock.gateway_url), value);
            else if (strcmp(key, "ai_gateway_device_id") == 0) copy_string(s_mock.device_id, sizeof(s_mock.device_id), value);
            else if (strcmp(key, "ai_gateway_credential_ready") == 0) s_mock.credential_ready = parse_bool(value);
            else if (strcmp(key, "ai_gateway_auto_tick") == 0) s_mock.auto_tick = parse_bool(value);
            else if (strcmp(key, "ai_gateway_now_ms") == 0) s_mock.now_ms = parse_u32(value);
            else if (strcmp(key, "ai_gateway_connect_result") == 0) s_mock.connect_result = parse_connect_result(value);
            else if (strcmp(key, "ai_gateway_auth_result") == 0) s_mock.auth_result = parse_auth_result(value);
            else if (strcmp(key, "ai_gateway_connect_trigger") == 0) s_mock.connect_trigger = parse_bool(value);
            else if (strcmp(key, "ai_gateway_session_expire_trigger") == 0) s_mock.session_expire_trigger = parse_bool(value);
        }
        fclose(fp);
    }

    apply_config_if_changed();
    apply_connectivity_if_changed();

    effective_now = s_mock.now_ms;
    if (s_mock.auto_tick) {
        if (effective_now > s_auto_now_ms) {
            s_auto_now_ms = effective_now;
        } else {
            s_auto_now_ms += 1000U;
        }
        effective_now = s_auto_now_ms;
        s_mock.now_ms = effective_now;
    }

    echoear_ai_gateway_tick(effective_now);

    if (s_mock.connect_trigger && !s_prev_connect_trigger) {
        printf("[AIGatewayManualConnect] now=%u\n", (unsigned)effective_now);
        (void)echoear_ai_gateway_request_connect();
    }

    if (s_mock.session_expire_trigger && !s_prev_session_expire_trigger) {
        printf("[AIGatewaySessionExpired] now=%u\n", (unsigned)effective_now);
        echoear_ai_gateway_session_expired();
    }

    s_prev_connect_trigger = s_mock.connect_trigger;
    s_prev_session_expire_trigger = s_mock.session_expire_trigger;

    run_transport_attempt();
    print_snapshot();
}
