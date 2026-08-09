#include "echoear_offline_mode_mock.h"

#include "echoear_network_health.h"
#include "echoear_offline_mode.h"
#include "echoear_runtime_network.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    MOCK_RECOVERY_SUCCESS = 0,
    MOCK_RECOVERY_FAILED,
    MOCK_RECOVERY_PENDING,
    MOCK_RECOVERY_INVALID
} mock_recovery_result_t;

typedef struct {
    bool enabled;
    bool auto_tick;
    uint32_t now_ms;
    mock_recovery_result_t recovery_result;
} mock_config_t;

static bool s_clock_initialized = false;
static uint32_t s_auto_now_ms = 0U;

static bool s_snapshot_initialized = false;
static echoear_connectivity_snapshot_t s_last_snapshot;

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

static mock_recovery_result_t parse_recovery_result(
    const char *value)
{
    if (value == NULL) {
        return MOCK_RECOVERY_INVALID;
    }

    if (strcmp(value, "success") == 0) {
        return MOCK_RECOVERY_SUCCESS;
    }

    if (strcmp(value, "failed") == 0) {
        return MOCK_RECOVERY_FAILED;
    }

    if (strcmp(value, "pending") == 0) {
        return MOCK_RECOVERY_PENDING;
    }

    return MOCK_RECOVERY_INVALID;
}

static const char *recovery_result_name(
    mock_recovery_result_t result)
{
    switch (result) {
    case MOCK_RECOVERY_SUCCESS:
        return "success";
    case MOCK_RECOVERY_FAILED:
        return "failed";
    case MOCK_RECOVERY_PENDING:
        return "pending";
    case MOCK_RECOVERY_INVALID:
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
    config->recovery_result = MOCK_RECOVERY_SUCCESS;
}

static void load_config(
    const char *path,
    mock_config_t *config)
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

        if (strcmp(key, "offline_mode_enabled") == 0) {
            config->enabled = parse_bool(value);
        }
        else if (strcmp(key, "offline_mode_auto_tick") == 0) {
            config->auto_tick = parse_bool(value);
        }
        else if (strcmp(key, "offline_mode_now_ms") == 0) {
            config->now_ms =
                (uint32_t)strtoul(value, NULL, 10);
        }
        else if (strcmp(key, "offline_recovery_result") == 0) {
            config->recovery_result =
                parse_recovery_result(value);
        }
    }

    fclose(fp);
}

static uint32_t resolve_now(
    const mock_config_t *config)
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

static bool snapshot_equal(
    const echoear_connectivity_snapshot_t *a,
    const echoear_connectivity_snapshot_t *b)
{
    return a->network_ready == b->network_ready &&
           a->local_available == b->local_available &&
           a->internet_ready == b->internet_ready;
}

static echoear_connectivity_snapshot_t
build_snapshot(void)
{
    echoear_connectivity_snapshot_t snapshot;
    echoear_runtime_network_t *network;
    echoear_network_health_t *health;

    memset(&snapshot, 0, sizeof(snapshot));

    network = echoear_runtime_network_get();
    health = echoear_network_health_get();

    if (network != NULL) {
        snapshot.network_ready =
            network->state ==
                ECHOEAR_RUNTIME_NETWORK_READY &&
            network->associated &&
            network->ip_ready;
    }

    if (health != NULL &&
        snapshot.network_ready &&
        health->network_ready) {
        snapshot.local_available =
            health->gateway_reachable;

        snapshot.internet_ready =
            health->state ==
                ECHOEAR_NETWORK_HEALTH_INTERNET_READY &&
            health->internet_reachable;
    }

    return snapshot;
}

static void print_state(uint32_t now_ms)
{
    echoear_offline_mode_t *mode =
        echoear_offline_mode_get();

    if (mode == NULL) {
        return;
    }

    printf(
        "[OfflineMode] state=%s reason=%s error=%s "
        "network=%d local=%d internet=%d "
        "recovery_request=%d recovery_active=%d "
        "attempt=%u/%u delay=%lu due=%lu now=%lu "
        "transitions=%lu recoveries=%lu generation=%lu\n",
        echoear_offline_mode_state_name(mode->state),
        echoear_offline_mode_reason_name(mode->reason),
        echoear_offline_mode_error_name(mode->error),
        mode->network_ready ? 1 : 0,
        mode->local_available ? 1 : 0,
        mode->internet_ready ? 1 : 0,
        mode->recovery_requested ? 1 : 0,
        mode->recovery_in_progress ? 1 : 0,
        (unsigned int)mode->recovery_attempt,
        (unsigned int)mode->policy.recovery_attempts,
        (unsigned long)mode->recovery_retry_delay_ms,
        (unsigned long)mode->recovery_retry_due_ms,
        (unsigned long)now_ms,
        (unsigned long)mode->transition_count,
        (unsigned long)mode->recovery_count,
        (unsigned long)mode->generation);
}

static void handle_recovery_request(
    mock_recovery_result_t result,
    uint32_t now_ms)
{
    echoear_offline_mode_t *mode =
        echoear_offline_mode_get();

    if (mode == NULL) {
        return;
    }

    if (!echoear_offline_mode_take_recovery_request()) {
        return;
    }

    printf(
        "[ServiceRecoveryAction] attempt=%u result=%s\n",
        (unsigned int)mode->recovery_attempt,
        recovery_result_name(result));

    print_state(now_ms);

    switch (result) {
    case MOCK_RECOVERY_SUCCESS:
        echoear_offline_mode_mark_recovery_completed();

        printf(
            "[ServiceRecoveryComplete] result=success\n");

        print_state(now_ms);
        break;

    case MOCK_RECOVERY_FAILED:
        echoear_offline_mode_mark_recovery_failed(
            now_ms);

        printf(
            "[ServiceRecoveryComplete] result=failed\n");

        print_state(now_ms);
        break;

    case MOCK_RECOVERY_PENDING:
        /*
         * Keep recovery_in_progress=1. A later simulator integration can
         * expose an explicit service completion event. Pending is useful
         * only for observing the in-progress state.
         */
        break;

    case MOCK_RECOVERY_INVALID:
    default:
        echoear_offline_mode_mark_recovery_failed(
            now_ms);

        printf(
            "[ServiceRecoveryComplete] result=invalid\n");

        print_state(now_ms);
        break;
    }
}

void echoear_offline_mode_mock_load(
    const char *path)
{
    mock_config_t config;
    echoear_connectivity_snapshot_t snapshot;
    uint32_t now_ms;

    load_config(path, &config);

    if (!config.enabled) {
        return;
    }

    now_ms = resolve_now(&config);
    snapshot = build_snapshot();

    if (!s_snapshot_initialized ||
        !snapshot_equal(
            &snapshot,
            &s_last_snapshot)) {
        s_snapshot_initialized = true;
        s_last_snapshot = snapshot;

        echoear_offline_mode_update(
            &snapshot,
            now_ms);

        print_state(now_ms);
    }

    echoear_offline_mode_tick(now_ms);

    handle_recovery_request(
        config.recovery_result,
        now_ms);
}
