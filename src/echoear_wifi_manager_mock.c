#include "echoear_wifi_manager_mock.h"

#include "echoear_captive_portal.h"
#include "echoear_pro_ui.h"
#include "echoear_provisioning.h"
#include "echoear_softap.h"
#include "echoear_wifi_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOCK_LINE_MAX 384

typedef struct
{
    char ssid[ECHOEAR_WIFI_SSID_MAX];
    char bssid[ECHOEAR_WIFI_BSSID_MAX];
    int16_t rssi;
    uint8_t channel;
    echoear_wifi_security_t security;
    bool hidden;
} mock_network_t;

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

static long parse_long(
    const char *value,
    long fallback)
{
    char *end;
    long parsed;

    if (value == NULL || value[0] == '\0')
    {
        return fallback;
    }

    parsed = strtol(value, &end, 10);

    if (end == value || *end != '\0')
    {
        return fallback;
    }

    return parsed;
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

static size_t split_pipe_fields(
    char *value,
    char **fields,
    size_t field_capacity)
{
    size_t count = 0U;
    char *cursor;

    if (value == NULL ||
        fields == NULL ||
        field_capacity == 0U)
    {
        return 0U;
    }

    cursor = value;

    while (count < field_capacity)
    {
        char *separator;

        fields[count++] = cursor;
        separator = strchr(cursor, '|');

        if (separator == NULL)
        {
            break;
        }

        *separator = '\0';
        cursor = separator + 1;
    }

    return count;
}

static bool parse_network(
    char *value,
    mock_network_t *network)
{
    char *fields[6];
    size_t field_count;
    long rssi;
    long channel;

    if (value == NULL || network == NULL)
    {
        return false;
    }

    memset(network, 0, sizeof(*network));

    field_count = split_pipe_fields(
        value,
        fields,
        sizeof(fields) / sizeof(fields[0]));

    if (field_count < 4U)
    {
        return false;
    }

    copy_text(
        network->ssid,
        sizeof(network->ssid),
        trim_whitespace(fields[0]));

    if (network->ssid[0] == '\0')
    {
        return false;
    }

    rssi = parse_long(
        trim_whitespace(fields[1]),
        -100L);

    if (rssi < -127L)
    {
        rssi = -127L;
    }
    else if (rssi > 0L)
    {
        rssi = 0L;
    }

    network->rssi = (int16_t)rssi;

    if (!echoear_wifi_manager_parse_security(
            trim_whitespace(fields[2]),
            &network->security))
    {
        network->security =
            ECHOEAR_WIFI_SECURITY_UNKNOWN;
    }

    channel = parse_long(
        trim_whitespace(fields[3]),
        1L);

    if (channel < 1L)
    {
        channel = 1L;
    }
    else if (channel > 255L)
    {
        channel = 255L;
    }

    network->channel = (uint8_t)channel;

    if (field_count >= 5U)
    {
        copy_text(
            network->bssid,
            sizeof(network->bssid),
            trim_whitespace(fields[4]));
    }

    if (field_count >= 6U)
    {
        network->hidden = parse_bool(
            trim_whitespace(fields[5]));
    }

    return true;
}

static echoear_captive_portal_error_t
map_wifi_error_to_portal(
    echoear_wifi_error_t error)
{
    switch (error)
    {
    case ECHOEAR_WIFI_ERROR_NETWORK_NOT_FOUND:
        return ECHOEAR_PORTAL_ERROR_NETWORK_NOT_FOUND;

    case ECHOEAR_WIFI_ERROR_PASSWORD_REQUIRED:
        return ECHOEAR_PORTAL_ERROR_PASSWORD_REQUIRED;

    case ECHOEAR_WIFI_ERROR_AUTH_FAILED:
        return ECHOEAR_PORTAL_ERROR_AUTH_FAILED;

    case ECHOEAR_WIFI_ERROR_TIMEOUT:
        return ECHOEAR_PORTAL_ERROR_TIMEOUT;

    case ECHOEAR_WIFI_ERROR_SCAN_FAILED:
    case ECHOEAR_WIFI_ERROR_DHCP_FAILED:
    case ECHOEAR_WIFI_ERROR_UNSUPPORTED_SECURITY:
    case ECHOEAR_WIFI_ERROR_INTERNAL:
        return ECHOEAR_PORTAL_ERROR_INTERNAL;

    case ECHOEAR_WIFI_ERROR_NONE:
    default:
        return ECHOEAR_PORTAL_ERROR_NONE;
    }
}

static echoear_provisioning_error_t
map_wifi_error_to_provisioning(
    echoear_wifi_error_t error)
{
    switch (error)
    {
    case ECHOEAR_WIFI_ERROR_SCAN_FAILED:
        return ECHOEAR_PROVISIONING_ERROR_SCAN_FAILED;

    case ECHOEAR_WIFI_ERROR_NETWORK_NOT_FOUND:
        return ECHOEAR_PROVISIONING_ERROR_NETWORK_NOT_FOUND;

    case ECHOEAR_WIFI_ERROR_AUTH_FAILED:
        return ECHOEAR_PROVISIONING_ERROR_AUTH_FAILED;

    case ECHOEAR_WIFI_ERROR_DHCP_FAILED:
        return ECHOEAR_PROVISIONING_ERROR_DHCP_FAILED;

    case ECHOEAR_WIFI_ERROR_TIMEOUT:
        return ECHOEAR_PROVISIONING_ERROR_TIMEOUT;

    case ECHOEAR_WIFI_ERROR_PASSWORD_REQUIRED:
    case ECHOEAR_WIFI_ERROR_UNSUPPORTED_SECURITY:
    case ECHOEAR_WIFI_ERROR_INTERNAL:
        return ECHOEAR_PROVISIONING_ERROR_UNKNOWN;

    case ECHOEAR_WIFI_ERROR_NONE:
    default:
        return ECHOEAR_PROVISIONING_ERROR_NONE;
    }
}

static void sync_portal_and_provisioning(
    bool wifi_enabled)
{
    echoear_wifi_manager_t *wifi =
        echoear_wifi_manager_get();

    echoear_captive_portal_t *portal =
        echoear_captive_portal_get();

    echoear_softap_t *softap =
        echoear_softap_get();

    char status[ECHOEAR_PORTAL_MESSAGE_MAX];

    if (wifi == NULL || !wifi_enabled)
    {
        return;
    }

    echoear_provisioning_set_enabled(true);
    echoear_provisioning_set_setup_completed(false);

    if (wifi->selected_ssid[0] != '\0')
    {
        echoear_provisioning_set_target_ssid(
            wifi->selected_ssid);
    }

    if (wifi->error != ECHOEAR_WIFI_ERROR_NONE ||
        wifi->state == ECHOEAR_WIFI_ERROR)
    {
        echoear_captive_portal_set_error(
            map_wifi_error_to_portal(wifi->error));

        echoear_provisioning_set_error(
            map_wifi_error_to_provisioning(
                wifi->error));

        echoear_pro_ui_apply_provisioning_state();
        return;
    }

    switch (wifi->state)
    {
    case ECHOEAR_WIFI_SCAN_REQUESTED:
    case ECHOEAR_WIFI_SCANNING:
        echoear_captive_portal_set_state(
            ECHOEAR_PORTAL_READY);
        echoear_captive_portal_set_page(
            ECHOEAR_PORTAL_PAGE_WIFI_LIST);
        echoear_captive_portal_set_status_message(
            "Scanning for Wi-Fi networks");

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_SCANNING);
        break;

    case ECHOEAR_WIFI_SCAN_COMPLETE:
        echoear_captive_portal_set_state(
            ECHOEAR_PORTAL_READY);
        echoear_captive_portal_set_page(
            ECHOEAR_PORTAL_PAGE_WIFI_LIST);

        snprintf(
            status,
            sizeof(status),
            "%u Wi-Fi network(s) found",
            (unsigned int)wifi->network_count);

        echoear_captive_portal_set_status_message(
            status);

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

    case ECHOEAR_WIFI_CONNECT_REQUESTED:
        echoear_captive_portal_set_state(
            ECHOEAR_PORTAL_CREDENTIALS_RECEIVED);
        echoear_captive_portal_set_page(
            ECHOEAR_PORTAL_PAGE_CONNECTING);
        echoear_captive_portal_set_status_message(
            "Wi-Fi connection requested");

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_WIFI_CONNECTING);
        break;

    case ECHOEAR_WIFI_CONNECTING:
        echoear_captive_portal_mark_connecting();

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_WIFI_CONNECTING);
        break;

    case ECHOEAR_WIFI_CONNECTED:
        echoear_captive_portal_mark_success();

        echoear_provisioning_set_target_ssid(
            wifi->connected_ssid);
        echoear_provisioning_set_ip_address(
            wifi->ip_address);
        echoear_provisioning_set_wifi_rssi(
            wifi->connected_rssi);
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_WIFI_CONNECTED);
        break;

    case ECHOEAR_WIFI_DISCONNECTED:
        echoear_captive_portal_set_state(
            ECHOEAR_PORTAL_READY);
        echoear_captive_portal_set_page(
            ECHOEAR_PORTAL_PAGE_WIFI_LIST);
        echoear_captive_portal_set_status_message(
            "Wi-Fi disconnected");

        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_READY);
        break;

    case ECHOEAR_WIFI_IDLE:
    default:
        if (portal != NULL && portal->enabled)
        {
            echoear_captive_portal_set_state(
                ECHOEAR_PORTAL_READY);
            echoear_captive_portal_set_page(
                ECHOEAR_PORTAL_PAGE_WIFI_LIST);

            echoear_provisioning_set_state(
                ECHOEAR_PROVISIONING_AP_READY);
        }
        break;
    }

    echoear_pro_ui_apply_provisioning_state();
}

