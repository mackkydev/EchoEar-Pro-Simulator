#include "echoear_network_health_mock.h"

#include "echoear_network_health.h"
#include "echoear_runtime_network.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    MOCK_HEALTH_INTERNET_READY = 0,
    MOCK_HEALTH_INTERNET_DOWN,
    MOCK_HEALTH_GATEWAY_DOWN,
    MOCK_HEALTH_DNS_DOWN,
    MOCK_HEALTH_PENDING,
    MOCK_HEALTH_INVALID
} mock_health_result_t;

typedef struct {
    bool enabled;
    bool auto_tick;
    uint32_t now_ms;
    mock_health_result_t result;
    uint32_t latency_ms;
} mock_config_t;

static bool s_clock_initialized = false;
static uint32_t s_auto_now_ms = 0U;

static bool s_network_state_initialized = false;
static bool s_last_network_ready = false;

static void trim(char *text)
{
    size_t len;

    if (text == NULL) {
        return;
    }

    len = strlen(text);

    while (len > 0U &&
           (text[len - 1U] == '\r' ||
            text[len - 1U] == '\n' ||
            text[len - 1U] == ' ' ||
            text[len - 1U] == '\t')) {
        text[len - 1U] = '\0';
        len--;
    }

    while (*text == ' ' || *text == '\t') {
        memmove(text, text + 1, strlen(text));
    }

    /* Strip UTF-8 BOM when a config file was saved by an editor that added it. */
    if ((unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) {
        memmove(text, text + 3, strlen(text + 3) + 1U);
    }
}

static bool parse_bool(const char *value)
{
    return value != NULL &&
           (strcmp(value, "1") == 0 ||
            strcmp(value, "true") == 0 ||
            strcmp(value, "yes") == 0 ||
            strcmp(value, "on") == 0);
}

static mock_health_result_t parse_result(const char *value)
{
    if (value == NULL) {
        return MOCK_HEALTH_INVALID;
    }

    if (strcmp(value, "internet_ready") == 0 ||
        strcmp(value, "success") == 0) {
        return MOCK_HEALTH_INTERNET_READY;
    }

    if (strcmp(value, "internet_down") == 0) {
        return MOCK_HEALTH_INTERNET_DOWN;
    }

    if (strcmp(value, "gateway_down") == 0) {
        return MOCK_HEALTH_GATEWAY_DOWN;
    }

    if (strcmp(value, "dns_down") == 0) {
        return MOCK_HEALTH_DNS_DOWN;
    }

    if (strcmp(value, "pending") == 0) {
        return MOCK_HEALTH_PENDING;
    }

    return MOCK_HEALTH_INVALID;
}

static const char *result_name(mock_health_result_t result)
{
    switch (result) {
    case MOCK_HEALTH_INTERNET_READY:
        return "internet_ready";
    case MOCK_HEALTH_INTERNET_DOWN:
        return "internet_down";
    case MOCK_HEALTH_GATEWAY_DOWN:
        return "gateway_down";
    case MOCK_HEALTH_DNS_DOWN:
        return "dns_down";
    case MOCK_HEALTH_PENDING:
        return "pending";
    case MOCK_HEALTH_INVALID:
    default:
        return "invalid";
    }
}

static void config_defaults(mock_config_t *config)
{
    memset(config, 0, sizeof(*config));

    config->enabled = true;
    config->auto_tick = true;
    config->now_ms = 0U;
    config->result = MOCK_HEALTH_INTERNET_READY;
    config->latency_ms = 35U;
}

static void load_config(const char *path, mock_config_t *config)
{
    FILE *fp;
    char line[256];

    config_defaults(config);

    fp = fopen(path, "r");
    if (fp == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq;
        char *key;
        char *value;

        trim(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }

        *eq = '\0';
        key = line;
        value = eq + 1;

        trim(key);
        trim(value);

        if (strcmp(key, "network_health_enabled") == 0) {
            config->enabled = parse_bool(value);
        }
        else if (strcmp(key, "network_health_auto_tick") == 0) {
            config->auto_tick = parse_bool(value);
        }
        else if (strcmp(key, "network_health_now_ms") == 0) {
            config->now_ms = (uint32_t)strtoul(value, NULL, 10);
        }
        else if (strcmp(key, "network_health_result") == 0) {
            config->result = parse_result(value);
        }
        else if (strcmp(key, "network_health_latency_ms") == 0) {
            config->latency_ms = (uint32_t)strtoul(value, NULL, 10);
        }
    }

    fclose(fp);
}

static uint32_t resolve_now(const mock_config_t *config)
{
    if (!config->auto_tick) {
        return config->now_ms;
    }

    if (!s_clock_initialized) {
        s_auto_now_ms = 1000U;
        s_clock_initialized = true;
    }
    else {
        s_auto_now_ms += 1000U;
    }

    return s_auto_now_ms;
}

static bool runtime_network_ready(void)
{
    echoear_runtime_network_t *network =
        echoear_runtime_network_get();

    if (network == NULL) {
        return false;
    }

    return network->state == ECHOEAR_RUNTIME_NETWORK_READY &&
           network->associated &&
           network->ip_ready;
}

static void print_state(uint32_t now_ms)
{
    echoear_network_health_t *health =
        echoear_network_health_get();

    if (health == NULL) {
        return;
    }

    printf(
        "[NetworkHealth] state=%s error=%s network_ready=%d "
        "gateway=%d dns=%d internet=%d "
        "failures=%u successes=%u checks=%lu "
        "latency=%lu last=%lu next=%lu now=%lu "
        "request=%d generation=%lu\n",
        echoear_network_health_state_name(health->state),
        echoear_network_health_error_name(health->error),
        health->network_ready ? 1 : 0,
        health->gateway_reachable ? 1 : 0,
        health->dns_working ? 1 : 0,
        health->internet_reachable ? 1 : 0,
        (unsigned int)health->consecutive_failures,
        (unsigned int)health->consecutive_successes,
        (unsigned long)health->check_count,
        (unsigned long)health->last_latency_ms,
        (unsigned long)health->last_check_ms,
        (unsigned long)health->next_check_ms,
        (unsigned long)now_ms,
        health->check_requested ? 1 : 0,
        (unsigned long)health->generation);
}

static void report_result(
    mock_health_result_t result,
    uint32_t latency_ms,
    uint32_t now_ms)
{
    echoear_network_health_probe_t probe;

    memset(&probe, 0, sizeof(probe));
    probe.latency_ms = latency_ms;

    switch (result) {
    case MOCK_HEALTH_INTERNET_READY:
        probe.gateway_reachable = true;
        probe.dns_working = true;
        probe.internet_reachable = true;
        break;

    case MOCK_HEALTH_INTERNET_DOWN:
        probe.gateway_reachable = true;
        probe.dns_working = true;
        probe.internet_reachable = false;
        break;

    case MOCK_HEALTH_GATEWAY_DOWN:
        probe.gateway_reachable = false;
        probe.dns_working = false;
        probe.internet_reachable = false;
        break;

    case MOCK_HEALTH_DNS_DOWN:
        probe.gateway_reachable = true;
        probe.dns_working = false;
        probe.internet_reachable = false;
        break;

    case MOCK_HEALTH_PENDING:
    case MOCK_HEALTH_INVALID:
    default:
        return;
    }

    printf(
        "[NetworkHealthProbe] result=%s gateway=%d dns=%d "
        "internet=%d latency=%lu\n",
        result_name(result),
        probe.gateway_reachable ? 1 : 0,
        probe.dns_working ? 1 : 0,
        probe.internet_reachable ? 1 : 0,
        (unsigned long)probe.latency_ms);

    echoear_network_health_report_probe(
        &probe,
        now_ms);
}

void echoear_network_health_mock_load(const char *path)
{
    mock_config_t config;
    bool ready;
    uint32_t now_ms;

    load_config(path, &config);

    if (!config.enabled) {
        return;
    }

    now_ms = resolve_now(&config);
    ready = runtime_network_ready();

    if (!s_network_state_initialized ||
        ready != s_last_network_ready) {
        s_network_state_initialized = true;
        s_last_network_ready = ready;

        echoear_network_health_set_network_ready(
            ready,
            now_ms);

        print_state(now_ms);
    }

    echoear_network_health_tick(now_ms);

    if (!echoear_network_health_take_check_request()) {
        return;
    }

    print_state(now_ms);

    if (config.result == MOCK_HEALTH_PENDING) {
        printf(
            "[NetworkHealthProbe] result=pending "
            "gateway=<pending> dns=<pending> internet=<pending>\n");
        return;
    }

    if (config.result == MOCK_HEALTH_INVALID) {
        printf(
            "[NetworkHealthProbe] result=invalid "
            "gateway=0 dns=0 internet=0 latency=0\n");

        echoear_network_health_report_probe(
            NULL,
            now_ms);

        print_state(now_ms);
        return;
    }

    report_result(
        config.result,
        config.latency_ms,
        now_ms);

    print_state(now_ms);
}
