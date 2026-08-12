#include "echoear_search_bridge_mock.h"

#include <stdint.h>
#include <stdio.h>

#include "echoear_search_bridge.h"

static uint32_t s_last_generation;

static void print_snapshot(void)
{
    echoear_search_bridge_t *bridge =
        echoear_search_bridge_get();

    printf(
        "[SearchBridge] state=%s active=%d "
        "call_id=%s search_request_id=%s query=%s "
        "routed=%u completed=%u failed=%u generation=%u\n",
        echoear_search_bridge_state_name(
            bridge->state
        ),
        bridge->active ? 1 : 0,
        bridge->call_id[0] != '\0'
            ? bridge->call_id
            : "<none>",
        bridge->search_request_id[0] != '\0'
            ? bridge->search_request_id
            : "<none>",
        bridge->query[0] != '\0'
            ? bridge->query
            : "<none>",
        (unsigned)bridge->routed_count,
        (unsigned)bridge->completed_count,
        (unsigned)bridge->failed_count,
        (unsigned)bridge->generation
    );
}

void echoear_search_bridge_mock_init(void)
{
    echoear_search_bridge_init();

    s_last_generation = UINT32_MAX;

    print_snapshot();

    s_last_generation =
        echoear_search_bridge_get()->generation;
}

void echoear_search_bridge_mock_step(void)
{
    echoear_search_bridge_t *bridge;

    echoear_search_bridge_process();

    bridge = echoear_search_bridge_get();

    if (bridge->generation ==
        s_last_generation) {
        return;
    }

    print_snapshot();

    s_last_generation =
        bridge->generation;
}