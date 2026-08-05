#include "echoear_captive_portal_mock.h"

#include "echoear_captive_portal.h"
#include "echoear_pro_ui.h"
#include "echoear_provisioning.h"
#include "echoear_softap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static echoear_provisioning_error_t
map_portal_error(
    echoear_captive_portal_error_t error)
{
    switch (error)
    {
    case ECHOEAR_PORTAL_ERROR_AUTH_FAILED:
        return ECHOEAR_PROVISIONING_ERROR_AUTH_FAILED;

    case ECHOEAR_PORTAL_ERROR_NETWORK_NOT_FOUND:
        return ECHOEAR_PROVISIONING_ERROR_NETWORK_NOT_FOUND;

    case ECHOEAR_PORTAL_ERROR_TIMEOUT:
        return ECHOEAR_PROVISIONING_ERROR_TIMEOUT;

    case ECHOEAR_PORTAL_ERROR_INVALID_REQUEST:
    case ECHOEAR_PORTAL_ERROR_NETWORK_REQUIRED:
    case ECHOEAR_PORTAL_ERROR_PASSWORD_REQUIRED:
    case ECHOEAR_PORTAL_ERROR_INTERNAL:
        return ECHOEAR_PROVISIONING_ERROR_UNKNOWN;

    case ECHOEAR_PORTAL_ERROR_NONE:
    default:
        return ECHOEAR_PROVISIONING_ERROR_NONE;
    }
}

static void sync_provisioning_from_portal(void)
{
    echoear_captive_portal_t *portal =
        echoear_captive_portal_get();

    echoear_softap_t *softap =
        echoear_softap_get();

    if (portal == NULL)
    {
        return;
    }

    if (!portal->enabled)
    {
        echoear_pro_ui_apply_provisioning_state();
        return;
    }

    echoear_provisioning_set_enabled(true);
    echoear_provisioning_set_setup_completed(false);

    if (portal->selected_ssid[0] != '\0')
    {
        echoear_provisioning_set_target_ssid(
            portal->selected_ssid);
    }

    if (portal->error != ECHOEAR_PORTAL_ERROR_NONE ||
        portal->state == ECHOEAR_PORTAL_ERROR)
    {
        echoear_provisioning_set_error(
            map_portal_error(portal->error));

        echoear_pro_ui_apply_provisioning_state();
        return;
    }

    switch (portal->state)
    {
    case ECHOEAR_PORTAL_STARTING:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_STARTING);
        break;

    case ECHOEAR_PORTAL_READY:
    case ECHOEAR_PORTAL_NETWORK_SELECTED:
        if (softap != NULL &&
            softap->connected_clients > 0U)
        {
            echoear_provisioning_set_state(
                ECHOEAR_PROVISIONING_CLIENT_CONNECTED);
        }
        else
        {
            echoear_provisioning_set_state(
                ECHOEAR_PROVISIONING_AP_READY);
        }
        break;

    case ECHOEAR_PORTAL_CREDENTIALS_RECEIVED:
    case ECHOEAR_PORTAL_CONNECTING:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_WIFI_CONNECTING);
        break;

    case ECHOEAR_PORTAL_SUCCESS:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_WIFI_CONNECTED);
        break;

    case ECHOEAR_PORTAL_STOPPED:
    default:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_STARTING);
        break;
    }

    echoear_pro_ui_apply_provisioning_state();
}

static void print_portal_state_if_changed(void)
{
    static char previous[512];
    char current[512];

    echoear_captive_portal_t *portal =
        echoear_captive_portal_get();

    if (portal == NULL)
    {
        return;
    }

    snprintf(
        current,
        sizeof(current),
        "[Portal] enabled=%d state=%s page=%s "
        "ssid=%s secured=%d password=%d "
        "scan=%d connect=%d redirect=%d "
        "route=%s error=%s",
        portal->enabled ? 1 : 0,
        echoear_captive_portal_state_name(
            portal->state),
        echoear_captive_portal_page_name(
            portal->page),
        portal->selected_ssid[0] != '\0'
            ? portal->selected_ssid
            : "<none>",
        portal->selected_network_secured ? 1 : 0,
        portal->password_present ? 1 : 0,
        portal->scan_requested ? 1 : 0,
        portal->connect_requested ? 1 : 0,
        portal->redirect_enabled ? 1 : 0,
        portal->route,
        echoear_captive_portal_error_name(
            portal->error));

    if (strcmp(previous, current) == 0)
    {
        return;
    }

    copy_text(previous, sizeof(previous), current);

    printf("%s\n", current);
    fflush(stdout);
}

