#include "echoear_reset_manager_mock.h"

#include "echoear_credentials_store.h"
#include "echoear_first_boot.h"
#include "echoear_pro_ui.h"
#include "echoear_provisioning.h"
#include "echoear_reset_manager.h"
#include "echoear_wifi_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define RESET_LINE_MAX 256

typedef struct
{
    bool setup_completed;
    bool wifi_credentials_saved;
    bool force_provisioning;

    char saved_ssid[ECHOEAR_CREDENTIALS_SSID_MAX];
    echoear_wifi_security_t saved_security;
} device_storage_snapshot_t;

typedef struct
{
    echoear_reset_action_t action;
    echoear_reset_source_t source;
    echoear_reset_state_t state;
    echoear_reset_error_t error;

    bool request;
    bool confirm;
    bool cancel;
    bool auto_execute;

    char execute_result[40];
} reset_mock_input_t;

static bool previous_request;
static bool previous_confirm;
static bool previous_cancel;
static bool edge_state_initialized;

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
    device_storage_snapshot_t *storage)
{
    memset(storage, 0, sizeof(*storage));

    storage->saved_security =
        ECHOEAR_WIFI_SECURITY_UNKNOWN;
}

static bool read_device_storage(
    const char *path,
    device_storage_snapshot_t *storage)
{
    FILE *file;
    char line[RESET_LINE_MAX];

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

static bool write_device_storage(
    const char *path,
    const device_storage_snapshot_t *storage)
{
    FILE *file;
    int result;

    if (path == NULL || storage == NULL)
    {
        return false;
    }

    /*
     * Binary mode plus ASCII output guarantees:
     * - no UTF-8 BOM
     * - deterministic CRLF line endings on Windows
     * - no password or secret is stored
     */
    file = fopen(path, "wb");

    if (file == NULL)
    {
        return false;
    }

    result = fprintf(
        file,
        "setup_completed=%d\r\n"
        "wifi_credentials_saved=%d\r\n"
        "force_provisioning=%d\r\n"
        "saved_ssid=%s\r\n"
        "saved_security=%s\r\n",
        storage->setup_completed ? 1 : 0,
        storage->wifi_credentials_saved ? 1 : 0,
        storage->force_provisioning ? 1 : 0,
        storage->saved_ssid,
        security_name(storage->saved_security));

    if (result < 0 || fclose(file) != 0)
    {
        return false;
    }

    return true;
}

static void reset_input_defaults(
    reset_mock_input_t *input)
{
    memset(input, 0, sizeof(*input));

    input->action = ECHOEAR_RESET_ACTION_NONE;
    input->source = ECHOEAR_RESET_SOURCE_SETTINGS;
    input->state = ECHOEAR_RESET_IDLE;
    input->error = ECHOEAR_RESET_ERROR_NONE;
    input->auto_execute = true;

    copy_text(
        input->execute_result,
        sizeof(input->execute_result),
        "success");
}

static bool read_reset_input(
    const char *path,
    reset_mock_input_t *input)
{
    FILE *file;
    char line[RESET_LINE_MAX];

    if (input == NULL)
    {
        return false;
    }

    reset_input_defaults(input);

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

        if (strcmp(key, "reset_action") == 0)
        {
            echoear_reset_manager_parse_action(
                value,
                &input->action);
        }
        else if (strcmp(key, "reset_source") == 0)
        {
            echoear_reset_manager_parse_source(
                value,
                &input->source);
        }
        else if (strcmp(key, "reset_state") == 0)
        {
            echoear_reset_manager_parse_state(
                value,
                &input->state);
        }
        else if (strcmp(key, "reset_error") == 0)
        {
            echoear_reset_manager_parse_error(
                value,
                &input->error);
        }
        else if (strcmp(key, "reset_request") == 0)
        {
            input->request = parse_bool(value);
        }
        else if (strcmp(key, "reset_confirm") == 0)
        {
            input->confirm = parse_bool(value);
        }
        else if (strcmp(key, "reset_cancel") == 0)
        {
            input->cancel = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "reset_auto_execute") == 0)
        {
            input->auto_execute =
                parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "reset_execute_result") == 0)
        {
            copy_text(
                input->execute_result,
                sizeof(input->execute_result),
                value);
        }
    }

    fclose(file);

    return true;
}

