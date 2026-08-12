#include "echoear_conversation_mock.h"

#include "echoear_ai_gateway.h"
#include "echoear_conversation.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOCK_LINE_MAX 1024
#define MOCK_VALUE_MAX 768

typedef enum {
    MOCK_ACTION_SUCCESS = 0,
    MOCK_ACTION_PENDING,
    MOCK_ACTION_GATEWAY_ERROR,
    MOCK_ACTION_STALE
} mock_action_t;

typedef struct {
    bool enabled;
    bool auto_start_session;
    bool auto_tick;

    uint32_t now_ms;
    uint32_t request_timeout_ms;
    uint32_t response_timeout_ms;
    uint32_t response_chunk_size;

    char locale[ECHOEAR_CONVERSATION_LOCALE_MAX];
    char conversation_id[ECHOEAR_CONVERSATION_ID_MAX];
    char request_id[ECHOEAR_CONVERSATION_REQUEST_ID_MAX];
    char user_text[ECHOEAR_CONVERSATION_TEXT_MAX];
    char response_text[ECHOEAR_CONVERSATION_TEXT_MAX];

    mock_action_t dispatch_result;
    mock_action_t response_result;

    int request_trigger;
    int cancel_trigger;
    int clear_trigger;
    int end_session_trigger;
} conversation_mock_config_t;

static conversation_mock_config_t s_mock;
static bool s_loaded_once;
static bool s_last_gateway_ready;
static int s_last_request_trigger;
static int s_last_cancel_trigger;
static int s_last_clear_trigger;
static int s_last_end_session_trigger;
static uint32_t s_auto_now_ms;

static void trim(char *text)
{
    char *start;
    char *end;

    if (text == NULL) return;

    start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) start++;

    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }

    if (text[0] == '\0') return;

    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

static bool parse_bool(const char *value, bool fallback)
{
    if (value == NULL) return fallback;
    if (strcmp(value, "1") == 0 ||
        strcmp(value, "true") == 0 ||
        strcmp(value, "yes") == 0) return true;
    if (strcmp(value, "0") == 0 ||
        strcmp(value, "false") == 0 ||
        strcmp(value, "no") == 0) return false;
    return fallback;
}

static uint32_t parse_u32(const char *value, uint32_t fallback)
{
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0') return fallback;

    parsed = strtoul(value, &end, 10);
    if (end == value || (end != NULL && *end != '\0')) return fallback;

    return (uint32_t)parsed;
}

static int parse_int(const char *value, int fallback)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || value[0] == '\0') return fallback;

    parsed = strtol(value, &end, 10);
    if (end == value || (end != NULL && *end != '\0')) return fallback;

    return (int)parsed;
}

static mock_action_t parse_action(const char *value, mock_action_t fallback)
{
    if (value == NULL) return fallback;
    if (strcmp(value, "success") == 0) return MOCK_ACTION_SUCCESS;
    if (strcmp(value, "pending") == 0) return MOCK_ACTION_PENDING;
    if (strcmp(value, "gateway_error") == 0) return MOCK_ACTION_GATEWAY_ERROR;
    if (strcmp(value, "stale") == 0) return MOCK_ACTION_STALE;
    return fallback;
}

static const char *action_name(mock_action_t action)
{
    switch (action) {
        case MOCK_ACTION_SUCCESS: return "success";
        case MOCK_ACTION_PENDING: return "pending";
        case MOCK_ACTION_GATEWAY_ERROR: return "gateway_error";
        case MOCK_ACTION_STALE: return "stale";
        default: return "unknown";
    }
}

static void copy_value(char *dst, size_t dst_size, const char *value)
{
    if (dst == NULL || dst_size == 0U || value == NULL) return;
    snprintf(dst, dst_size, "%s", value);
}

static conversation_mock_config_t default_mock(void)
{
    conversation_mock_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = true;
    cfg.auto_start_session = true;
    cfg.auto_tick = true;
    cfg.request_timeout_ms = 15000U;
    cfg.response_timeout_ms = 60000U;
    cfg.response_chunk_size = 16U;
    cfg.dispatch_result = MOCK_ACTION_SUCCESS;
    cfg.response_result = MOCK_ACTION_SUCCESS;

    copy_value(cfg.locale, sizeof(cfg.locale), "th-TH");
    copy_value(cfg.conversation_id, sizeof(cfg.conversation_id), "conv-sim-001");
    copy_value(cfg.request_id, sizeof(cfg.request_id), "req-001");
    copy_value(cfg.user_text, sizeof(cfg.user_text), "Hello EchoEar");
    copy_value(cfg.response_text, sizeof(cfg.response_text), "Hello from EchoEar AI");

    return cfg;
}

