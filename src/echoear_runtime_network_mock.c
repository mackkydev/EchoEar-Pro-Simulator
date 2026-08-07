#include "echoear_runtime_network_mock.h"

#include "echoear_runtime_network.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNTIME_LINE_MAX 256

typedef struct
{
    bool setup_completed;
    bool wifi_credentials_saved;
    bool force_provisioning;

    char saved_ssid[ECHOEAR_WIFI_SSID_MAX];
    echoear_wifi_security_t saved_security;
} runtime_storage_t;

typedef struct
{
    bool enabled;
    bool auto_boot;
    bool boot_trigger;

    bool auto_tick;
    uint32_t configured_now_ms;

    char connect_result[32];

    char ip[ECHOEAR_RUNTIME_NETWORK_IPV4_MAX];
    char gateway[ECHOEAR_RUNTIME_NETWORK_IPV4_MAX];
    char netmask[ECHOEAR_RUNTIME_NETWORK_IPV4_MAX];

    int16_t rssi;
} runtime_mock_input_t;

static bool boot_started;
static bool previous_boot_trigger;
static bool trigger_initialized;
static uint32_t automatic_now_ms;
static char previous_log[640];

static char *trim_whitespace(char *value)
{
    char *end;

    if (value == NULL)
    {
        return NULL;
    }

    while (*value != '\0' &&
           isspace((unsigned char)*value))
    {
        value++;
    }

    if (*value == '\0')
    {
        return value;
    }

    end = value + strlen(value) - 1;

    while (end > value &&
           isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }

    return value;
}

static void strip_utf8_bom(char *value)
{
    unsigned char *bytes =
        (unsigned char *)value;

    if (value == NULL)
    {
        return;
    }

    if (bytes[0] == 0xEFU &&
        bytes[1] == 0xBBU &&
        bytes[2] == 0xBFU)
    {
        memmove(
            value,
            value + 3,
            strlen(value + 3) + 1U);
    }
}

static bool parse_bool(const char *value)
{
    if (value == NULL)
    {
        return false;
    }

    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "on") == 0;
}

static void copy_text(
    char *destination,
    size_t destination_size,
    const char *source)
{
    if (destination == NULL ||
        destination_size == 0U)
    {
        return;
    }

    if (source == NULL)
    {
        destination[0] = '\0';
        return;
    }

    strncpy(
        destination,
        source,
        destination_size - 1U);

    destination[destination_size - 1U] = '\0';
}

static bool parse_security(
    const char *value,
    echoear_wifi_security_t *security)
{
    if (value == NULL || security == NULL)
    {
        return false;
    }

    if (strcmp(value, "open") == 0)
        *security = ECHOEAR_WIFI_SECURITY_OPEN;
    else if (strcmp(value, "wpa2_psk") == 0)
        *security = ECHOEAR_WIFI_SECURITY_WPA2_PSK;
    else if (strcmp(value, "wpa3_sae") == 0)
        *security = ECHOEAR_WIFI_SECURITY_WPA3_SAE;
    else if (strcmp(value, "wpa2_wpa3") == 0)
        *security = ECHOEAR_WIFI_SECURITY_WPA2_WPA3;
    else if (strcmp(value, "unknown") == 0 ||
             value[0] == '\0')
        *security = ECHOEAR_WIFI_SECURITY_UNKNOWN;
    else
        return false;

    return true;
}

static const char *security_name(
    echoear_wifi_security_t security)
{
    switch (security)
    {
    case ECHOEAR_WIFI_SECURITY_OPEN:
        return "open";
    case ECHOEAR_WIFI_SECURITY_WPA2_PSK:
        return "wpa2_psk";
    case ECHOEAR_WIFI_SECURITY_WPA3_SAE:
        return "wpa3_sae";
    case ECHOEAR_WIFI_SECURITY_WPA2_WPA3:
        return "wpa2_wpa3";
    case ECHOEAR_WIFI_SECURITY_UNKNOWN:
    default:
        return "unknown";
    }
}

static void storage_defaults(
    runtime_storage_t *storage)
{
    memset(storage, 0, sizeof(*storage));

    storage->saved_security =
        ECHOEAR_WIFI_SECURITY_UNKNOWN;
}

static bool read_storage(
    const char *path,
    runtime_storage_t *storage)
{
    FILE *file;
    char line[RUNTIME_LINE_MAX];

    if (storage == NULL)
    {
        return false;
    }

    storage_defaults(storage);

    if (path == NULL)
    {
        return false;
    }

    file = fopen(path, "rb");

    if (file == NULL)
    {
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *separator;
        char *key;
        char *value;

        strip_utf8_bom(line);

        key = trim_whitespace(line);

        if (key == NULL ||
            key[0] == '\0' ||
            key[0] == '#')
        {
            continue;
        }

        separator = strchr(key, '=');

        if (separator == NULL)
        {
            continue;
        }

        *separator = '\0';

        value = trim_whitespace(separator + 1);
        key = trim_whitespace(key);

        if (strcmp(key, "setup_completed") == 0)
        {
            storage->setup_completed =
                parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "wifi_credentials_saved") == 0)
        {
            storage->wifi_credentials_saved =
                parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "force_provisioning") == 0)
        {
            storage->force_provisioning =
                parse_bool(value);
        }
        else if (strcmp(key, "saved_ssid") == 0)
        {
            copy_text(
                storage->saved_ssid,
                sizeof(storage->saved_ssid),
                value);
        }
        else if (strcmp(
                     key,
                     "saved_security") == 0)
        {
            parse_security(
                value,
                &storage->saved_security);
        }
    }

    fclose(file);

    return true;
}

