#include "echoear_tool_router_mock.h"

#include "echoear_tool_router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool enabled;
    bool network_ready;

    int request_trigger;
    char call_id[ECHOEAR_TOOL_CALL_ID_MAX];
    char request_id[ECHOEAR_TOOL_REQUEST_ID_MAX];
    char tool_name[ECHOEAR_TOOL_NAME_MAX];
    char arguments[ECHOEAR_TOOL_ARGUMENTS_MAX];

    char confirmation_result[24];
    char execution_result[24];
    char execution_output[ECHOEAR_TOOL_RESULT_MAX];

    int cancel_trigger;
    int clear_trigger;
} echoear_tool_router_mock_config_t;

static int s_last_request_trigger;
static int s_last_cancel_trigger;
static int s_last_clear_trigger;
static bool s_last_enabled = true;
static bool s_stale_attempted;

static void copy_value(char *destination,
                       size_t destination_size,
                       const char *source)
{
    if (destination == NULL || destination_size == 0U) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    snprintf(destination, destination_size, "%s", source);
}

static void trim_line(char *text)
{
    size_t length;

    if (text == NULL) {
        return;
    }

    length = strlen(text);

    while (length > 0U) {
        char c = text[length - 1U];

        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
            break;
        }

        text[length - 1U] = '\0';
        length--;
    }
}

static void config_defaults(echoear_tool_router_mock_config_t *config)
{
    memset(config, 0, sizeof(*config));

    config->enabled = true;
    config->network_ready = true;

    copy_value(config->call_id,
               sizeof(config->call_id),
               "call-001");

    copy_value(config->request_id,
               sizeof(config->request_id),
               "req-001");

    copy_value(config->tool_name,
               sizeof(config->tool_name),
               "obd.read_speed");

    copy_value(config->arguments,
               sizeof(config->arguments),
               "{}");

    copy_value(config->confirmation_result,
               sizeof(config->confirmation_result),
               "none");

    copy_value(config->execution_result,
               sizeof(config->execution_result),
               "success");

    copy_value(config->execution_output,
               sizeof(config->execution_output),
               "{\"speed_kph\":88}");
}

static void apply_key_value(echoear_tool_router_mock_config_t *config,
                            const char *key,
                            const char *value)
{
    if (strcmp(key, "tool_router_enabled") == 0) {
        config->enabled = atoi(value) != 0;
    } else if (strcmp(key, "tool_router_network_ready") == 0) {
        config->network_ready = atoi(value) != 0;
    } else if (strcmp(key, "tool_router_request_trigger") == 0) {
        config->request_trigger = atoi(value);
    } else if (strcmp(key, "tool_router_call_id") == 0) {
        copy_value(config->call_id, sizeof(config->call_id), value);
    } else if (strcmp(key, "tool_router_request_id") == 0) {
        copy_value(config->request_id, sizeof(config->request_id), value);
    } else if (strcmp(key, "tool_router_tool_name") == 0) {
        copy_value(config->tool_name, sizeof(config->tool_name), value);
    } else if (strcmp(key, "tool_router_arguments") == 0) {
        copy_value(config->arguments, sizeof(config->arguments), value);
    } else if (strcmp(key, "tool_router_confirmation_result") == 0) {
        copy_value(config->confirmation_result,
                   sizeof(config->confirmation_result),
                   value);
    } else if (strcmp(key, "tool_router_execution_result") == 0) {
        copy_value(config->execution_result,
                   sizeof(config->execution_result),
                   value);
    } else if (strcmp(key, "tool_router_execution_output") == 0) {
        copy_value(config->execution_output,
                   sizeof(config->execution_output),
                   value);
    } else if (strcmp(key, "tool_router_cancel_trigger") == 0) {
        config->cancel_trigger = atoi(value);
    } else if (strcmp(key, "tool_router_clear_trigger") == 0) {
        config->clear_trigger = atoi(value);
    }
}

static bool load_config(const char *path,
                        echoear_tool_router_mock_config_t *config)
{
    FILE *file;
    char line[1024];

    if (path == NULL || config == NULL) {
        return false;
    }

    config_defaults(config);

    file = fopen(path, "rb");

    if (file == NULL) {
        printf("[ToolRouterMock] config_open_failed path=%s\n", path);
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *equals;
        char *key;
        char *value;

        trim_line(line);

        key = line;

        while (*key == ' ' || *key == '\t') {
            key++;
        }

        if (*key == '\0' || *key == '#') {
            continue;
        }

        equals = strchr(key, '=');

        if (equals == NULL) {
            continue;
        }

        *equals = '\0';
        value = equals + 1;

        trim_line(key);

        while (*value == ' ' || *value == '\t') {
            value++;
        }

        apply_key_value(config, key, value);
    }

    fclose(file);
    return true;
}