static bool same_core_config(
    const conversation_mock_config_t *a,
    const conversation_mock_config_t *b)
{
    return a->enabled == b->enabled &&
           a->auto_start_session == b->auto_start_session &&
           a->request_timeout_ms == b->request_timeout_ms &&
           a->response_timeout_ms == b->response_timeout_ms &&
           strcmp(a->locale, b->locale) == 0 &&
           strcmp(a->conversation_id, b->conversation_id) == 0;
}

static void parse_file(const char *path, conversation_mock_config_t *cfg)
{
    FILE *file;
    char line[MOCK_LINE_MAX];

    if (path == NULL || cfg == NULL) return;

    file = fopen(path, "rb");
    if (file == NULL) {
        printf("[ConversationMock] file_missing path=%s\n", path);
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *equals;
        char *key;
        char *value;

        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        equals = strchr(line, '=');
        if (equals == NULL) continue;

        *equals = '\0';
        key = line;
        value = equals + 1;
        trim(key);
        trim(value);

        if (strcmp(key, "conversation_enabled") == 0)
            cfg->enabled = parse_bool(value, cfg->enabled);
        else if (strcmp(key, "conversation_auto_start_session") == 0)
            cfg->auto_start_session = parse_bool(value, cfg->auto_start_session);
        else if (strcmp(key, "conversation_auto_tick") == 0)
            cfg->auto_tick = parse_bool(value, cfg->auto_tick);
        else if (strcmp(key, "conversation_now_ms") == 0)
            cfg->now_ms = parse_u32(value, cfg->now_ms);
        else if (strcmp(key, "conversation_request_timeout_ms") == 0)
            cfg->request_timeout_ms = parse_u32(value, cfg->request_timeout_ms);
        else if (strcmp(key, "conversation_response_timeout_ms") == 0)
            cfg->response_timeout_ms = parse_u32(value, cfg->response_timeout_ms);
        else if (strcmp(key, "conversation_response_chunk_size") == 0)
            cfg->response_chunk_size = parse_u32(value, cfg->response_chunk_size);
        else if (strcmp(key, "conversation_locale") == 0)
            copy_value(cfg->locale, sizeof(cfg->locale), value);
        else if (strcmp(key, "conversation_id") == 0)
            copy_value(cfg->conversation_id, sizeof(cfg->conversation_id), value);
        else if (strcmp(key, "conversation_request_id") == 0)
            copy_value(cfg->request_id, sizeof(cfg->request_id), value);
        else if (strcmp(key, "conversation_user_text") == 0)
            copy_value(cfg->user_text, sizeof(cfg->user_text), value);
        else if (strcmp(key, "conversation_response_text") == 0)
            copy_value(cfg->response_text, sizeof(cfg->response_text), value);
        else if (strcmp(key, "conversation_dispatch_result") == 0)
            cfg->dispatch_result = parse_action(value, cfg->dispatch_result);
        else if (strcmp(key, "conversation_response_result") == 0)
            cfg->response_result = parse_action(value, cfg->response_result);
        else if (strcmp(key, "conversation_request_trigger") == 0)
            cfg->request_trigger = parse_int(value, cfg->request_trigger);
        else if (strcmp(key, "conversation_cancel_trigger") == 0)
            cfg->cancel_trigger = parse_int(value, cfg->cancel_trigger);
        else if (strcmp(key, "conversation_clear_trigger") == 0)
            cfg->clear_trigger = parse_int(value, cfg->clear_trigger);
        else if (strcmp(key, "conversation_end_session_trigger") == 0)
            cfg->end_session_trigger = parse_int(value, cfg->end_session_trigger);
    }

    fclose(file);
}

