#include "echoear_provisioning_recovery_mock.h"

#include "echoear_pro_ui.h"
#include "echoear_provisioning.h"
#include "echoear_provisioning_recovery.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECOVERY_LINE_MAX 256

typedef struct
{
    bool enabled;
    echoear_recovery_failure_t failure;
    bool trigger;

    uint32_t configured_now_ms;
    bool auto_tick;

    char action_result[32];
} recovery_mock_input_t;

static bool previous_trigger;
static bool trigger_initialized;
static uint32_t automatic_now_ms;
static char previous_log[512];

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

static void input_defaults(
    recovery_mock_input_t *input)
{
    memset(input, 0, sizeof(*input));

    input->enabled = true;
    input->failure =
        ECHOEAR_RECOVERY_FAILURE_NONE;

    input->auto_tick = true;

    copy_text(
        input->action_result,
        sizeof(input->action_result),
        "success");
}

static bool read_input(
    const char *path,
    recovery_mock_input_t *input)
{
    FILE *file;
    char line[RECOVERY_LINE_MAX];

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
                "recovery_enabled") == 0)
        {
            input->enabled = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "recovery_failure") == 0)
        {
            echoear_provisioning_recovery_parse_failure(
                value,
                &input->failure);
        }
        else if (strcmp(
                     key,
                     "recovery_trigger") == 0)
        {
            input->trigger = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "recovery_now_ms") == 0)
        {
            input->configured_now_ms =
                (uint32_t)strtoul(
                    value,
                    NULL,
                    10);
        }
        else if (strcmp(
                     key,
                     "recovery_auto_tick") == 0)
        {
            input->auto_tick = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "recovery_action_result") == 0)
        {
            copy_text(
                input->action_result,
                sizeof(input->action_result),
                value);
        }
    }

    fclose(file);

    return true;
}

static uint32_t resolve_now_ms(
    const recovery_mock_input_t *input)
{
    if (input == NULL)
    {
        return automatic_now_ms;
    }

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
     * main.c loads simulator mocks approximately once per second.
     * Advancing 1000 ms per load keeps backoff tests deterministic
     * without relying on the host wall clock.
     */
    automatic_now_ms += 1000U;

    return automatic_now_ms;
}

static echoear_provisioning_error_t
provisioning_error_for_failure(
    echoear_recovery_failure_t failure)
{
    switch (failure)
    {
    case ECHOEAR_RECOVERY_FAILURE_SOFTAP_START:
        return ECHOEAR_PROVISIONING_ERROR_AP_START_FAILED;

    case ECHOEAR_RECOVERY_FAILURE_SCAN:
        return ECHOEAR_PROVISIONING_ERROR_SCAN_FAILED;

    case ECHOEAR_RECOVERY_FAILURE_NETWORK_NOT_FOUND:
        return ECHOEAR_PROVISIONING_ERROR_NETWORK_NOT_FOUND;

    case ECHOEAR_RECOVERY_FAILURE_AUTH:
        return ECHOEAR_PROVISIONING_ERROR_AUTH_FAILED;

    case ECHOEAR_RECOVERY_FAILURE_DHCP:
        return ECHOEAR_PROVISIONING_ERROR_DHCP_FAILED;

    case ECHOEAR_RECOVERY_FAILURE_STORAGE:
        return ECHOEAR_PROVISIONING_ERROR_SAVE_FAILED;

    case ECHOEAR_RECOVERY_FAILURE_WIFI_CONNECT_TIMEOUT:
    case ECHOEAR_RECOVERY_FAILURE_PORTAL_IDLE_TIMEOUT:
    case ECHOEAR_RECOVERY_FAILURE_PROVISIONING_TIMEOUT:
        return ECHOEAR_PROVISIONING_ERROR_TIMEOUT;

    case ECHOEAR_RECOVERY_FAILURE_INTERNAL:
    case ECHOEAR_RECOVERY_FAILURE_NONE:
    default:
        return ECHOEAR_PROVISIONING_ERROR_UNKNOWN;
    }
}

