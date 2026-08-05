#include "echoear_softap_mock.h"

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
map_softap_error(
    echoear_softap_error_t error)
{
    switch (error)
    {
    case ECHOEAR_SOFTAP_ERROR_INVALID_CONFIG:
    case ECHOEAR_SOFTAP_ERROR_START_FAILED:
        return ECHOEAR_PROVISIONING_ERROR_AP_START_FAILED;

    case ECHOEAR_SOFTAP_ERROR_DNS_FAILED:
    case ECHOEAR_SOFTAP_ERROR_HTTP_FAILED:
    case ECHOEAR_SOFTAP_ERROR_TIMEOUT:
    case ECHOEAR_SOFTAP_ERROR_UNKNOWN:
        return ECHOEAR_PROVISIONING_ERROR_UNKNOWN;

    case ECHOEAR_SOFTAP_ERROR_NONE:
    default:
        return ECHOEAR_PROVISIONING_ERROR_NONE;
    }
}

static void sync_provisioning_from_softap(void)
{
    echoear_softap_t *softap =
        echoear_softap_get();

    if (softap == NULL)
    {
        return;
    }

    if (!softap->requested)
    {
        echoear_pro_ui_apply_provisioning_state();
        return;
    }

    echoear_provisioning_set_enabled(true);

    echoear_provisioning_set_setup_completed(
        false);

    echoear_provisioning_set_ap_ssid(
        softap->ssid);

    echoear_provisioning_set_ip_address(
        softap->ip_address);

    echoear_provisioning_set_client_connected(
        softap->connected_clients > 0U);

    if (softap->error !=
        ECHOEAR_SOFTAP_ERROR_NONE ||
        softap->state ==
            ECHOEAR_SOFTAP_ERROR)
    {
        echoear_provisioning_set_error(
            map_softap_error(softap->error));

        echoear_pro_ui_apply_provisioning_state();
        return;
    }

    switch (softap->state)
    {
    case ECHOEAR_SOFTAP_STARTING:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_STARTING);
        break;

    case ECHOEAR_SOFTAP_ACTIVE:
    case ECHOEAR_SOFTAP_PORTAL_READY:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_READY);
        break;

    case ECHOEAR_SOFTAP_CLIENT_CONNECTED:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_CLIENT_CONNECTED);
        break;

    case ECHOEAR_SOFTAP_STOPPING:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_CHECKING);
        break;

    case ECHOEAR_SOFTAP_STOPPED:
    default:
        echoear_provisioning_set_state(
            ECHOEAR_PROVISIONING_AP_STARTING);
        break;
    }

    echoear_pro_ui_apply_provisioning_state();
}

static void print_softap_state(void)
{
    echoear_softap_t *softap =
        echoear_softap_get();

    if (softap == NULL)
    {
        return;
    }

    printf(
        "[SoftAP] requested=%d state=%s "
        "ssid=%s clients=%u dns=%d http=%d "
        "portal=%d error=%s\n",
        softap->requested ? 1 : 0,
        echoear_softap_state_name(
            softap->state),
        softap->ssid,
        (unsigned int)softap->connected_clients,
        softap->dns_redirect_ready ? 1 : 0,
        softap->http_server_ready ? 1 : 0,
        echoear_softap_is_portal_ready() ? 1 : 0,
        echoear_softap_error_name(
            softap->error));

    fflush(stdout);
}