static void apply_core_config(const conversation_mock_config_t *cfg)
{
    echoear_conversation_config_t config;

    if (cfg == NULL) return;

    config = echoear_conversation_default_config();
    config.enabled = cfg->enabled;
    config.request_timeout_ms = cfg->request_timeout_ms;
    config.response_timeout_ms = cfg->response_timeout_ms;
    snprintf(config.locale, sizeof(config.locale), "%s", cfg->locale);

    (void)echoear_conversation_configure(&config);

    if (cfg->auto_start_session &&
        !echoear_conversation_get()->session_active &&
        cfg->conversation_id[0] != '\0') {
        (void)echoear_conversation_start_session(cfg->conversation_id);
        printf("[ConversationSession] action=start id=%s\n",
               cfg->conversation_id);
    }

    printf("[ConversationConfig] enabled=%d auto_start_session=%d locale=%s request_timeout=%u response_timeout=%u conversation_id=%s\n",
           cfg->enabled ? 1 : 0,
           cfg->auto_start_session ? 1 : 0,
           cfg->locale,
           (unsigned)cfg->request_timeout_ms,
           (unsigned)cfg->response_timeout_ms,
           cfg->conversation_id);
}

static void sync_gateway_ready(void)
{
    bool ready = echoear_ai_gateway_is_ready();

    if (!s_loaded_once || ready != s_last_gateway_ready) {
        echoear_conversation_set_gateway_ready(ready);
        printf("[ConversationGateway] ready=%d\n", ready ? 1 : 0);
        s_last_gateway_ready = ready;
    }
}

static void print_snapshot(void)
{
    echoear_conversation_t *c = echoear_conversation_get();

    if (c == NULL) return;

    printf("[Conversation] state=%s error=%s gateway=%d session=%d request_active=%d response_active=%d conversation_id=%s request_id=%s turn=%u requests=%u success=%u failures=%u cancels=%u text_len=%u deadline=%u now=%u generation=%u\n",
           echoear_conversation_state_name(c->state),
           echoear_conversation_error_name(c->error),
           c->gateway_ready ? 1 : 0,
           c->session_active ? 1 : 0,
           c->request_active ? 1 : 0,
           c->response_active ? 1 : 0,
           c->conversation_id[0] != '\0' ? c->conversation_id : "<none>",
           c->request_id[0] != '\0' ? c->request_id : "<none>",
           (unsigned)c->turn_count,
           (unsigned)c->request_count,
           (unsigned)c->success_count,
           (unsigned)c->failure_count,
           (unsigned)c->cancel_count,
           (unsigned)c->assistant_text_length,
           (unsigned)c->deadline_ms,
           (unsigned)c->now_ms,
           (unsigned)c->generation);
}

static void append_response_chunks(const char *request_id,
                                   const char *text,
                                   uint32_t chunk_size)
{
    size_t length;
    size_t offset = 0U;

    if (request_id == NULL || text == NULL) return;

    length = strlen(text);
    if (chunk_size == 0U) chunk_size = 16U;

    while (offset < length) {
        char chunk[MOCK_VALUE_MAX];
        size_t remaining = length - offset;
        size_t amount = remaining < chunk_size ? remaining : chunk_size;

        if (amount >= sizeof(chunk)) amount = sizeof(chunk) - 1U;

        memcpy(chunk, text + offset, amount);
        chunk[amount] = '\0';

        if (!echoear_conversation_response_append(request_id, chunk)) {
            printf("[ConversationResponseChunk] request_id=%s result=failed\n",
                   request_id);
            return;
        }

        printf("[ConversationResponseChunk] request_id=%s size=%u text=%s\n",
               request_id, (unsigned)amount, chunk);
        offset += amount;
    }
}