static void input_defaults(
    runtime_mock_input_t *input)
{
    memset(input, 0, sizeof(*input));

    input->enabled = true;
    input->auto_boot = true;
    input->auto_tick = true;

    copy_text(
        input->connect_result,
        sizeof(input->connect_result),
        "success");

    copy_text(
        input->ip,
        sizeof(input->ip),
        "192.168.1.88");

    copy_text(
        input->gateway,
        sizeof(input->gateway),
        "192.168.1.1");

    copy_text(
        input->netmask,
        sizeof(input->netmask),
        "255.255.255.0");

    input->rssi = -45;
}

static bool read_input(
    const char *path,
    runtime_mock_input_t *input)
{
    FILE *file;
    char line[RUNTIME_LINE_MAX];

    if (input == NULL)
    {
        return false;
    }

    input_defaults(input);

    if (path == NULL)
    {
        return false;
    }

    file = fopen(path, "rb");

    if (file == NULL)
    {
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *separator;
        char *key;
        char *value;

        strip_utf8_bom(line);

        key = trim_whitespace(line);

        if (key == NULL ||
            key[0] == '\0' ||
            key[0] == '#')
        {
            continue;
        }

        separator = strchr(key, '=');

        if (separator == NULL)
        {
            continue;
        }

        *separator = '\0';

        value = trim_whitespace(separator + 1);
        key = trim_whitespace(key);

        if (strcmp(
                key,
                "runtime_network_enabled") == 0)
        {
            input->enabled = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "runtime_auto_boot") == 0)
        {
            input->auto_boot = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "runtime_boot_trigger") == 0)
        {
            input->boot_trigger = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "runtime_auto_tick") == 0)
        {
            input->auto_tick = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "runtime_now_ms") == 0)
        {
            input->configured_now_ms =
                (uint32_t)strtoul(
                    value,
                    NULL,
                    10);
        }
        else if (strcmp(
                     key,
                     "runtime_connect_result") == 0)
        {
            copy_text(
                input->connect_result,
                sizeof(input->connect_result),
                value);
        }
        else if (strcmp(
                     key,
                     "runtime_ip") == 0)
        {
            copy_text(
                input->ip,
                sizeof(input->ip),
                value);
        }
        else if (strcmp(
                     key,
                     "runtime_gateway") == 0)
        {
            copy_text(
                input->gateway,
                sizeof(input->gateway),
                value);
        }
        else if (strcmp(
                     key,
                     "runtime_netmask") == 0)
        {
            copy_text(
                input->netmask,
                sizeof(input->netmask),
                value);
        }
        else if (strcmp(
                     key,
                     "runtime_rssi") == 0)
        {
            input->rssi =
                (int16_t)atoi(value);
        }
    }

    fclose(file);

    return true;
}

static uint32_t resolve_now_ms(
    const runtime_mock_input_t *input)
{
    if (!input->auto_tick)
    {
        automatic_now_ms =
            input->configured_now_ms;

        return automatic_now_ms;
    }

    if (automatic_now_ms <
        input->configured_now_ms)
    {
        automatic_now_ms =
            input->configured_now_ms;
    }

    /*
     * main.c reloads mocks about once per second.
     */
    automatic_now_ms += 1000U;

    return automatic_now_ms;
}

static void print_state(uint32_t now_ms)
{
    echoear_runtime_network_t *state =
        echoear_runtime_network_get();

    char current_log[640];

    if (state == NULL)
    {
        return;
    }

    snprintf(
        current_log,
        sizeof(current_log),
        "[RuntimeNet] state=%s reason=%s error=%s "
        "setup=%d credentials=%d provisioning=%d "
        "ssid=%s security=%s associated=%d ip_ready=%d "
        "ip=%s gateway=%s rssi=%d attempt=%u/%u "
        "retry_delay=%lu retry_due=%lu now=%lu "
        "connect_request=%d generation=%lu",
        echoear_runtime_network_state_name(
            state->state),
        echoear_runtime_network_reason_name(
            state->reason),
        echoear_runtime_network_error_name(
            state->error),
        state->setup_completed ? 1 : 0,
        state->credentials_available ? 1 : 0,
        state->provisioning_required ? 1 : 0,
        state->ssid[0] != '\0'
            ? state->ssid
            : "<none>",
        security_name(state->security),
        state->associated ? 1 : 0,
        state->ip_ready ? 1 : 0,
        state->ip[0] != '\0'
            ? state->ip
            : "<none>",
        state->gateway[0] != '\0'
            ? state->gateway
            : "<none>",
        (int)state->rssi,
        (unsigned int)state->attempt,
        (unsigned int)state->policy.max_boot_attempts,
        (unsigned long)state->current_retry_delay_ms,
        (unsigned long)state->retry_due_ms,
        (unsigned long)now_ms,
        state->connect_requested ? 1 : 0,
        (unsigned long)state->generation);

    if (strcmp(current_log, previous_log) != 0)
    {
        printf("%s\n", current_log);

        copy_text(
            previous_log,
            sizeof(previous_log),
            current_log);
    }
}