static void register_tool(const char *name,
                          echoear_tool_risk_t risk,
                          echoear_tool_policy_t policy,
                          bool enabled,
                          bool requires_network)
{
    echoear_tool_descriptor_t tool;

    memset(&tool, 0, sizeof(tool));
    snprintf(tool.name, sizeof(tool.name), "%s", name);

    tool.risk = risk;
    tool.policy = policy;
    tool.enabled = enabled;
    tool.requires_network = requires_network;

    printf(
        "[ToolRouterRegistry] tool=%s risk=%s policy=%s enabled=%d network=%d registered=%d\n",
        tool.name,
        echoear_tool_risk_name(tool.risk),
        echoear_tool_policy_name(tool.policy),
        tool.enabled ? 1 : 0,
        tool.requires_network ? 1 : 0,
        echoear_tool_router_register_tool(&tool) ? 1 : 0
    );
}

static void print_snapshot(void)
{
    echoear_tool_router_t *router = echoear_tool_router_get();

    printf(
        "[ToolRouter] state=%s error=%s enabled=%d network=%d "
        "request_active=%d confirmation=%d dispatch_ready=%d executing=%d "
        "call_id=%s request_id=%s tool=%s risk=%s policy=%s "
        "tools=%zu requests=%u dispatches=%u success=%u failures=%u "
        "denied=%u cancels=%u result_len=%zu generation=%u\n",
        echoear_tool_router_state_name(router->state),
        echoear_tool_router_error_name(router->error),
        router->config.enabled ? 1 : 0,
        router->network_ready ? 1 : 0,
        router->request_active ? 1 : 0,
        router->confirmation_pending ? 1 : 0,
        router->dispatch_ready ? 1 : 0,
        router->executing ? 1 : 0,
        router->request.call_id[0] != '\0' ? router->request.call_id : "<none>",
        router->request.request_id[0] != '\0' ? router->request.request_id : "<none>",
        router->request.tool_name[0] != '\0' ? router->request.tool_name : "<none>",
        router->active_tool.name[0] != '\0'
            ? echoear_tool_risk_name(router->active_tool.risk)
            : "<none>",
        router->active_tool.name[0] != '\0'
            ? echoear_tool_policy_name(router->active_tool.policy)
            : "<none>",
        router->tool_count,
        (unsigned)router->request_count,
        (unsigned)router->dispatch_count,
        (unsigned)router->success_count,
        (unsigned)router->failure_count,
        (unsigned)router->denied_count,
        (unsigned)router->cancel_count,
        router->result_length,
        (unsigned)router->generation
    );
}

void echoear_tool_router_mock_init(void)
{
    echoear_tool_router_config_t config;

    echoear_tool_router_init();

    config = echoear_tool_router_default_config();
    config.enabled = true;

    echoear_tool_router_configure(&config);
    echoear_tool_router_set_network_ready(true);

    register_tool(
        "obd.read_speed",
        ECHOEAR_TOOL_RISK_READ_ONLY,
        ECHOEAR_TOOL_POLICY_AUTO_ALLOW,
        true,
        false
    );

    register_tool(
        "device.set_volume",
        ECHOEAR_TOOL_RISK_CONTROL,
        ECHOEAR_TOOL_POLICY_REQUIRE_CONFIRMATION,
        true,
        false
    );

    register_tool(
        "search.web",
        ECHOEAR_TOOL_RISK_READ_ONLY,
        ECHOEAR_TOOL_POLICY_AUTO_ALLOW,
        true,
        true
    );

    register_tool(
        "device.factory_reset",
        ECHOEAR_TOOL_RISK_SENSITIVE,
        ECHOEAR_TOOL_POLICY_DENY,
        true,
        false
    );

    s_last_request_trigger = 0;
    s_last_cancel_trigger = 0;
    s_last_clear_trigger = 0;
    s_last_enabled = true;
    s_stale_attempted = false;

    print_snapshot();
}