bool echoear_softap_mock_load(
    const char *path)
{
    echoear_softap_t *current;
    FILE *file;
    char line[256];

    bool has_requested_override = false;
    bool requested = false;

    echoear_softap_state_t state;
    echoear_softap_error_t error;
    echoear_softap_auth_mode_t auth_mode;

    char ssid[ECHOEAR_SOFTAP_SSID_MAX];
    char password[ECHOEAR_SOFTAP_PASSWORD_MAX];

    char ip_address[ECHOEAR_SOFTAP_IPV4_MAX];
    char gateway[ECHOEAR_SOFTAP_IPV4_MAX];
    char netmask[ECHOEAR_SOFTAP_IPV4_MAX];
    char hostname[ECHOEAR_SOFTAP_HOSTNAME_MAX];

    uint8_t channel;
    uint8_t max_clients;
    uint8_t connected_clients;

    bool dns_ready;
    bool http_ready;
    bool configuration_valid;

    current = echoear_softap_get();

    if (current == NULL)
    {
        return false;
    }

    requested = current->requested;
    state = current->state;
    error = current->error;
    auth_mode = current->auth_mode;

    copy_text(ssid, sizeof(ssid), current->ssid);
    copy_text(
        password,
        sizeof(password),
        current->password);

    copy_text(
        ip_address,
        sizeof(ip_address),
        current->ip_address);

    copy_text(
        gateway,
        sizeof(gateway),
        current->gateway);

    copy_text(
        netmask,
        sizeof(netmask),
        current->netmask);

    copy_text(
        hostname,
        sizeof(hostname),
        current->hostname);

    channel = current->channel;
    max_clients = current->max_clients;
    connected_clients =
        current->connected_clients;

    dns_ready = current->dns_redirect_ready;
    http_ready = current->http_server_ready;

    if (path == NULL)
    {
        sync_provisioning_from_softap();
        print_softap_state();
        return false;
    }

    file = fopen(path, "r");

    if (file == NULL)
    {
        echoear_softap_set_error(
            ECHOEAR_SOFTAP_ERROR_UNKNOWN);

        sync_provisioning_from_softap();
        print_softap_state();
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

        if (strcmp(key, "softap_enabled") == 0)
        {
            if (strcmp(value, "auto") != 0)
            {
                has_requested_override = true;
                requested = parse_bool(value);
            }
        }
        else if (strcmp(key, "softap_state") == 0)
        {
            echoear_softap_parse_state(
                value,
                &state);
        }
        else if (strcmp(key, "softap_error") == 0)
        {
            echoear_softap_parse_error(
                value,
                &error);
        }
        else if (strcmp(key, "softap_auth") == 0)
        {
            echoear_softap_parse_auth_mode(
                value,
                &auth_mode);
        }
        else if (strcmp(key, "ap_ssid") == 0)
        {
            copy_text(ssid, sizeof(ssid), value);
        }
        else if (strcmp(key, "ap_password") == 0)
        {
            copy_text(
                password,
                sizeof(password),
                value);
        }
        else if (strcmp(key, "ap_ip") == 0)
        {
            copy_text(
                ip_address,
                sizeof(ip_address),
                value);
        }
        else if (strcmp(key, "ap_gateway") == 0)
        {
            copy_text(
                gateway,
                sizeof(gateway),
                value);
        }
        else if (strcmp(key, "ap_netmask") == 0)
        {
            copy_text(
                netmask,
                sizeof(netmask),
                value);
        }
        else if (strcmp(key, "ap_hostname") == 0)
        {
            copy_text(
                hostname,
                sizeof(hostname),
                value);
        }
        else if (strcmp(key, "ap_channel") == 0)
        {
            channel = (uint8_t)atoi(value);
        }
        else if (strcmp(key, "ap_max_clients") == 0)
        {
            max_clients =
                (uint8_t)atoi(value);
        }
        else if (strcmp(
                     key,
                     "ap_connected_clients") == 0)
        {
            connected_clients =
                (uint8_t)atoi(value);
        }
        else if (strcmp(
                     key,
                     "dns_redirect_ready") == 0)
        {
            dns_ready = parse_bool(value);
        }
        else if (strcmp(
                     key,
                     "http_server_ready") == 0)
        {
            http_ready = parse_bool(value);
        }
    }

    fclose(file);

    if (has_requested_override)
    {
        echoear_softap_set_requested(requested);
    }

    configuration_valid =
        echoear_softap_configure(
            ssid,
            password,
            auth_mode);

    echoear_softap_set_network(
        ip_address,
        gateway,
        netmask);

    echoear_softap_set_hostname(hostname);
    echoear_softap_set_channel(channel);
    echoear_softap_set_max_clients(max_clients);

    if (!current->requested)
    {
        echoear_softap_set_dns_redirect_ready(false);
        echoear_softap_set_http_server_ready(false);
        echoear_softap_set_connected_clients(0);

        echoear_softap_set_error(
            ECHOEAR_SOFTAP_ERROR_NONE);

        echoear_softap_set_state(
            ECHOEAR_SOFTAP_STOPPED);
    }
    else if (!configuration_valid)
    {
        echoear_softap_set_error(
            ECHOEAR_SOFTAP_ERROR_INVALID_CONFIG);
    }
    else
    {
        echoear_softap_set_state(state);

        if (error != ECHOEAR_SOFTAP_ERROR_NONE)
        {
            echoear_softap_set_error(error);
        }

        echoear_softap_set_dns_redirect_ready(
            dns_ready);

        echoear_softap_set_http_server_ready(
            http_ready);

        echoear_softap_set_connected_clients(
            connected_clients);
    }

    sync_provisioning_from_softap();
    print_softap_state();

    return true;
}