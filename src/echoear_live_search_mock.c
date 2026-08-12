#include "echoear_live_search_mock.h"

#include "echoear_live_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool enabled;
    bool network_ready;
    bool gateway_ready;
    bool auto_tick;
    uint32_t now_ms;
    size_t max_results;

    int request_trigger;
    char request_id[ECHOEAR_SEARCH_REQUEST_ID_MAX];
    char type[24];
    char query[ECHOEAR_SEARCH_QUERY_MAX];

    char dispatch_result[24];
    char response_result[24];

    char result1_title[ECHOEAR_SEARCH_TITLE_MAX];
    char result1_url[ECHOEAR_SEARCH_URL_MAX];
    char result1_snippet[ECHOEAR_SEARCH_SNIPPET_MAX];
    char result1_source[ECHOEAR_SEARCH_SOURCE_MAX];

    char result2_title[ECHOEAR_SEARCH_TITLE_MAX];
    char result2_url[ECHOEAR_SEARCH_URL_MAX];
    char result2_snippet[ECHOEAR_SEARCH_SNIPPET_MAX];
    char result2_source[ECHOEAR_SEARCH_SOURCE_MAX];

    int stale_result_trigger;
    int clear_trigger;
    int cancel_trigger;
} echoear_live_search_mock_config_t;

static int s_last_request_trigger;
static int s_last_stale_result_trigger;
static int s_last_clear_trigger;
static int s_last_cancel_trigger;
static bool s_last_enabled = true;
static size_t s_last_max_results = 4U;
static uint32_t s_auto_now_ms;

static void copy_value(char *destination, size_t destination_size, const char *source)
{
    if (destination == NULL || destination_size == 0U) return;
    if (source == NULL) { destination[0] = '\0'; return; }
    snprintf(destination, destination_size, "%s", source);
}

static void trim_line(char *text)
{
    size_t length;
    if (text == NULL) return;
    length = strlen(text);
    while (length > 0U) {
        char c = text[length - 1U];
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') break;
        text[length - 1U] = '\0';
        length--;
    }
}

static echoear_search_type_t parse_type(const char *value)
{
    if (value != NULL && strcmp(value, "news") == 0) return ECHOEAR_SEARCH_TYPE_NEWS;
    if (value != NULL && strcmp(value, "live_info") == 0) return ECHOEAR_SEARCH_TYPE_LIVE_INFO;
    return ECHOEAR_SEARCH_TYPE_WEB;
}

static void config_defaults(echoear_live_search_mock_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->enabled = true;
    config->network_ready = true;
    config->gateway_ready = true;
    config->auto_tick = true;
    config->max_results = 4U;
    copy_value(config->request_id, sizeof(config->request_id), "search-001");
    copy_value(config->type, sizeof(config->type), "web");
    copy_value(config->query, sizeof(config->query), "EchoEar Pro");
    copy_value(config->dispatch_result, sizeof(config->dispatch_result), "success");
    copy_value(config->response_result, sizeof(config->response_result), "success");
    copy_value(config->result1_title, sizeof(config->result1_title), "EchoEar Pro Search Result");
    copy_value(config->result1_url, sizeof(config->result1_url), "https://example.local/echoear");
    copy_value(config->result1_snippet, sizeof(config->result1_snippet), "Deterministic simulator result for EchoEar Pro.");
    copy_value(config->result1_source, sizeof(config->result1_source), "simulator");
    copy_value(config->result2_title, sizeof(config->result2_title), "EchoEar Pro Documentation");
    copy_value(config->result2_url, sizeof(config->result2_url), "https://example.local/docs");
    copy_value(config->result2_snippet, sizeof(config->result2_snippet), "Second deterministic simulator result.");
    copy_value(config->result2_source, sizeof(config->result2_source), "simulator");
}