void echoear_tool_router_mock_load(const char *path)
{
    echoear_tool_router_mock_config_t config;
    echoear_tool_router_t *router;

    if (!load_config(path, &config)) {
        return;
    }

    router = echoear_tool_router_get();

    if (config.enabled != s_last_enabled &&
        !echoear_tool_router_is_busy()) {
        echoear_tool_router_config_t router_config =
            echoear_tool_router_default_config();

        router_config.enabled = config.enabled;

        printf(
            "[ToolRouterConfig] enabled=%d applied=%d\n",
            config.enabled ? 1 : 0,
            echoear_tool_router_configure(&router_config) ? 1 : 0
        );

        s_last_enabled = config.enabled;
    }

    echoear_tool_router_set_network_ready(config.network_ready);

    if (config.clear_trigger != 0 &&
        s_last_clear_trigger == 0) {
        echoear_tool_router_clear_completed();
        printf("[ToolRouterClear] triggered=1\n");
        s_stale_attempted = false;
    }

    if (config.cancel_trigger != 0 &&
        s_last_cancel_trigger == 0) {
        echoear_tool_router_cancel();
        printf("[ToolRouterCancel] triggered=1\n");
        s_stale_attempted = false;
    }

    if (config.request_trigger != 0 &&
        s_last_request_trigger == 0) {
        echoear_tool_request_t request;
        bool accepted;

        memset(&request, 0, sizeof(request));

        snprintf(request.call_id, sizeof(request.call_id), "%s", config.call_id);
        snprintf(request.request_id, sizeof(request.request_id), "%s", config.request_id);
        snprintf(request.tool_name, sizeof(request.tool_name), "%s", config.tool_name);
        snprintf(request.arguments, sizeof(request.arguments), "%s", config.arguments);

        accepted = echoear_tool_router_submit(&request);

        printf(
            "[ToolRouterRequest] call_id=%s request_id=%s tool=%s arguments=%s accepted=%d\n",
            request.call_id,
            request.request_id,
            request.tool_name,
            request.arguments,
            accepted ? 1 : 0
        );

        s_stale_attempted = false;
    }

    router = echoear_tool_router_get();

    if (router->state ==
            ECHOEAR_TOOL_ROUTER_STATE_AWAITING_CONFIRMATION) {
        if (strcmp(config.confirmation_result, "approve") == 0) {
            bool accepted =
                echoear_tool_router_authorize(router->request.call_id, true);

            printf(
                "[ToolRouterConfirmation] call_id=%s result=approve accepted=%d\n",
                router->request.call_id,
                accepted ? 1 : 0
            );
        } else if (strcmp(config.confirmation_result, "deny") == 0) {
            bool accepted =
                echoear_tool_router_authorize(router->request.call_id, false);

            printf(
                "[ToolRouterConfirmation] call_id=%s result=deny accepted=%d\n",
                router->request.call_id,
                accepted ? 1 : 0
            );
        }
    }

    router = echoear_tool_router_get();

    if (router->state ==
            ECHOEAR_TOOL_ROUTER_STATE_DISPATCH_READY) {
        echoear_tool_dispatch_t dispatch;

        if (echoear_tool_router_take_dispatch(&dispatch)) {
            printf(
                "[ToolRouterDispatch] call_id=%s request_id=%s tool=%s risk=%s arguments=%s\n",
                dispatch.call_id,
                dispatch.request_id,
                dispatch.tool_name,
                echoear_tool_risk_name(dispatch.risk),
                dispatch.arguments
            );

            s_stale_attempted = false;
        }
    }

    router = echoear_tool_router_get();

    if (router->state ==
            ECHOEAR_TOOL_ROUTER_STATE_EXECUTING) {
        if (strcmp(config.execution_result, "success") == 0) {
            char call_id[ECHOEAR_TOOL_CALL_ID_MAX];
            bool accepted;

            snprintf(call_id, sizeof(call_id), "%s", router->request.call_id);

            accepted =
                echoear_tool_router_complete(call_id, config.execution_output);

            printf(
                "[ToolRouterExecution] call_id=%s result=success accepted=%d output=%s\n",
                call_id,
                accepted ? 1 : 0,
                config.execution_output
            );
        } else if (strcmp(config.execution_result, "failure") == 0) {
            char call_id[ECHOEAR_TOOL_CALL_ID_MAX];
            bool accepted;

            snprintf(call_id, sizeof(call_id), "%s", router->request.call_id);

            accepted =
                echoear_tool_router_fail(
                    call_id,
                    ECHOEAR_TOOL_ROUTER_ERROR_EXECUTION_FAILED
                );

            printf(
                "[ToolRouterExecution] call_id=%s result=failure accepted=%d\n",
                call_id,
                accepted ? 1 : 0
            );
        } else if (strcmp(config.execution_result, "stale") == 0) {
            if (!s_stale_attempted) {
                bool accepted =
                    echoear_tool_router_complete(
                        "stale-call-id",
                        "{\"stale\":true}"
                    );

                printf(
                    "[ToolRouterExecution] call_id=stale-call-id result=stale accepted=%d\n",
                    accepted ? 1 : 0
                );

                s_stale_attempted = true;
            }
        } else if (strcmp(config.execution_result, "pending") == 0) {
            /* Deliberately remain in EXECUTING. */
        }
    }

    s_last_request_trigger = config.request_trigger;
    s_last_cancel_trigger = config.cancel_trigger;
    s_last_clear_trigger = config.clear_trigger;

    print_snapshot();
}
