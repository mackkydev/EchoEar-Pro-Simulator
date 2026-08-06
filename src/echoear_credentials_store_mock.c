#include "echoear_credentials_store_mock.h"

#include "echoear_captive_portal.h"
#include "echoear_credentials_store.h"
#include "echoear_first_boot.h"
#include "echoear_pro_ui.h"
#include "echoear_provisioning.h"
#include "echoear_wifi_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CREDENTIALS_MOCK_LINE_MAX 320
#define CREDENTIALS_MOCK_PATH_MAX 512

typedef enum
{
    MOCK_RESULT_SUCCESS = 0,
    MOCK_RESULT_WRITE_FAILED,
    MOCK_RESULT_VERIFY_FAILED,
    MOCK_RESULT_DELETE_FAILED,
    MOCK_RESULT_INTERNAL
} mock_result_t;

typedef struct
{
    bool exists;
    bool setup_completed;
    bool wifi_credentials_saved;
    bool force_provisioning;

    char saved_ssid[ECHOEAR_CREDENTIALS_SSID_MAX];
    echoear_wifi_security_t saved_security;
} mock_device_storage_t;

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

static char *skip_utf8_bom(char *value)
{
    const unsigned char *bytes;

    if (value == NULL || strlen(value) < 3U)
    {
        return value;
    }

    bytes = (const unsigned char *)value;

    if (bytes[0] == 0xEFU &&
        bytes[1] == 0xBBU &&
        bytes[2] == 0xBFU)
    {
        return value + 3;
    }

    return value;
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

static mock_result_t parse_result(
    const char *value)
{
    if (value == NULL ||
        strcmp(value, "success") == 0)
    {
        return MOCK_RESULT_SUCCESS;
    }

    if (strcmp(value, "write_failed") == 0)
    {
        return MOCK_RESULT_WRITE_FAILED;
    }

    if (strcmp(value, "verify_failed") == 0)
    {
        return MOCK_RESULT_VERIFY_FAILED;
    }

    if (strcmp(value, "delete_failed") == 0)
    {
        return MOCK_RESULT_DELETE_FAILED;
    }

    return MOCK_RESULT_INTERNAL;
}

static bool read_device_storage(
    const char *path,
    mock_device_storage_t *storage)
{
    FILE *file;
    char line[CREDENTIALS_MOCK_LINE_MAX];

    if (storage == NULL)
    {
        return false;
    }

    memset(storage, 0, sizeof(*storage));
    storage->saved_security =
        ECHOEAR_WIFI_SECURITY_UNKNOWN;

    if (path == NULL)
    {
        return false;
    }

    file = fopen(path, "rb");

    if (file == NULL)
    {
        return false;
    }

    storage->exists = true;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *separator;
        char *key;
        char *value;

        key = trim_whitespace(line);
        key = skip_utf8_bom(key);

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
            echoear_wifi_manager_parse_security(
                value,
                &storage->saved_security);
        }
    }

    fclose(file);

    return true;
}

static bool write_device_storage(
    const char *path,
    bool completed,
    bool credentials_saved,
    const char *ssid,
    echoear_wifi_security_t security)
{
    FILE *file;
    char temporary_path[CREDENTIALS_MOCK_PATH_MAX];

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    {
        int path_length = snprintf(
            temporary_path,
            sizeof(temporary_path),
            "%s.tmp",
            path);

        if (path_length < 0 ||
            (size_t)path_length >=
                sizeof(temporary_path))
        {
            return false;
        }
    }

    file = fopen(temporary_path, "wb");

    if (file == NULL)
    {
        return false;
    }

    /*
     * Binary mode plus plain ASCII writes guarantees that no UTF-8 BOM
     * is added before setup_completed.
     */
    fprintf(
        file,
        "setup_completed=%d\n"
        "wifi_credentials_saved=%d\n"
        "force_provisioning=0\n"
        "saved_ssid=%s\n"
        "saved_security=%s\n",
        completed ? 1 : 0,
        credentials_saved ? 1 : 0,
        credentials_saved && ssid != NULL
            ? ssid
            : "",
        credentials_saved
            ? echoear_wifi_manager_security_name(
                  security)
            : "unknown");

    if (fflush(file) != 0 ||
        ferror(file) != 0)
    {
        fclose(file);
        remove(temporary_path);
        return false;
    }

    if (fclose(file) != 0)
    {
        remove(temporary_path);
        return false;
    }

    remove(path);

    if (rename(temporary_path, path) != 0)
    {
        remove(temporary_path);
        return false;
    }

    return true;
}