static void apply_key_value(echoear_live_search_mock_config_t *config, const char *key, const char *value)
{
    if (strcmp(key, "live_search_enabled") == 0) config->enabled = atoi(value) != 0;
    else if (strcmp(key, "live_search_network_ready") == 0) config->network_ready = atoi(value) != 0;
    else if (strcmp(key, "live_search_gateway_ready") == 0) config->gateway_ready = atoi(value) != 0;
    else if (strcmp(key, "live_search_auto_tick") == 0) config->auto_tick = atoi(value) != 0;
    else if (strcmp(key, "live_search_now_ms") == 0) config->now_ms = (uint32_t)strtoul(value, NULL, 10);
    else if (strcmp(key, "live_search_max_results") == 0) config->max_results = (size_t)strtoul(value, NULL, 10);
    else if (strcmp(key, "live_search_request_trigger") == 0) config->request_trigger = atoi(value);
    else if (strcmp(key, "live_search_request_id") == 0) copy_value(config->request_id, sizeof(config->request_id), value);
    else if (strcmp(key, "live_search_type") == 0) copy_value(config->type, sizeof(config->type), value);
    else if (strcmp(key, "live_search_query") == 0) copy_value(config->query, sizeof(config->query), value);
    else if (strcmp(key, "live_search_dispatch_result") == 0) copy_value(config->dispatch_result, sizeof(config->dispatch_result), value);
    else if (strcmp(key, "live_search_response_result") == 0) copy_value(config->response_result, sizeof(config->response_result), value);
    else if (strcmp(key, "live_search_result1_title") == 0) copy_value(config->result1_title, sizeof(config->result1_title), value);
    else if (strcmp(key, "live_search_result1_url") == 0) copy_value(config->result1_url, sizeof(config->result1_url), value);
    else if (strcmp(key, "live_search_result1_snippet") == 0) copy_value(config->result1_snippet, sizeof(config->result1_snippet), value);
    else if (strcmp(key, "live_search_result1_source") == 0) copy_value(config->result1_source, sizeof(config->result1_source), value);
    else if (strcmp(key, "live_search_result2_title") == 0) copy_value(config->result2_title, sizeof(config->result2_title), value);
    else if (strcmp(key, "live_search_result2_url") == 0) copy_value(config->result2_url, sizeof(config->result2_url), value);
    else if (strcmp(key, "live_search_result2_snippet") == 0) copy_value(config->result2_snippet, sizeof(config->result2_snippet), value);
    else if (strcmp(key, "live_search_result2_source") == 0) copy_value(config->result2_source, sizeof(config->result2_source), value);
    else if (strcmp(key, "live_search_stale_result_trigger") == 0) config->stale_result_trigger = atoi(value);
    else if (strcmp(key, "live_search_clear_trigger") == 0) config->clear_trigger = atoi(value);
    else if (strcmp(key, "live_search_cancel_trigger") == 0) config->cancel_trigger = atoi(value);
}

static bool load_config(const char *path, echoear_live_search_mock_config_t *config)
{
    FILE *file;
    char line[1200];
    if (path == NULL || config == NULL) return false;
    config_defaults(config);
    file = fopen(path, "rb");
    if (file == NULL) {
        printf("[LiveSearchMock] config_open_failed path=%s\n", path);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *equals, *key, *value;
        trim_line(line);
        key = line;
        while (*key == ' ' || *key == '\t') key++;
        if (*key == '\0' || *key == '#') continue;
        equals = strchr(key, '=');
        if (equals == NULL) continue;
        *equals = '\0';
        value = equals + 1;
        trim_line(key);
        while (*value == ' ' || *value == '\t') value++;
        apply_key_value(config, key, value);
    }
    fclose(file);
    return true;
}

static void fill_result(echoear_search_result_t *result, const char *title, const char *url, const char *snippet, const char *source)
{
    memset(result, 0, sizeof(*result));
    snprintf(result->title, sizeof(result->title), "%s", title);
    snprintf(result->url, sizeof(result->url), "%s", url);
    snprintf(result->snippet, sizeof(result->snippet), "%s", snippet);
    snprintf(result->source, sizeof(result->source), "%s", source);
}

static void print_results(void)
{
    echoear_live_search_t *search = echoear_live_search_get();
    size_t index;
    for (index = 0U; index < search->result_count; index++) {
        const echoear_search_result_t *result = &search->results[index];
        printf("[LiveSearchResult] index=%zu title=%s source=%s url=%s snippet=%s\n",
               index, result->title, result->source, result->url, result->snippet);
    }
}

static void print_snapshot(void)
{
    echoear_live_search_t *search = echoear_live_search_get();
    printf("[LiveSearch] state=%s error=%s enabled=%d network=%d gateway=%d request_active=%d request_id=%s type=%s query=%s results=%zu requests=%u success=%u failures=%u cancels=%u now=%u deadline=%u generation=%u\n",
           echoear_live_search_state_name(search->state),
           echoear_live_search_error_name(search->error),
           search->config.enabled ? 1 : 0,
           search->network_ready ? 1 : 0,
           search->gateway_ready ? 1 : 0,
           search->request_active ? 1 : 0,
           search->request_id[0] != '\0' ? search->request_id : "<none>",
           search->request_id[0] != '\0' ? echoear_live_search_type_name(search->type) : "<none>",
           search->query[0] != '\0' ? search->query : "<none>",
           search->result_count,
           (unsigned)search->request_count,
           (unsigned)search->success_count,
           (unsigned)search->failure_count,
           (unsigned)search->cancel_count,
           (unsigned)search->now_ms,
           (unsigned)search->deadline_ms,
           (unsigned)search->generation);
}

void echoear_live_search_mock_init(void)
{
    echoear_search_config_t config;
    echoear_live_search_init();
    config = echoear_live_search_default_config();
    config.enabled = true;
    echoear_live_search_configure(&config);
    echoear_live_search_set_network_ready(true);
    echoear_live_search_set_gateway_ready(true);
    s_last_request_trigger = 0;
    s_last_stale_result_trigger = 0;
    s_last_clear_trigger = 0;
    s_last_cancel_trigger = 0;
    s_last_enabled = true;
    s_last_max_results = 4U;
    s_auto_now_ms = 0U;
    print_snapshot();
}