static void print_wifi_state_if_changed(
    bool enabled)
{
    static char previous[640];
    char current[640];

    echoear_wifi_manager_t *wifi =
        echoear_wifi_manager_get();

    if (wifi == NULL)
    {
        return;
    }

    snprintf(
        current,
        sizeof(current),
        "[WiFi] enabled=%d state=%s error=%s "
        "networks=%u generation=%lu selected=%s "
        "security=%s password=%d scan=%d connect=%d "
        "connected=%s ip=%s rssi=%d",
        enabled ? 1 : 0,
        echoear_wifi_manager_state_name(
            wifi->state),
        echoear_wifi_manager_error_name(
            wifi->error),
        (unsigned int)wifi->network_count,
        (unsigned long)wifi->scan_generation,
        wifi->selected_ssid[0] != '\0'
            ? wifi->selected_ssid
            : "<none>",
        echoear_wifi_manager_security_name(
            wifi->selected_security),
        wifi->password_present ? 1 : 0,
        wifi->scan_requested ? 1 : 0,
        wifi->connect_requested ? 1 : 0,
        wifi->connected_ssid[0] != '\0'
            ? wifi->connected_ssid
            : "<none>",
        wifi->ip_address[0] != '\0'
            ? wifi->ip_address
            : "<none>",
        (int)wifi->connected_rssi);

    if (strcmp(previous, current) == 0)
    {
        return;
    }

    copy_text(previous, sizeof(previous), current);

    printf("%s\n", current);

    if (enabled && wifi->network_count > 0U)
    {
        size_t index;

        for (index = 0U;
             index < wifi->network_count;
             index++)
        {
            const echoear_wifi_network_t *network =
                &wifi->networks[index];

            printf(
                "[WiFiNetwork] ssid=%s rssi=%d "
                "security=%s channel=%u hidden=%d\n",
                network->ssid,
                (int)network->rssi,
                echoear_wifi_manager_security_name(
                    network->security),
                (unsigned int)network->channel,
                network->hidden ? 1 : 0);
        }
    }

    fflush(stdout);
}