static bool verify_saved_storage(
    const char *path,
    const char *ssid,
    echoear_wifi_security_t security)
{
    mock_device_storage_t storage;

    if (!read_device_storage(path, &storage))
    {
        return false;
    }

    if (!storage.setup_completed ||
        !storage.wifi_credentials_saved ||
        storage.force_provisioning)
    {
        return false;
    }

    if (ssid != NULL &&
        ssid[0] != '\0' &&
        strcmp(storage.saved_ssid, ssid) != 0)
    {
        return false;
    }

    if (security != ECHOEAR_WIFI_SECURITY_UNKNOWN &&
        storage.saved_security != security)
    {
        return false;
    }

    return true;
}

static echoear_wifi_security_t resolve_security(
    echoear_wifi_manager_t *wifi,
    const char *ssid,
    echoear_wifi_security_t fallback)
{
    const echoear_wifi_network_t *network;

    if (fallback != ECHOEAR_WIFI_SECURITY_UNKNOWN)
    {
        return fallback;
    }

    if (wifi == NULL)
    {
        return ECHOEAR_WIFI_SECURITY_UNKNOWN;
    }

    if (wifi->selected_security !=
        ECHOEAR_WIFI_SECURITY_UNKNOWN)
    {
        return wifi->selected_security;
    }

    network = echoear_wifi_manager_find_network(ssid);

    if (network != NULL)
    {
        return network->security;
    }

    return ECHOEAR_WIFI_SECURITY_UNKNOWN;
}

static void print_credentials_state(void)
{
    echoear_credentials_store_t *credentials =
        echoear_credentials_store_get();

    if (credentials == NULL)
    {
        return;
    }

    printf(
        "[Credentials] state=%s error=%s "
        "saved=%d saved_ssid=%s "
        "pending=%s security=%s password=%d "
        "save=%d delete=%d generation=%lu\n",
        echoear_credentials_store_state_name(
            credentials->state),
        echoear_credentials_store_error_name(
            credentials->error),
        credentials->credentials_saved ? 1 : 0,
        credentials->saved_ssid[0] != '\0'
            ? credentials->saved_ssid
            : "<none>",
        credentials->pending_ssid[0] != '\0'
            ? credentials->pending_ssid
            : "<none>",
        echoear_wifi_manager_security_name(
            credentials->credentials_saved
                ? credentials->saved_security
                : credentials->pending_security),
        credentials->password_present ? 1 : 0,
        credentials->save_requested ? 1 : 0,
        credentials->delete_requested ? 1 : 0,
        (unsigned long)credentials->generation);

    fflush(stdout);
}

static void print_credentials_state_if_changed(void)
{
    static char previous[640];
    char current[640];

    echoear_credentials_store_t *credentials =
        echoear_credentials_store_get();

    if (credentials == NULL)
    {
        return;
    }

    snprintf(
        current,
        sizeof(current),
        "%s|%s|%d|%s|%s|%s|%d|%d|%d|%lu",
        echoear_credentials_store_state_name(
            credentials->state),
        echoear_credentials_store_error_name(
            credentials->error),
        credentials->credentials_saved ? 1 : 0,
        credentials->saved_ssid,
        credentials->pending_ssid,
        echoear_wifi_manager_security_name(
            credentials->credentials_saved
                ? credentials->saved_security
                : credentials->pending_security),
        credentials->password_present ? 1 : 0,
        credentials->save_requested ? 1 : 0,
        credentials->delete_requested ? 1 : 0,
        (unsigned long)credentials->generation);

    if (strcmp(previous, current) == 0)
    {
        return;
    }

    copy_text(previous, sizeof(previous), current);
    print_credentials_state();
}

static void show_saving_state(void)
{
    echoear_captive_portal_set_state(
        ECHOEAR_PORTAL_CONNECTING);

    echoear_captive_portal_set_page(
        ECHOEAR_PORTAL_PAGE_CONNECTING);

    echoear_captive_portal_set_status_message(
        "Saving Wi-Fi settings");

    echoear_provisioning_set_state(
        ECHOEAR_PROVISIONING_SAVING);

    echoear_pro_ui_apply_provisioning_state();
}