static void process_pending_request(void)
{
    echoear_conversation_t *c = echoear_conversation_get();
    echoear_conversation_request_t request;
    const char *response_request_id;

    if (c == NULL) return;

    if (c->state == ECHOEAR_CONVERSATION_STATE_REQUEST_PENDING &&
        c->request_active) {
        if (!echoear_conversation_get_pending_request(&request)) return;

        printf("[ConversationDispatch] request_id=%s result=%s\n",
               request.request_id, action_name(s_mock.dispatch_result));

        if (s_mock.dispatch_result == MOCK_ACTION_PENDING) return;

        if (s_mock.dispatch_result == MOCK_ACTION_GATEWAY_ERROR) {
            echoear_conversation_fail(ECHOEAR_CONVERSATION_ERROR_GATEWAY_ERROR);
            return;
        }

        if (s_mock.dispatch_result == MOCK_ACTION_STALE) {
            (void)echoear_conversation_mark_request_dispatched("stale-request");
            return;
        }

        if (!echoear_conversation_mark_request_dispatched(request.request_id))
            return;
    }

    c = echoear_conversation_get();
    if (c->state != ECHOEAR_CONVERSATION_STATE_WAITING_RESPONSE ||
        !c->request_active) return;

    printf("[ConversationResponse] request_id=%s result=%s\n",
           c->request_id, action_name(s_mock.response_result));

    if (s_mock.response_result == MOCK_ACTION_PENDING) return;

    if (s_mock.response_result == MOCK_ACTION_GATEWAY_ERROR) {
        echoear_conversation_fail(ECHOEAR_CONVERSATION_ERROR_GATEWAY_ERROR);
        return;
    }

    response_request_id =
        s_mock.response_result == MOCK_ACTION_STALE
            ? "stale-response"
            : c->request_id;

    if (!echoear_conversation_response_begin(response_request_id))
        return;

    append_response_chunks(
        response_request_id,
        s_mock.response_text,
        s_mock.response_chunk_size);

    c = echoear_conversation_get();
    if (c->state != ECHOEAR_CONVERSATION_STATE_STREAMING_RESPONSE)
        return;

    if (echoear_conversation_response_complete(response_request_id)) {
        printf("[ConversationComplete] request_id=%s assistant=%s\n",
               response_request_id,
               c->assistant_text);
    }
}

void echoear_conversation_mock_init(void)
{
    s_mock = default_mock();
    s_loaded_once = false;
    s_last_gateway_ready = false;
    s_last_request_trigger = 0;
    s_last_cancel_trigger = 0;
    s_last_clear_trigger = 0;
    s_last_end_session_trigger = 0;
    s_auto_now_ms = 0U;

    echoear_conversation_init();
}

void echoear_conversation_mock_load(const char *path)
{
    conversation_mock_config_t next;
    bool core_changed;
    uint32_t effective_now;
    echoear_conversation_t *c;

    next = s_loaded_once ? s_mock : default_mock();
    parse_file(path, &next);

    core_changed = !s_loaded_once || !same_core_config(&next, &s_mock);
    s_mock = next;

    if (core_changed) {
        apply_core_config(&s_mock);
    }

    effective_now = s_mock.now_ms;
    if (s_mock.auto_tick) {
        if (effective_now > s_auto_now_ms)
            s_auto_now_ms = effective_now;
        else
            s_auto_now_ms += 1000U;

        effective_now = s_auto_now_ms;
        s_mock.now_ms = effective_now;
    }

    echoear_conversation_tick(effective_now);

    sync_gateway_ready();

    c = echoear_conversation_get();

    if (s_mock.auto_start_session &&
        !c->session_active &&
        s_mock.conversation_id[0] != '\0') {
        (void)echoear_conversation_start_session(s_mock.conversation_id);
        printf("[ConversationSession] action=start id=%s\n",
               s_mock.conversation_id);
    }

    if (s_mock.end_session_trigger != 0 &&
        s_last_end_session_trigger == 0) {
        echoear_conversation_end_session();
        printf("[ConversationSession] action=end\n");
    }

    if (s_mock.cancel_trigger != 0 &&
        s_last_cancel_trigger == 0) {
        echoear_conversation_cancel();
        printf("[ConversationCancel] now=%u\n", (unsigned)effective_now);
    }

    if (s_mock.clear_trigger != 0 &&
        s_last_clear_trigger == 0) {
        echoear_conversation_clear_completed();
        printf("[ConversationClear] now=%u\n", (unsigned)effective_now);
    }

    if (s_mock.request_trigger != 0 &&
        s_last_request_trigger == 0) {
        bool accepted = echoear_conversation_submit_user_text(
            s_mock.request_id,
            s_mock.user_text);

        printf("[ConversationRequest] request_id=%s accepted=%d text=%s\n",
               s_mock.request_id,
               accepted ? 1 : 0,
               s_mock.user_text);
    }

    process_pending_request();

    s_last_request_trigger = s_mock.request_trigger;
    s_last_cancel_trigger = s_mock.cancel_trigger;
    s_last_clear_trigger = s_mock.clear_trigger;
    s_last_end_session_trigger = s_mock.end_session_trigger;
    s_loaded_once = true;

    print_snapshot();
}
