#include "echoear_ai_provider_mock.h"

#include "echoear_ai_provider.h"
#include "echoear_network_health.h"
#include "echoear_runtime_network.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 320
#define VALUE_MAX_LEN 192

typedef enum {
    MOCK_RESULT_SUCCESS = 0,
    MOCK_RESULT_PROVIDER_UNAVAILABLE,
    MOCK_RESULT_RATE_LIMITED,
    MOCK_RESULT_AUTH_FAILED,
    MOCK_RESULT_REQUEST_FAILED,
    MOCK_RESULT_PENDING
} mock_result_t;

typedef struct {
    bool enabled;
    echoear_ai_provider_id_t primary;
    echoear_ai_provider_id_t fallback;
    bool fallback_enabled;
    bool use_gateway;
    char gateway_url[ECHOEAR_AI_PROVIDER_GATEWAY_URL_MAX];
    char model[ECHOEAR_AI_PROVIDER_MODEL_MAX];
    uint32_t required_capabilities;

    bool request_trigger;
    bool auto_tick;
    uint32_t now_ms;

    mock_result_t primary_result;
    mock_result_t fallback_result;
} mock_config_t;

static bool s_initialized;
static bool s_last_trigger;
static uint32_t s_auto_now_ms = 1000;

static bool s_have_applied_config;
static echoear_ai_provider_config_t s_last_config;

static char *trim(char *text)
{
    char *end;

    if (text == NULL) {
        return NULL;
    }

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }

    return text;
}

static bool parse_bool(const char *text, bool fallback)
{
    if (text == NULL) {
        return fallback;
    }

    if (strcmp(text, "1") == 0 ||
        strcmp(text, "true") == 0 ||
        strcmp(text, "yes") == 0 ||
        strcmp(text, "on") == 0) {
        return true;
    }

    if (strcmp(text, "0") == 0 ||
        strcmp(text, "false") == 0 ||
        strcmp(text, "no") == 0 ||
        strcmp(text, "off") == 0) {
        return false;
    }

    return fallback;
}

static uint32_t capability_from_token(const char *token)
{
    if (strcmp(token, "chat") == 0) {
        return ECHOEAR_AI_CAPABILITY_CHAT;
    }
    if (strcmp(token, "tools") == 0) {
        return ECHOEAR_AI_CAPABILITY_TOOLS;
    }
    if (strcmp(token, "search") == 0) {
        return ECHOEAR_AI_CAPABILITY_SEARCH;
    }
    if (strcmp(token, "realtime_voice") == 0 ||
        strcmp(token, "voice") == 0) {
        return ECHOEAR_AI_CAPABILITY_REALTIME_VOICE;
    }
    if (strcmp(token, "vision") == 0) {
        return ECHOEAR_AI_CAPABILITY_VISION;
    }

    return ECHOEAR_AI_CAPABILITY_NONE;
}

static uint32_t parse_capabilities(const char *text)
{
    char buffer[VALUE_MAX_LEN];
    char *token;
    uint32_t capabilities = ECHOEAR_AI_CAPABILITY_NONE;

    if (text == NULL || *text == '\0') {
        return ECHOEAR_AI_CAPABILITY_CHAT |
               ECHOEAR_AI_CAPABILITY_TOOLS;
    }

    snprintf(buffer, sizeof(buffer), "%s", text);

    token = strtok(buffer, ",");
    while (token != NULL) {
        char *clean = trim(token);
        capabilities |= capability_from_token(clean);
        token = strtok(NULL, ",");
    }

    return capabilities;
}

static mock_result_t parse_result(const char *text)
{
    if (text == NULL || strcmp(text, "success") == 0) {
        return MOCK_RESULT_SUCCESS;
    }
    if (strcmp(text, "provider_unavailable") == 0) {
        return MOCK_RESULT_PROVIDER_UNAVAILABLE;
    }
    if (strcmp(text, "rate_limited") == 0) {
        return MOCK_RESULT_RATE_LIMITED;
    }
    if (strcmp(text, "auth_failed") == 0) {
        return MOCK_RESULT_AUTH_FAILED;
    }
    if (strcmp(text, "request_failed") == 0 ||
        strcmp(text, "failed") == 0) {
        return MOCK_RESULT_REQUEST_FAILED;
    }
    if (strcmp(text, "pending") == 0) {
        return MOCK_RESULT_PENDING;
    }

    return MOCK_RESULT_REQUEST_FAILED;
}