void echoear_live_search_mock_load(const char *path)
{
    echoear_live_search_mock_config_t config;
    echoear_live_search_t *search;
    if (!load_config(path, &config)) return;

    search = echoear_live_search_get();
    if ((config.enabled != s_last_enabled ||
         config.max_results != s_last_max_results) &&
        !echoear_live_search_is_busy()) {
        bool applied;
        echoear_search_config_t search_config =
            echoear_live_search_default_config();

        search_config.enabled = config.enabled;
        search_config.max_results = config.max_results;

        applied = echoear_live_search_configure(
            &search_config
        );

        printf(
            "[LiveSearchConfig] enabled=%d max_results=%zu applied=%d\n",
            config.enabled ? 1 : 0,
            config.max_results,
            applied ? 1 : 0
        );

        if (applied) {
            s_last_enabled = config.enabled;
            s_last_max_results = config.max_results;
        }
    }
    echoear_live_search_set_network_ready(config.network_ready);
    echoear_live_search_set_gateway_ready(config.gateway_ready);

    if (config.auto_tick) {
        s_auto_now_ms += 1000U;
        echoear_live_search_tick(s_auto_now_ms);
    } else {
        echoear_live_search_tick(config.now_ms);
    }

    if (config.clear_trigger != 0 && s_last_clear_trigger == 0) {
        echoear_live_search_clear_completed();
        printf("[LiveSearchClear] triggered=1\n");
    }

    if (config.cancel_trigger != 0 && s_last_cancel_trigger == 0) {
        echoear_live_search_cancel();
        printf("[LiveSearchCancel] triggered=1\n");
    }

    if (config.request_trigger != 0 && s_last_request_trigger == 0) {
        bool accepted = echoear_live_search_submit(config.request_id, parse_type(config.type), config.query);
        printf("[LiveSearchRequest] request_id=%s type=%s query=%s accepted=%d\n",
               config.request_id, config.type, config.query, accepted ? 1 : 0);
    }

    search = echoear_live_search_get();
    if (search->state == ECHOEAR_SEARCH_STATE_REQUEST_PENDING && strcmp(config.dispatch_result, "success") == 0) {
        echoear_search_request_t request;
        bool got_request = echoear_live_search_get_pending_request(&request);
        bool dispatched = false;
        if (got_request) dispatched = echoear_live_search_mark_dispatched(request.request_id);
        printf("[LiveSearchDispatch] request_id=%s got_request=%d dispatched=%d\n",
               got_request ? request.request_id : "<none>", got_request ? 1 : 0, dispatched ? 1 : 0);
    }

    search = echoear_live_search_get();
    if (config.stale_result_trigger != 0 && s_last_stale_result_trigger == 0 &&
        search->state == ECHOEAR_SEARCH_STATE_WAITING_RESPONSE) {
        echoear_search_result_t stale_result;
        bool accepted;
        fill_result(&stale_result,
                    "Stale Search Result",
                    "https://stale.invalid/result",
                    "This stale result must be ignored.",
                    "stale");
        accepted = echoear_live_search_add_result("stale-search-id", &stale_result);
        printf("[LiveSearchStaleResult] request_id=stale-search-id accepted=%d active_request=%s\n",
               accepted ? 1 : 0, search->request_id);
    }

    search = echoear_live_search_get();
    if (search->state == ECHOEAR_SEARCH_STATE_WAITING_RESPONSE) {
        if (strcmp(config.response_result, "success") == 0) {
            echoear_search_result_t result;
            char request_id[ECHOEAR_SEARCH_REQUEST_ID_MAX];
            bool accepted1, accepted2, completed;
            snprintf(request_id, sizeof(request_id), "%s", search->request_id);

            fill_result(&result, config.result1_title, config.result1_url,
                        config.result1_snippet, config.result1_source);
            accepted1 = echoear_live_search_add_result(request_id, &result);

            fill_result(&result, config.result2_title, config.result2_url,
                        config.result2_snippet, config.result2_source);
            accepted2 = echoear_live_search_add_result(request_id, &result);

            completed = echoear_live_search_complete(request_id);
            printf("[LiveSearchResponse] request_id=%s result=success add1=%d add2=%d complete=%d\n",
                   request_id, accepted1 ? 1 : 0, accepted2 ? 1 : 0, completed ? 1 : 0);
            print_results();
        } else if (strcmp(config.response_result, "gateway_error") == 0) {
            char request_id[ECHOEAR_SEARCH_REQUEST_ID_MAX];
            bool failed;
            snprintf(request_id, sizeof(request_id), "%s", search->request_id);
            failed = echoear_live_search_fail(request_id, ECHOEAR_SEARCH_ERROR_GATEWAY_ERROR);
            printf("[LiveSearchResponse] request_id=%s result=gateway_error accepted=%d\n",
                   request_id, failed ? 1 : 0);
        } else if (strcmp(config.response_result, "pending") == 0) {
            /* Deliberately wait for timeout or later response. */
        }
    }

    s_last_request_trigger = config.request_trigger;
    s_last_stale_result_trigger = config.stale_result_trigger;
    s_last_clear_trigger = config.clear_trigger;
    s_last_cancel_trigger = config.cancel_trigger;
    print_snapshot();
}