static bool result_is(
    const runtime_mock_input_t *input,
    const char *expected)
{
    return strcmp(
               input->connect_result,
               expected) == 0;
}

static void consume_connect_request(
    const runtime_mock_input_t *input,
    uint32_t now_ms)
{
    echoear_runtime_network_t *state =
        echoear_runtime_network_get();

    if (state == NULL ||
        !echoear_runtime_network_take_connect_request())
    {
        return;
    }

    printf(
        "[RuntimeConnect] ssid=%s security=%s "
        "attempt=%u result=%s secret=<secure-store>\n",
        state->ssid[0] != '\0'
            ? state->ssid
            : "<none>",
        security_name(state->security),
        (unsigned int)state->attempt,
        input->connect_result);

    echoear_runtime_network_mark_connecting(
        now_ms);

    print_state(now_ms);

    if (result_is(input, "pending"))
    {
        return;
    }

    if (result_is(input, "success"))
    {
        echoear_runtime_network_mark_associated(
            input->rssi);

        print_state(now_ms);

        echoear_runtime_network_mark_ip_ready(
            input->ip,
            input->gateway,
            input->netmask,
            input->rssi);

        printf(
            "[RuntimeReady] ssid=%s ip=%s "
            "gateway=%s rssi=%d\n",
            state->ssid,
            state->ip,
            state->gateway,
            (int)state->rssi);

        return;
    }

    if (result_is(
            input,
            "network_not_found"))
    {
        echoear_runtime_network_report_connect_failure(
            ECHOEAR_RUNTIME_NETWORK_ERROR_NETWORK_NOT_FOUND,
            now_ms);

        return;
    }

    if (result_is(input, "auth_failed"))
    {
        echoear_runtime_network_report_connect_failure(
            ECHOEAR_RUNTIME_NETWORK_ERROR_AUTH_FAILED,
            now_ms);

        return;
    }

    if (result_is(input, "dhcp_failed"))
    {
        echoear_runtime_network_mark_associated(
            input->rssi);

        print_state(now_ms);

        echoear_runtime_network_report_connect_failure(
            ECHOEAR_RUNTIME_NETWORK_ERROR_DHCP_FAILED,
            now_ms);

        return;
    }

    if (result_is(input, "timeout"))
    {
        echoear_runtime_network_report_connect_failure(
            ECHOEAR_RUNTIME_NETWORK_ERROR_CONNECT_TIMEOUT,
            now_ms);

        return;
    }

    if (result_is(input, "driver"))
    {
        echoear_runtime_network_report_connect_failure(
            ECHOEAR_RUNTIME_NETWORK_ERROR_DRIVER,
            now_ms);

        return;
    }

    echoear_runtime_network_report_connect_failure(
        ECHOEAR_RUNTIME_NETWORK_ERROR_INTERNAL,
        now_ms);
}

static void begin_boot(
    const runtime_storage_t *storage,
    uint32_t now_ms)
{
    bool normal_setup;
    bool credentials_available;

    if (storage == NULL)
    {
        return;
    }

    /*
     * force_provisioning wins over saved credentials.
     * Runtime auto-connect is suppressed while a forced
     * provisioning flow is active.
     */
    normal_setup =
        storage->setup_completed &&
        !storage->force_provisioning;

    credentials_available =
        storage->wifi_credentials_saved &&
        !storage->force_provisioning;

    echoear_runtime_network_begin_boot(
        normal_setup,
        credentials_available,
        storage->saved_ssid,
        storage->saved_security,
        now_ms);

    boot_started = true;
}

bool echoear_runtime_network_mock_load(
    const char *runtime_path,
    const char *device_storage_path)
{
    runtime_mock_input_t input;
    runtime_storage_t storage;

    uint32_t now_ms;
    bool trigger_edge;

    if (!read_input(runtime_path, &input))
    {
        return false;
    }

    read_storage(
        device_storage_path,
        &storage);

    now_ms = resolve_now_ms(&input);

    if (!trigger_initialized)
    {
        previous_boot_trigger = false;
        trigger_initialized = true;
    }

    trigger_edge =
        input.boot_trigger &&
        !previous_boot_trigger;

    previous_boot_trigger =
        input.boot_trigger;

    if (!input.enabled)
    {
        print_state(now_ms);
        return true;
    }

    if ((input.auto_boot && !boot_started) ||
        trigger_edge)
    {
        /*
         * Manual trigger also allows a clean simulator reboot
         * of this manager without restarting the executable.
         */
        echoear_runtime_network_reset();
        begin_boot(&storage, now_ms);
    }

    echoear_runtime_network_tick(now_ms);

    print_state(now_ms);

    consume_connect_request(
        &input,
        now_ms);

    print_state(now_ms);

    return true;
}