static const char *result_to_string(mock_result_t result)
{
    switch (result) {
        case MOCK_RESULT_SUCCESS:
            return "success";
        case MOCK_RESULT_PROVIDER_UNAVAILABLE:
            return "provider_unavailable";
        case MOCK_RESULT_RATE_LIMITED:
            return "rate_limited";
        case MOCK_RESULT_AUTH_FAILED:
            return "auth_failed";
        case MOCK_RESULT_REQUEST_FAILED:
            return "request_failed";
        case MOCK_RESULT_PENDING:
            return "pending";
        default:
            return "unknown";
    }
}

static echoear_ai_provider_error_t result_to_error(mock_result_t result)
{
    switch (result) {
        case MOCK_RESULT_PROVIDER_UNAVAILABLE:
            return ECHOEAR_AI_PROVIDER_ERROR_PROVIDER_UNAVAILABLE;
        case MOCK_RESULT_RATE_LIMITED:
            return ECHOEAR_AI_PROVIDER_ERROR_RATE_LIMITED;
        case MOCK_RESULT_AUTH_FAILED:
            return ECHOEAR_AI_PROVIDER_ERROR_AUTH_FAILED;
        case MOCK_RESULT_REQUEST_FAILED:
            return ECHOEAR_AI_PROVIDER_ERROR_REQUEST_FAILED;
        case MOCK_RESULT_SUCCESS:
        case MOCK_RESULT_PENDING:
        default:
            return ECHOEAR_AI_PROVIDER_ERROR_NONE;
    }
}

static mock_config_t default_config(void)
{
    mock_config_t config;
    memset(&config, 0, sizeof(config));

    config.enabled = true;
    config.primary = ECHOEAR_AI_PROVIDER_OPENAI;
    config.fallback = ECHOEAR_AI_PROVIDER_GEMINI;
    config.fallback_enabled = true;
    config.use_gateway = true;
    snprintf(
        config.gateway_url,
        sizeof(config.gateway_url),
        "%s",
        "https://gateway.echoear.local/v1");
    config.required_capabilities =
        ECHOEAR_AI_CAPABILITY_CHAT |
        ECHOEAR_AI_CAPABILITY_TOOLS;

    config.request_trigger = false;
    config.auto_tick = true;
    config.now_ms = 0;

    config.primary_result = MOCK_RESULT_SUCCESS;
    config.fallback_result = MOCK_RESULT_SUCCESS;

    return config;
}

static void load_config_file(const char *path, mock_config_t *config)
{
    FILE *file;
    char line[LINE_MAX_LEN];

    if (path == NULL || config == NULL) {
        return;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = trim(line);
        char *equals;
        char *key;
        char *value;

        if (*text == '\0' || *text == '#') {
            continue;
        }

        equals = strchr(text, '=');
        if (equals == NULL) {
            continue;
        }

        *equals = '\0';
        key = trim(text);
        value = trim(equals + 1);

        if (strcmp(key, "ai_provider_enabled") == 0) {
            config->enabled = parse_bool(value, config->enabled);
        } else if (strcmp(key, "ai_provider_primary") == 0) {
            config->primary = echoear_ai_provider_id_from_string(value);
        } else if (strcmp(key, "ai_provider_fallback") == 0) {
            config->fallback = echoear_ai_provider_id_from_string(value);
        } else if (strcmp(key, "ai_provider_fallback_enabled") == 0) {
            config->fallback_enabled =
                parse_bool(value, config->fallback_enabled);
        } else if (strcmp(key, "ai_provider_use_gateway") == 0) {
            config->use_gateway =
                parse_bool(value, config->use_gateway);
        } else if (strcmp(key, "ai_provider_gateway_url") == 0) {
            snprintf(
                config->gateway_url,
                sizeof(config->gateway_url),
                "%s",
                value);
        } else if (strcmp(key, "ai_provider_model") == 0) {
            snprintf(
                config->model,
                sizeof(config->model),
                "%s",
                value);
        } else if (strcmp(key, "ai_provider_required") == 0) {
            config->required_capabilities =
                parse_capabilities(value);
        } else if (strcmp(key, "ai_provider_request_trigger") == 0) {
            config->request_trigger =
                parse_bool(value, config->request_trigger);
        } else if (strcmp(key, "ai_provider_auto_tick") == 0) {
            config->auto_tick =
                parse_bool(value, config->auto_tick);
        } else if (strcmp(key, "ai_provider_now_ms") == 0) {
            config->now_ms = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(key, "ai_provider_primary_result") == 0) {
            config->primary_result = parse_result(value);
        } else if (strcmp(key, "ai_provider_fallback_result") == 0) {
            config->fallback_result = parse_result(value);
        }
    }

    fclose(file);
}