static void show_save_error(
    echoear_credentials_error_t error)
{
    echoear_credentials_store_set_error(error);

    echoear_captive_portal_set_error(
        ECHOEAR_PORTAL_ERROR_INTERNAL);

    echoear_provisioning_set_error(
        ECHOEAR_PROVISIONING_ERROR_SAVE_FAILED);

    echoear_pro_ui_apply_provisioning_state();
    print_credentials_state_if_changed();
}

static void complete_provisioning(
    const char *ssid)
{
    echoear_first_boot_t *first_boot;

    echoear_first_boot_set_setup_completed(true);

    echoear_first_boot_set_wifi_credentials_saved(
        true);

    echoear_first_boot_set_force_provisioning(false);
    echoear_first_boot_apply();

    echoear_provisioning_set_target_ssid(ssid);

    echoear_provisioning_set_setup_completed(true);

    echoear_captive_portal_mark_success();
    echoear_captive_portal_set_enabled(false);

    /*
     * The simulator Wi-Fi manager represents the provisioning operation.
     * Normal mode does not keep this temporary flow active.
     */
    echoear_wifi_manager_reset();

    echoear_pro_ui_apply_app_state();

    first_boot = echoear_first_boot_get();

    printf(
        "[ProvisioningComplete] storage_saved=1 "
        "setup=%d credentials=%d force=%d "
        "mode=%s ssid=%s\n",
        first_boot != NULL &&
                first_boot->setup_completed
            ? 1
            : 0,
        first_boot != NULL &&
                first_boot->wifi_credentials_saved
            ? 1
            : 0,
        first_boot != NULL &&
                first_boot->force_provisioning
            ? 1
            : 0,
        first_boot != NULL
            ? echoear_first_boot_mode_name(
                  first_boot->mode)
            : "unknown",
        ssid != NULL && ssid[0] != '\0'
            ? ssid
            : "<none>");

    fflush(stdout);
}

static void return_to_provisioning(void)
{
    echoear_first_boot_set_setup_completed(false);

    echoear_first_boot_set_wifi_credentials_saved(
        false);

    echoear_first_boot_set_force_provisioning(false);
    echoear_first_boot_apply();

    echoear_provisioning_set_enabled(true);

    echoear_provisioning_set_setup_completed(false);

    echoear_provisioning_set_state(
        ECHOEAR_PROVISIONING_AP_STARTING);

    echoear_pro_ui_apply_provisioning_state();

    printf(
        "[ProvisioningReset] storage_saved=0 "
        "setup=0 credentials=0 mode=provisioning\n");

    fflush(stdout);
}

static bool process_save(
    const char *device_storage_path,
    bool auto_complete,
    mock_result_t save_result)
{
    echoear_credentials_store_t *credentials =
        echoear_credentials_store_get();

    if (credentials == NULL)
    {
        return false;
    }

    if (credentials->state ==
        ECHOEAR_CREDENTIALS_SAVE_REQUESTED)
    {
        echoear_credentials_store_mark_saving();
        show_saving_state();
        print_credentials_state_if_changed();
    }

    if (credentials->state !=
            ECHOEAR_CREDENTIALS_SAVING &&
        credentials->state !=
            ECHOEAR_CREDENTIALS_VERIFYING)
    {
        return true;
    }

    if (!auto_complete)
    {
        return true;
    }

    if (credentials->state ==
        ECHOEAR_CREDENTIALS_SAVING)
    {
        if (save_result == MOCK_RESULT_WRITE_FAILED ||
            save_result == MOCK_RESULT_INTERNAL)
        {
            show_save_error(
                ECHOEAR_CREDENTIALS_ERROR_WRITE_FAILED);

            return false;
        }

        echoear_credentials_store_mark_verifying();
        show_saving_state();
        print_credentials_state_if_changed();
    }

    if (!write_device_storage(
            device_storage_path,
            true,
            true,
            credentials->pending_ssid,
            credentials->pending_security))
    {
        show_save_error(
            ECHOEAR_CREDENTIALS_ERROR_WRITE_FAILED);

        return false;
    }

    if (save_result == MOCK_RESULT_VERIFY_FAILED ||
        !verify_saved_storage(
            device_storage_path,
            credentials->pending_ssid,
            credentials->pending_security))
    {
        /*
         * Roll back the first-boot flags so a verification failure cannot
         * accidentally boot into normal mode after a restart.
         */
        write_device_storage(
            device_storage_path,
            false,
            false,
            NULL,
            ECHOEAR_WIFI_SECURITY_UNKNOWN);

        show_save_error(
            ECHOEAR_CREDENTIALS_ERROR_VERIFY_FAILED);

        return false;
    }

    {
        char saved_ssid[ECHOEAR_CREDENTIALS_SSID_MAX];
        echoear_wifi_security_t saved_security =
            credentials->pending_security;

        copy_text(
            saved_ssid,
            sizeof(saved_ssid),
            credentials->pending_ssid);

        echoear_credentials_store_mark_saved(
            saved_ssid,
            saved_security);

        print_credentials_state_if_changed();
        complete_provisioning(saved_ssid);
    }

    return true;
}