bool echoear_captive_portal_mock_load(
    const char *path)
{
    echoear_captive_portal_t *current;
    FILE *file;
    char line[320];

    bool enabled_auto = true;
    bool enabled = false;
    bool redirect_enabled = true;
    bool scan_requested = false;
    bool connect_requested = false;
    bool password_present = false;
    bool selected_network_secured = true;

    echoear_captive_portal_state_t state;
    echoear_captive_portal_page_t page;
    echoear_captive_portal_error_t error;

    char title[ECHOEAR_PORTAL_TITLE_MAX];
    char locale[ECHOEAR_PORTAL_LOCALE_MAX];
    char portal_url[ECHOEAR_PORTAL_URL_MAX];
    char route[ECHOEAR_PORTAL_ROUTE_MAX];
    char selected_ssid[ECHOEAR_PORTAL_SSID_MAX];
    char status_message[ECHOEAR_PORTAL_MESSAGE_MAX];

    current = echoear_captive_portal_get();

    if (current == NULL)
    {
        return false;
    }

    enabled = current->enabled;
    redirect_enabled = current->redirect_enabled;
    state = current->state;
    page = current->page;
    error = current->error;

    copy_text(title, sizeof(title), current->title);
    copy_text(locale, sizeof(locale), current->locale);

    copy_text(
        portal_url,
        sizeof(portal_url),
        current->portal_url);

    copy_text(route, sizeof(route), current->route);

    copy_text(
        selected_ssid,
        sizeof(selected_ssid),
        current->selected_ssid);

    copy_text(
        status_message,
        sizeof(status_message),
        current->status_message);

    selected_network_secured =
        current->selected_network_secured;

    if (path == NULL)
    {
        echoear_captive_portal_set_enabled(
            echoear_softap_is_portal_ready());

        sync_provisioning_from_portal();
        print_portal_state_if_changed();
        return false;
    }

    file = fopen(path, "r");

    if (file == NULL)
    {
        echoear_captive_portal_set_enabled(
            echoear_softap_is_portal_ready());

        echoear_captive_portal_set_error(
            ECHOEAR_PORTAL_ERROR_INTERNAL);

        sync_provisioning_from_portal();
        print_portal_state_if_changed();
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *separator;
        char *key;
        char *value;

        key = trim_whitespace(line);

        if (key == NULL ||
            *key == '\0' ||
            *key == '#')
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

        if (strcmp(key, "portal_enabled") == 0)
        {
            enabled_auto =
                strcmp(value, "auto") == 0;

            if (!enabled_auto)
            {
                enabled = parse_bool(value);
            }
        }
        else if (strcmp(key, "portal_state") == 0)
        {
            echoear_captive_portal_parse_state(
                value,
                &state);
        }
        else if (strcmp(key, "portal_page") == 0)
        {
            echoear_captive_portal_parse_page(
                value,
                &page);
        }
        else if (strcmp(key, "portal_error") == 0)
        {
            echoear_captive_portal_parse_error(
                value,
                &error);
        }
        else if (strcmp(
                     key,
                     "portal_redirect_enabled") == 0)
        {
            redirect_enabled = parse_bool(value);
        }
        else if (strcmp(key, "portal_title") == 0)
        {
            copy_text(title, sizeof(title), value);
        }
        else if (strcmp(key, "portal_locale") == 0)
        {
            copy_text(locale, sizeof(locale), value);
        }
        else if (strcmp(key, "portal_url") == 0)
        {
            copy_text(
                portal_url,
                sizeof(portal_url),
                value);
        }
        else if (strcmp(key, "portal_route") == 0)
        {
            copy_text(route, sizeof(route), value);
        }
        else if (strcmp(
                     key,
                     "portal_scan_requested") == 0)
        {
            scan_requested = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "portal_selected_ssid") == 0)
        {
            copy_text(
                selected_ssid,
                sizeof(selected_ssid),
                value);
        }
        else if (strcmp(
                     key,
                     "portal_selected_network_secured") == 0)
        {
            selected_network_secured =
                parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "portal_password_present") == 0)
        {
            password_present = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "portal_connect_requested") == 0)
        {
            connect_requested = parse_bool(value);
        }
        else if (strcmp(key, "portal_status") == 0)
        {
            copy_text(
                status_message,
                sizeof(status_message),
                value);
        }
    }

    fclose(file);

    if (enabled_auto)
    {
        enabled = echoear_softap_is_portal_ready();
    }

    echoear_captive_portal_set_enabled(enabled);

    if (!enabled)
    {
        sync_provisioning_from_portal();
        print_portal_state_if_changed();
        return true;
    }

    echoear_captive_portal_set_redirect_enabled(
        redirect_enabled);

    echoear_captive_portal_set_title(title);
    echoear_captive_portal_set_locale(locale);

    echoear_captive_portal_set_portal_url(
        portal_url);

    echoear_captive_portal_mark_request(route);

    echoear_captive_portal_set_state(state);
    echoear_captive_portal_set_page(page);

    if (scan_requested)
    {
        echoear_captive_portal_request_scan();
    }

    if (selected_ssid[0] != '\0')
    {
        echoear_captive_portal_select_network(
            selected_ssid,
            selected_network_secured);
    }
    else
    {
        echoear_captive_portal_clear_selection();
    }

    if (connect_requested)
    {
        echoear_captive_portal_submit_credentials(
            password_present
                ? "mock-password"
                : "");
    }

    if (state == ECHOEAR_PORTAL_CONNECTING)
    {
        echoear_captive_portal_mark_connecting();
    }
    else if (state == ECHOEAR_PORTAL_SUCCESS)
    {
        echoear_captive_portal_mark_success();
    }

    if (error != ECHOEAR_PORTAL_ERROR_NONE ||
        state == ECHOEAR_PORTAL_ERROR)
    {
        if (error == ECHOEAR_PORTAL_ERROR_NONE)
        {
            error = ECHOEAR_PORTAL_ERROR_INTERNAL;
        }

        echoear_captive_portal_set_error(error);
    }

    echoear_captive_portal_set_status_message(
        status_message);

    sync_provisioning_from_portal();
    print_portal_state_if_changed();

    return true;
}