static bool runtime_network_ready(void)
{
    echoear_runtime_network_t *runtime =
        echoear_runtime_network_get();

    return runtime != NULL &&
           runtime->state == ECHOEAR_RUNTIME_NETWORK_READY &&
           runtime->associated &&
           runtime->ip_ready;
}

static bool internet_ready(void)
{
    echoear_network_health_t *health =
        echoear_network_health_get();

    return health != NULL &&
           health->state == ECHOEAR_NETWORK_HEALTH_INTERNET_READY &&
           health->internet_reachable;
}

static echoear_ai_provider_config_t to_provider_config(
    const mock_config_t *mock)
{
    echoear_ai_provider_config_t config =
        echoear_ai_provider_default_config();

    config.primary_provider = mock->primary;
    config.fallback_provider = mock->fallback;
    config.fallback_enabled = mock->fallback_enabled;
    config.use_gateway = mock->use_gateway;
    config.required_capabilities = mock->required_capabilities;

    snprintf(
        config.gateway_url,
        sizeof(config.gateway_url),
        "%s",
        mock->gateway_url);

    snprintf(
        config.model,
        sizeof(config.model),
        "%s",
        mock->model);

    return config;
}

static bool provider_config_equal(
    const echoear_ai_provider_config_t *a,
    const echoear_ai_provider_config_t *b)
{
    return a->primary_provider == b->primary_provider &&
           a->fallback_provider == b->fallback_provider &&
           a->fallback_enabled == b->fallback_enabled &&
           a->use_gateway == b->use_gateway &&
           a->required_capabilities == b->required_capabilities &&
           strcmp(a->model, b->model) == 0 &&
           strcmp(a->gateway_url, b->gateway_url) == 0;
}

static void print_state(uint32_t now_ms)
{
    echoear_ai_provider_t *provider =
        echoear_ai_provider_get();

    printf(
        "[AIProvider] state=%s error=%s primary=%s fallback=%s "
        "active=%s fallback_enabled=%d fallback_active=%d "
        "network=%d internet=%d request_active=%d "
        "requests=%lu success=%lu failures=%lu fallbacks=%lu "
        "required=0x%08lx now=%lu generation=%lu\n",
        echoear_ai_provider_state_to_string(provider->state),
        echoear_ai_provider_error_to_string(provider->error),
        echoear_ai_provider_id_to_string(
            provider->config.primary_provider),
        echoear_ai_provider_id_to_string(
            provider->config.fallback_provider),
        echoear_ai_provider_id_to_string(
            provider->active_provider),
        provider->config.fallback_enabled ? 1 : 0,
        provider->fallback_active ? 1 : 0,
        provider->network_ready ? 1 : 0,
        provider->internet_ready ? 1 : 0,
        provider->request_active ? 1 : 0,
        (unsigned long)provider->request_count,
        (unsigned long)provider->success_count,
        (unsigned long)provider->failure_count,
        (unsigned long)provider->fallback_count,
        (unsigned long)provider->config.required_capabilities,
        (unsigned long)now_ms,
        (unsigned long)provider->generation);
}

static bool result_allows_fallback(mock_result_t result)
{
    return result == MOCK_RESULT_PROVIDER_UNAVAILABLE ||
           result == MOCK_RESULT_RATE_LIMITED ||
           result == MOCK_RESULT_REQUEST_FAILED;
}