static bool process_delete(
    const char *device_storage_path,
    bool auto_complete,
    mock_result_t delete_result)
{
    echoear_credentials_store_t *credentials =
        echoear_credentials_store_get();

    if (credentials == NULL ||
        credentials->state !=
            ECHOEAR_CREDENTIALS_DELETE_REQUESTED)
    {
        return true;
    }

    echoear_credentials_store_mark_deleting();
    print_credentials_state_if_changed();

    if (!auto_complete)
    {
        return true;
    }

    if (delete_result == MOCK_RESULT_DELETE_FAILED ||
        delete_result == MOCK_RESULT_INTERNAL ||
        !write_device_storage(
            device_storage_path,
            false,
            false,
            NULL,
            ECHOEAR_WIFI_SECURITY_UNKNOWN))
    {
        show_save_error(
            ECHOEAR_CREDENTIALS_ERROR_DELETE_FAILED);

        return false;
    }

    echoear_credentials_store_mark_deleted();
    print_credentials_state_if_changed();
    return_to_provisioning();

    return true;
}

bool echoear_credentials_store_mock_load(
    const char *config_path,
    const char *device_storage_path)
{
    FILE *file;
    char line[CREDENTIALS_MOCK_LINE_MAX];

    bool auto_complete = true;
    bool configured_saved = false;
    bool password_present = false;
    bool save_requested = false;
    bool delete_requested = false;

    mock_result_t save_result = MOCK_RESULT_SUCCESS;
    mock_result_t delete_result = MOCK_RESULT_SUCCESS;

    echoear_credentials_state_t requested_state =
        ECHOEAR_CREDENTIALS_EMPTY;

    echoear_credentials_error_t requested_error =
        ECHOEAR_CREDENTIALS_ERROR_NONE;

    char configured_ssid[
        ECHOEAR_CREDENTIALS_SSID_MAX] = "";

    echoear_wifi_security_t configured_security =
        ECHOEAR_WIFI_SECURITY_UNKNOWN;

    mock_device_storage_t storage;
    echoear_wifi_manager_t *wifi;
    echoear_credentials_store_t *credentials;

    if (config_path == NULL)
    {
        print_credentials_state_if_changed();
        return false;
    }

    file = fopen(config_path, "rb");

    if (file == NULL)
    {
        show_save_error(
            ECHOEAR_CREDENTIALS_ERROR_INTERNAL);

        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *separator;
        char *key;
        char *value;

        key = trim_whitespace(line);
        key = skip_utf8_bom(key);

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

        if (strcmp(key, "credentials_state") == 0)
        {
            echoear_credentials_store_parse_state(
                value,
                &requested_state);
        }
        else if (strcmp(
                     key,
                     "credentials_error") == 0)
        {
            echoear_credentials_store_parse_error(
                value,
                &requested_error);
        }
        else if (strcmp(
                     key,
                     "credentials_save_result") == 0)
        {
            save_result = parse_result(value);
        }
        else if (strcmp(
                     key,
                     "credentials_delete_result") == 0)
        {
            delete_result = parse_result(value);
        }
        else if (strcmp(
                     key,
                     "credentials_auto_complete") == 0)
        {
            auto_complete = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "credentials_saved") == 0)
        {
            configured_saved = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "credentials_saved_ssid") == 0)
        {
            copy_text(
                configured_ssid,
                sizeof(configured_ssid),
                value);
        }
        else if (strcmp(
                     key,
                     "credentials_saved_security") == 0)
        {
            echoear_wifi_manager_parse_security(
                value,
                &configured_security);
        }
        else if (strcmp(
                     key,
                     "credentials_password_present") == 0)
        {
            password_present = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "credentials_save_requested") == 0)
        {
            save_requested = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "credentials_delete_requested") == 0)
        {
            delete_requested = parse_bool(value);
        }
    }

    fclose(file);

    read_device_storage(
        device_storage_path,
        &storage);

    credentials = echoear_credentials_store_get();
    wifi = echoear_wifi_manager_get();

    if (storage.wifi_credentials_saved)
    {
        echoear_credentials_store_load_metadata(
            true,
            storage.saved_ssid,
            storage.saved_security);
    }
    else if (configured_saved &&
             credentials != NULL &&
             !credentials->credentials_saved)
    {
        echoear_credentials_store_load_metadata(
            true,
            configured_ssid,
            configured_security);
    }

    if (delete_requested ||
        requested_state ==
            ECHOEAR_CREDENTIALS_DELETE_REQUESTED)
    {
        echoear_credentials_store_request_delete();
        print_credentials_state_if_changed();

        return process_delete(
            device_storage_path,
            auto_complete,
            delete_result);
    }

    if (requested_error !=
            ECHOEAR_CREDENTIALS_ERROR_NONE ||
        requested_state ==
            ECHOEAR_CREDENTIALS_ERROR)
    {
        if (requested_error ==
            ECHOEAR_CREDENTIALS_ERROR_NONE)
        {
            requested_error =
                ECHOEAR_CREDENTIALS_ERROR_INTERNAL;
        }

        show_save_error(requested_error);
        return false;
    }

    if (storage.wifi_credentials_saved)
    {
        print_credentials_state_if_changed();
        return true;
    }

    if (credentials == NULL)
    {
        return false;
    }

    if (credentials->state ==
            ECHOEAR_CREDENTIALS_SAVE_REQUESTED ||
        credentials->state ==
            ECHOEAR_CREDENTIALS_SAVING ||
        credentials->state ==
            ECHOEAR_CREDENTIALS_VERIFYING)
    {
        return process_save(
            device_storage_path,
            auto_complete,
            save_result);
    }

    if ((wifi != NULL &&
         echoear_wifi_manager_is_connected()) ||
        save_requested ||
        requested_state ==
            ECHOEAR_CREDENTIALS_SAVE_REQUESTED)
    {
        char target_ssid[
            ECHOEAR_CREDENTIALS_SSID_MAX] = "";

        echoear_wifi_security_t target_security =
            configured_security;

        const char *temporary_password = "";

        if (wifi != NULL &&
            wifi->connected_ssid[0] != '\0')
        {
            copy_text(
                target_ssid,
                sizeof(target_ssid),
                wifi->connected_ssid);
        }
        else if (configured_ssid[0] != '\0')
        {
            copy_text(
                target_ssid,
                sizeof(target_ssid),
                configured_ssid);
        }
        else if (wifi != NULL &&
                 wifi->selected_ssid[0] != '\0')
        {
            copy_text(
                target_ssid,
                sizeof(target_ssid),
                wifi->selected_ssid);
        }

        target_security = resolve_security(
            wifi,
            target_ssid,
            target_security);

        /*
         * A successful secure Wi-Fi connection proves that credentials
         * existed earlier in the flow. The simulator uses only this dummy
         * value and never writes it to device_storage.txt.
         */
        if (target_security !=
                ECHOEAR_WIFI_SECURITY_OPEN &&
            (password_present ||
             (wifi != NULL &&
              echoear_wifi_manager_is_connected())))
        {
            temporary_password = "mock-password";
        }

        echoear_credentials_store_request_save(
            target_ssid,
            target_security,
            temporary_password);

        print_credentials_state_if_changed();

        return process_save(
            device_storage_path,
            auto_complete,
            save_result);
    }

    if (requested_state ==
        ECHOEAR_CREDENTIALS_SAVING)
    {
        echoear_credentials_store_mark_saving();
        show_saving_state();
    }
    else if (requested_state ==
             ECHOEAR_CREDENTIALS_VERIFYING)
    {
        echoear_credentials_store_mark_verifying();
        show_saving_state();
    }

    print_credentials_state_if_changed();

    return true;
}