bool echoear_wifi_manager_mock_load(
    const char *path)
{
    FILE *file;
    char line[MOCK_LINE_MAX];

    bool enabled_auto = true;
    bool enabled = false;
    bool follow_portal = true;
    bool scan_requested = false;
    bool password_present = false;
    bool connect_requested = false;

    echoear_wifi_state_t state =
        ECHOEAR_WIFI_IDLE;

    echoear_wifi_error_t error =
        ECHOEAR_WIFI_ERROR_NONE;

    echoear_wifi_security_t selected_security =
        ECHOEAR_WIFI_SECURITY_UNKNOWN;

    mock_network_t networks[ECHOEAR_WIFI_MAX_NETWORKS];
    size_t network_count = 0U;

    char selected_ssid[ECHOEAR_WIFI_SSID_MAX] = "";
    char connected_ssid[ECHOEAR_WIFI_SSID_MAX] = "";
    char ip_address[ECHOEAR_WIFI_IPV4_MAX] = "";
    char gateway[ECHOEAR_WIFI_IPV4_MAX] = "";
    char netmask[ECHOEAR_WIFI_IPV4_MAX] = "";
    int16_t connected_rssi = 0;

    echoear_captive_portal_t *portal;

    if (path == NULL)
    {
        echoear_wifi_manager_reset();
        print_wifi_state_if_changed(false);
        return false;
    }

    file = fopen(path, "r");

    if (file == NULL)
    {
        echoear_wifi_manager_reset();
        echoear_wifi_manager_set_error(
            ECHOEAR_WIFI_ERROR_INTERNAL);
        sync_portal_and_provisioning(true);
        print_wifi_state_if_changed(true);
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *separator;
        char *key;
        char *value;

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

        if (strcmp(key, "wifi_enabled") == 0)
        {
            enabled_auto =
                strcmp(value, "auto") == 0;

            if (!enabled_auto)
            {
                enabled = parse_bool(value);
            }
        }
        else if (strcmp(
                     key,
                     "wifi_follow_portal") == 0)
        {
            follow_portal = parse_bool(value);
        }
        else if (strcmp(key, "wifi_state") == 0)
        {
            echoear_wifi_manager_parse_state(
                value,
                &state);
        }
        else if (strcmp(key, "wifi_error") == 0)
        {
            echoear_wifi_manager_parse_error(
                value,
                &error);
        }
        else if (strcmp(
                     key,
                     "wifi_scan_requested") == 0)
        {
            scan_requested = parse_bool(value);
        }
        else if (strcmp(key, "network") == 0)
        {
            if (network_count <
                    ECHOEAR_WIFI_MAX_NETWORKS &&
                parse_network(
                    value,
                    &networks[network_count]))
            {
                network_count++;
            }
        }
        else if (strcmp(
                     key,
                     "wifi_selected_ssid") == 0)
        {
            copy_text(
                selected_ssid,
                sizeof(selected_ssid),
                value);
        }
        else if (strcmp(
                     key,
                     "wifi_selected_security") == 0)
        {
            echoear_wifi_manager_parse_security(
                value,
                &selected_security);
        }
        else if (strcmp(
                     key,
                     "wifi_password_present") == 0)
        {
            password_present = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "wifi_connect_requested") == 0)
        {
            connect_requested = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "wifi_connected_ssid") == 0)
        {
            copy_text(
                connected_ssid,
                sizeof(connected_ssid),
                value);
        }
        else if (strcmp(key, "wifi_ip") == 0)
        {
            copy_text(
                ip_address,
                sizeof(ip_address),
                value);
        }
        else if (strcmp(key, "wifi_gateway") == 0)
        {
            copy_text(
                gateway,
                sizeof(gateway),
                value);
        }
        else if (strcmp(key, "wifi_netmask") == 0)
        {
            copy_text(
                netmask,
                sizeof(netmask),
                value);
        }
        else if (strcmp(
                     key,
                     "wifi_connected_rssi") == 0)
        {
            connected_rssi = (int16_t)parse_long(
                value,
                0L);
        }
    }

    fclose(file);

    portal = echoear_captive_portal_get();

    if (enabled_auto)
    {
        enabled = portal != NULL && portal->enabled;
    }

    if (!enabled)
    {
        echoear_wifi_manager_reset();
        print_wifi_state_if_changed(false);
        return true;
    }

    if (follow_portal && portal != NULL)
    {
        scan_requested =
            scan_requested || portal->scan_requested;

        if (portal->selected_ssid[0] != '\0')
        {
            copy_text(
                selected_ssid,
                sizeof(selected_ssid),
                portal->selected_ssid);

            selected_security =
                portal->selected_network_secured
                    ? ECHOEAR_WIFI_SECURITY_WPA2_PSK
                    : ECHOEAR_WIFI_SECURITY_OPEN;
        }

        password_present =
            password_present || portal->password_present;

        connect_requested =
            connect_requested || portal->connect_requested;
    }

    echoear_wifi_manager_reset();

    if (state == ECHOEAR_WIFI_SCANNING)
    {
        echoear_wifi_manager_begin_scan();
    }
    else
    {
        echoear_wifi_manager_clear_scan_results();
    }

    {
        size_t index;

        for (index = 0U;
             index < network_count;
             index++)
        {
            echoear_wifi_manager_add_network(
                networks[index].ssid,
                networks[index].bssid,
                networks[index].rssi,
                networks[index].channel,
                networks[index].security,
                networks[index].hidden);
        }
    }

    if (scan_requested ||
        state == ECHOEAR_WIFI_SCAN_REQUESTED)
    {
        echoear_wifi_manager_request_scan();
    }
    else if (state == ECHOEAR_WIFI_SCANNING)
    {
        echoear_wifi_manager_begin_scan();

        {
            size_t index;

            for (index = 0U;
                 index < network_count;
                 index++)
            {
                echoear_wifi_manager_add_network(
                    networks[index].ssid,
                    networks[index].bssid,
                    networks[index].rssi,
                    networks[index].channel,
                    networks[index].security,
                    networks[index].hidden);
            }
        }
    }
    else if (state == ECHOEAR_WIFI_SCAN_COMPLETE)
    {
        echoear_wifi_manager_finish_scan();
    }

    if (selected_ssid[0] != '\0')
    {
        if (selected_security ==
            ECHOEAR_WIFI_SECURITY_UNKNOWN)
        {
            const echoear_wifi_network_t *network =
                echoear_wifi_manager_find_network(
                    selected_ssid);

            if (network != NULL)
            {
                selected_security = network->security;
            }
        }

        if (connect_requested ||
            state == ECHOEAR_WIFI_CONNECT_REQUESTED ||
            state == ECHOEAR_WIFI_CONNECTING)
        {
            echoear_wifi_manager_request_connect(
                selected_ssid,
                selected_security,
                password_present
                    ? "mock-password"
                    : "");
        }
    }

    if (state == ECHOEAR_WIFI_CONNECTING)
    {
        echoear_wifi_manager_mark_connecting();
    }
    else if (state == ECHOEAR_WIFI_CONNECTED)
    {
        const char *ssid = connected_ssid[0] != '\0'
            ? connected_ssid
            : selected_ssid;

        echoear_wifi_manager_mark_connected(
            ssid,
            ip_address,
            gateway,
            netmask,
            connected_rssi);
    }
    else if (state == ECHOEAR_WIFI_DISCONNECTED)
    {
        echoear_wifi_manager_mark_disconnected();
    }

    if (error != ECHOEAR_WIFI_ERROR_NONE ||
        state == ECHOEAR_WIFI_ERROR)
    {
        if (error == ECHOEAR_WIFI_ERROR_NONE)
        {
            error = ECHOEAR_WIFI_ERROR_INTERNAL;
        }

        echoear_wifi_manager_set_error(error);
    }

    sync_portal_and_provisioning(true);
    print_wifi_state_if_changed(true);

    return true;
}