static bool run_provider_attempt(
    mock_result_t result,
    uint32_t required_capabilities,
    uint32_t now_ms)
{
    echoear_ai_provider_t *provider =
        echoear_ai_provider_get();
    echoear_ai_provider_id_t attempted =
        provider->active_provider;

    if (!echoear_ai_provider_begin_request(
            required_capabilities)) {
        printf(
            "[AIRequestBlocked] provider=%s error=%s now=%lu\n",
            echoear_ai_provider_id_to_string(attempted),
            echoear_ai_provider_error_to_string(
                provider->error),
            (unsigned long)now_ms);
        print_state(now_ms);
        return false;
    }

    printf(
        "[AIRequest] provider=%s result=%s now=%lu\n",
        echoear_ai_provider_id_to_string(attempted),
        result_to_string(result),
        (unsigned long)now_ms);

    if (result == MOCK_RESULT_PENDING) {
        print_state(now_ms);
        return true;
    }

    if (result == MOCK_RESULT_SUCCESS) {
        echoear_ai_provider_complete_request();
        printf(
            "[AIRequestComplete] provider=%s result=success now=%lu\n",
            echoear_ai_provider_id_to_string(attempted),
            (unsigned long)now_ms);
        print_state(now_ms);
        return true;
    }

    echoear_ai_provider_fail_request(
        result_to_error(result));

    printf(
        "[AIRequestComplete] provider=%s result=%s now=%lu\n",
        echoear_ai_provider_id_to_string(attempted),
        result_to_string(result),
        (unsigned long)now_ms);

    print_state(now_ms);
    return false;
}

void echoear_ai_provider_mock_reset(void)
{
    s_initialized = false;
    s_last_trigger = false;
    s_auto_now_ms = 1000;
    s_have_applied_config = false;
    memset(&s_last_config, 0, sizeof(s_last_config));
}

void echoear_ai_provider_mock_load(const char *path)
{
    mock_config_t mock = default_config();
    echoear_ai_provider_config_t provider_config;
    echoear_ai_provider_t *provider;
    bool network;
    bool internet;
    bool rising_edge;
    uint32_t now_ms;

    load_config_file(path, &mock);

    if (!mock.enabled) {
        return;
    }

    if (!s_initialized) {
        echoear_ai_provider_init();
        s_initialized = true;
    }

    now_ms = mock.auto_tick ? s_auto_now_ms : mock.now_ms;
    if (mock.auto_tick) {
        s_auto_now_ms += 1000;
    }

    provider_config = to_provider_config(&mock);

    if (!s_have_applied_config ||
        !provider_config_equal(
            &provider_config,
            &s_last_config)) {
        echoear_ai_provider_configure(&provider_config);
        s_last_config = provider_config;
        s_have_applied_config = true;
        printf(
            "[AIProviderConfig] primary=%s fallback=%s "
            "fallback_enabled=%d gateway=%d required=0x%08lx\n",
            echoear_ai_provider_id_to_string(
                provider_config.primary_provider),
            echoear_ai_provider_id_to_string(
                provider_config.fallback_provider),
            provider_config.fallback_enabled ? 1 : 0,
            provider_config.use_gateway ? 1 : 0,
            (unsigned long)provider_config.required_capabilities);
    }

    network = runtime_network_ready();
    internet = internet_ready();
    echoear_ai_provider_set_connectivity(network, internet);

    rising_edge =
        mock.request_trigger && !s_last_trigger;
    s_last_trigger = mock.request_trigger;

    provider = echoear_ai_provider_get();

    if (rising_edge) {
        bool primary_success;

        echoear_ai_provider_restore_primary();

        primary_success = run_provider_attempt(
            mock.primary_result,
            mock.required_capabilities,
            now_ms);

        if (!primary_success &&
            result_allows_fallback(mock.primary_result) &&
            provider->config.fallback_enabled &&
            echoear_ai_provider_activate_fallback()) {

            printf(
                "[AIFallback] from=%s to=%s reason=%s now=%lu\n",
                echoear_ai_provider_id_to_string(
                    provider->config.primary_provider),
                echoear_ai_provider_id_to_string(
                    provider->active_provider),
                result_to_string(mock.primary_result),
                (unsigned long)now_ms);

            run_provider_attempt(
                mock.fallback_result,
                mock.required_capabilities,
                now_ms);
        }
    }

    print_state(now_ms);
}