static void print_state(
    uint32_t now_ms)
{
    echoear_provisioning_recovery_t *state =
        echoear_provisioning_recovery_get();

    char current_log[512];

    if (state == NULL)
    {
        return;
    }

    snprintf(
        current_log,
        sizeof(current_log),
        "[Recovery] state=%s failure=%s action=%s "
        "attempt=%u/%u delay=%lu due=%lu now=%lu "
        "scheduled=%d requested=%d user=%d "
        "generation=%lu",
        echoear_provisioning_recovery_state_name(
            state->state),
        echoear_provisioning_recovery_failure_name(
            state->failure),
        echoear_provisioning_recovery_action_name(
            state->action),
        (unsigned int)state->attempt,
        (unsigned int)state->policy.max_attempts,
        (unsigned long)state->current_delay_ms,
        (unsigned long)state->retry_due_ms,
        (unsigned long)now_ms,
        state->retry_scheduled ? 1 : 0,
        state->action_requested ? 1 : 0,
        state->user_action_required ? 1 : 0,
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

static void apply_action_to_provisioning(
    echoear_recovery_action_t action,
    echoear_recovery_failure_t failure)
{
    switch (action)
    {
    case ECHOEAR_RECOVERY_ACTION_RESCAN:
        echoear_provisioning_set_error(
            ECHOEAR_PROVISIONING_ERROR_NONE);

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_SCANNING);
        break;

    case ECHOEAR_RECOVERY_ACTION_RECONNECT:
        echoear_provisioning_set_error(
            ECHOEAR_PROVISIONING_ERROR_NONE);

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_WIFI_CONNECTING);
        break;

    case ECHOEAR_RECOVERY_ACTION_RESTART_SOFTAP:
        echoear_provisioning_set_error(
            ECHOEAR_PROVISIONING_ERROR_NONE);

        echoear_provisioning_set_client_connected(false);

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_STARTING);
        break;

    case ECHOEAR_RECOVERY_ACTION_REOPEN_PORTAL:
        echoear_provisioning_set_error(
            ECHOEAR_PROVISIONING_ERROR_NONE);

        echoear_provisioning_set_client_connected(false);

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_READY);
        break;

    case ECHOEAR_RECOVERY_ACTION_FALLBACK_PROVISIONING:
        echoear_provisioning_set_error(
            ECHOEAR_PROVISIONING_ERROR_NONE);

        echoear_provisioning_set_setup_completed(false);

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_CHECKING);
        break;

    case ECHOEAR_RECOVERY_ACTION_RETRY:
        echoear_provisioning_set_error(
            ECHOEAR_PROVISIONING_ERROR_NONE);

        if (failure ==
            ECHOEAR_RECOVERY_FAILURE_STORAGE)
        {
            echoear_provisioning_set_state(
                ECHOEAR_PROVISIONING_SAVING);
        }
        break;

    case ECHOEAR_RECOVERY_ACTION_REQUIRE_USER:
        echoear_provisioning_set_error(
            provisioning_error_for_failure(
                failure));
        break;

    case ECHOEAR_RECOVERY_ACTION_FAIL_SAFE:
        echoear_provisioning_set_error(
            ECHOEAR_PROVISIONING_ERROR_UNKNOWN);
        break;

    case ECHOEAR_RECOVERY_ACTION_NONE:
    default:
        break;
    }

    echoear_pro_ui_apply_provisioning_state();
}

static bool action_result_is(
    const recovery_mock_input_t *input,
    const char *expected)
{
    if (input == NULL || expected == NULL)
    {
        return false;
    }

    return strcmp(
               input->action_result,
               expected) == 0;
}

static void execute_requested_action(
    const recovery_mock_input_t *input,
    uint32_t now_ms)
{
    echoear_provisioning_recovery_t *state =
        echoear_provisioning_recovery_get();

    echoear_recovery_action_t action;

    if (state == NULL)
    {
        return;
    }

    if (!echoear_provisioning_recovery_take_action(
            &action))
    {
        return;
    }

    printf(
        "[RecoveryAction] action=%s failure=%s "
        "attempt=%u result=%s\n",
        echoear_provisioning_recovery_action_name(
            action),
        echoear_provisioning_recovery_failure_name(
            state->failure),
        (unsigned int)state->attempt,
        input->action_result);

    apply_action_to_provisioning(
        action,
        state->failure);

    /*
     * "pending" leaves the recovery engine watching the
     * real subsystem. A later simulator trigger can report
     * a new failure or the test can be reset.
     */
    if (action_result_is(input, "pending"))
    {
        return;
    }

    /*
     * require_user is intentionally terminal from the
     * automatic recovery perspective. It must not be
     * converted into "recovered" automatically.
     */
    if (action ==
        ECHOEAR_RECOVERY_ACTION_REQUIRE_USER)
    {
        return;
    }

    if (action_result_is(input, "success"))
    {
        echoear_provisioning_recovery_mark_recovered();

        printf(
            "[RecoveryComplete] recovered=1 "
            "last_action=%s\n",
            echoear_provisioning_recovery_action_name(
                action));

        return;
    }

    if (action_result_is(input, "failed"))
    {
        echoear_provisioning_recovery_mark_action_failed(
            state->failure,
            now_ms);

        return;
    }

    echoear_provisioning_recovery_set_error();

    echoear_provisioning_set_error(
        ECHOEAR_PROVISIONING_ERROR_UNKNOWN);

    echoear_pro_ui_apply_provisioning_state();
}

bool echoear_provisioning_recovery_mock_load(
    const char *path)
{
    recovery_mock_input_t input;

    bool trigger_edge;
    uint32_t now_ms;

    if (!read_input(path, &input))
    {
        return false;
    }

    now_ms = resolve_now_ms(&input);

    if (!trigger_initialized)
    {
        previous_trigger = false;
        trigger_initialized = true;
    }

    trigger_edge =
        input.trigger && !previous_trigger;

    previous_trigger = input.trigger;

    if (!input.enabled)
    {
        print_state(now_ms);
        return true;
    }

    if (trigger_edge &&
        input.failure !=
            ECHOEAR_RECOVERY_FAILURE_NONE)
    {
        echoear_provisioning_set_error(
            provisioning_error_for_failure(
                input.failure));

        echoear_pro_ui_apply_provisioning_state();

        echoear_provisioning_recovery_report_failure(
            input.failure,
            now_ms);
    }

    echoear_provisioning_recovery_tick(now_ms);

    print_state(now_ms);

    execute_requested_action(
        &input,
        now_ms);

    print_state(now_ms);

    return true;
}