static void print_reset_state(void)
{
    echoear_reset_manager_t *state =
        echoear_reset_manager_get();

    char current_log[512];

    if (state == NULL)
    {
        return;
    }

    snprintf(
        current_log,
        sizeof(current_log),
        "[Reset] action=%s source=%s state=%s "
        "error=%s confirm_required=%d confirmed=%d "
        "execute=%d erase_wifi=%d erase_settings=%d "
        "provisioning=%d restart=%d generation=%lu",
        echoear_reset_manager_action_name(
            state->action),
        echoear_reset_manager_source_name(
            state->source),
        echoear_reset_manager_state_name(
            state->state),
        echoear_reset_manager_error_name(
            state->error),
        state->confirmation_required ? 1 : 0,
        state->confirmed ? 1 : 0,
        state->execute_requested ? 1 : 0,
        state->erase_wifi_credentials ? 1 : 0,
        state->erase_device_settings ? 1 : 0,
        state->enter_provisioning ? 1 : 0,
        state->restart_required ? 1 : 0,
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

static void apply_first_boot_state(
    const device_storage_snapshot_t *storage)
{
    echoear_first_boot_set_setup_completed(
        storage->setup_completed);

    echoear_first_boot_set_wifi_credentials_saved(
        storage->wifi_credentials_saved);

    echoear_first_boot_set_force_provisioning(
        storage->force_provisioning);

    echoear_first_boot_apply();
}

static void apply_provisioning_ui(void)
{
    echoear_provisioning_reset();

    echoear_provisioning_set_setup_completed(false);

    echoear_provisioning_set_state(
        ECHOEAR_PROVISIONING_CHECKING);

    echoear_pro_ui_apply_provisioning_state();
}

static void apply_reset_error(
    echoear_reset_error_t error)
{
    echoear_reset_manager_set_error(error);

    echoear_provisioning_set_error(
        ECHOEAR_PROVISIONING_ERROR_SAVE_FAILED);

    echoear_pro_ui_apply_provisioning_state();

    print_reset_state();
}

static bool result_is(
    const reset_mock_input_t *input,
    const char *expected)
{
    return strcmp(
               input->execute_result,
               expected) == 0;
}

static void execute_reset(
    const reset_mock_input_t *input,
    const char *device_storage_path)
{
    echoear_reset_manager_t *manager =
        echoear_reset_manager_get();

    device_storage_snapshot_t storage;

    if (manager == NULL)
    {
        return;
    }

    read_device_storage(
        device_storage_path,
        &storage);

    echoear_reset_manager_mark_running();
    print_reset_state();

    if (manager->erase_wifi_credentials)
    {
        echoear_credentials_store_request_delete();
        echoear_credentials_store_mark_deleting();

        if (result_is(input, "delete_failed"))
        {
            apply_reset_error(
                ECHOEAR_RESET_ERROR_DELETE_CREDENTIALS_FAILED);
            return;
        }

        echoear_credentials_store_mark_deleted();
        echoear_reset_manager_mark_credentials_erased();

        storage.wifi_credentials_saved = false;
        storage.saved_ssid[0] = '\0';
        storage.saved_security =
            ECHOEAR_WIFI_SECURITY_UNKNOWN;
    }

    if (manager->erase_device_settings)
    {
        if (result_is(input, "settings_failed"))
        {
            apply_reset_error(
                ECHOEAR_RESET_ERROR_RESET_SETTINGS_FAILED);
            return;
        }

        echoear_reset_manager_mark_settings_erased();
    }

    switch (manager->action)
    {
    case ECHOEAR_RESET_ACTION_FORGET_WIFI:
        storage.setup_completed = false;
        storage.wifi_credentials_saved = false;
        storage.force_provisioning = false;
        break;

    case ECHOEAR_RESET_ACTION_REPROVISION:
        /*
         * The current Wi-Fi metadata remains available until
         * replacement credentials are saved successfully.
         */
        storage.setup_completed = true;
        storage.force_provisioning = true;
        break;

    case ECHOEAR_RESET_ACTION_FACTORY_RESET:
        storage.setup_completed = false;
        storage.wifi_credentials_saved = false;
        storage.force_provisioning = false;
        storage.saved_ssid[0] = '\0';
        storage.saved_security =
            ECHOEAR_WIFI_SECURITY_UNKNOWN;
        break;

    case ECHOEAR_RESET_ACTION_NONE:
    default:
        apply_reset_error(
            ECHOEAR_RESET_ERROR_INVALID_ACTION);
        return;
    }

    if (result_is(input, "storage_failed") ||
        !write_device_storage(
            device_storage_path,
            &storage))
    {
        apply_reset_error(
            ECHOEAR_RESET_ERROR_STORAGE_WRITE_FAILED);
        return;
    }

    apply_first_boot_state(&storage);

    if (result_is(input, "apply_failed"))
    {
        apply_reset_error(
            ECHOEAR_RESET_ERROR_APPLY_FAILED);
        return;
    }

    apply_provisioning_ui();

    echoear_reset_manager_mark_provisioning_applied();
    echoear_reset_manager_mark_completed();

    print_reset_state();

    printf(
        "[ResetComplete] action=%s "
        "setup=%d credentials=%d force=%d "
        "ssid=%s restart=%d\n",
        echoear_reset_manager_action_name(
            manager->action),
        storage.setup_completed ? 1 : 0,
        storage.wifi_credentials_saved ? 1 : 0,
        storage.force_provisioning ? 1 : 0,
        storage.saved_ssid[0] != '\0'
            ? storage.saved_ssid
            : "<none>",
        manager->restart_required ? 1 : 0);
}

bool echoear_reset_manager_mock_load(
    const char *reset_path,
    const char *device_storage_path)
{
    reset_mock_input_t input;

    bool request_edge;
    bool confirm_edge;
    bool cancel_edge;

    if (!read_reset_input(reset_path, &input))
    {
        print_reset_state();
        return false;
    }

    if (!edge_state_initialized)
    {
        previous_request = false;
        previous_confirm = false;
        previous_cancel = false;
        edge_state_initialized = true;
    }

    request_edge =
        input.request && !previous_request;

    confirm_edge =
        input.confirm && !previous_confirm;

    cancel_edge =
        input.cancel && !previous_cancel;

    previous_request = input.request;
    previous_confirm = input.confirm;
    previous_cancel = input.cancel;

    if (input.error != ECHOEAR_RESET_ERROR_NONE)
    {
        echoear_reset_manager_set_error(
            input.error);

        print_reset_state();
        return true;
    }

    if (request_edge)
    {
        echoear_reset_manager_request(
            input.action,
            input.source);

        print_reset_state();
    }

    if (cancel_edge)
    {
        echoear_reset_manager_cancel();
        print_reset_state();
    }

    if (confirm_edge)
    {
        echoear_reset_manager_confirm();
        print_reset_state();
    }

    if (input.auto_execute &&
        echoear_reset_manager_take_execute_request())
    {
        execute_reset(
            &input,
            device_storage_path);
    }

    print_reset_state();

    return true;
}
