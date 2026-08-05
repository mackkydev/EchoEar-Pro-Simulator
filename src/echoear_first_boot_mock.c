#include "echoear_first_boot_mock.h"

#include "echoear_first_boot.h"

#include <ctype.h>
#include <stdio.h>
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

static void print_boot_decision(void)
{
    echoear_first_boot_t *state =
        echoear_first_boot_get();

    if (state == NULL)
    {
        return;
    }

    printf(
        "[FirstBoot] mode=%s reason=%s "
        "setup=%d credentials=%d force=%d\n",
        echoear_first_boot_mode_name(state->mode),
        echoear_first_boot_reason_name(state->reason),
        state->setup_completed ? 1 : 0,
        state->wifi_credentials_saved ? 1 : 0,
        state->force_provisioning ? 1 : 0);

    fflush(stdout);
}

bool echoear_first_boot_mock_load(
    const char *path)
{
    FILE *file;
    char line[256];

    if (path == NULL)
    {
        echoear_first_boot_apply();
        print_boot_decision();
        return false;
    }

    file = fopen(path, "r");

    if (file == NULL)
    {
        /*
         * Missing storage data is treated as first boot.
         */
        echoear_first_boot_apply();
        print_boot_decision();
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

        if (strcmp(key, "setup_completed") == 0)
        {
            echoear_first_boot_set_setup_completed(
                parse_bool(value));
        }
        else if (strcmp(
                     key,
                     "wifi_credentials_saved") == 0)
        {
            echoear_first_boot_set_wifi_credentials_saved(
                parse_bool(value));
        }
        else if (strcmp(
                     key,
                     "force_provisioning") == 0)
        {
            echoear_first_boot_set_force_provisioning(
                parse_bool(value));
        }
    }

    fclose(file);

    echoear_first_boot_apply();
    print_boot_decision();

    return true;
}