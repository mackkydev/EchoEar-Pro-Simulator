#include "echoear_runtime_reconnect_mock.h"
#include "echoear_runtime_network.h"
#include "echoear_runtime_reconnect.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 256

typedef struct {
    bool enabled;
    bool trigger;
    echoear_runtime_disconnect_reason_t reason;
    bool auto_tick;
    uint32_t now_ms;
    char result[24];
} mock_input_t;

typedef struct {
    char ip[ECHOEAR_RUNTIME_NETWORK_IPV4_MAX];
    char gateway[ECHOEAR_RUNTIME_NETWORK_IPV4_MAX];
    char netmask[ECHOEAR_RUNTIME_NETWORK_IPV4_MAX];
    int16_t rssi;
    bool valid;
} last_net_t;

static bool trigger_init;
static bool previous_trigger;
static uint32_t automatic_now_ms;
static char previous_log[640];
static last_net_t last_net;

static void copy_text(char *dst, size_t size, const char *src)
{
    if (!dst || size == 0U) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, size - 1U);
    dst[size - 1U] = '\0';
}

static char *trim(char *s)
{
    char *end;
    if (!s) return NULL;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static void strip_bom(char *s)
{
    unsigned char *b=(unsigned char *)s;
    if (s && b[0]==0xEFU && b[1]==0xBBU && b[2]==0xBFU)
        memmove(s, s+3, strlen(s+3)+1U);
}

static bool parse_bool(const char *v)
{
    return v && (strcmp(v,"1")==0 || strcmp(v,"true")==0 ||
                 strcmp(v,"yes")==0 || strcmp(v,"on")==0);
}

static void defaults(mock_input_t *in)
{
    memset(in,0,sizeof(*in));
    in->enabled=true;
    in->reason=ECHOEAR_RUNTIME_DISCONNECT_LINK_LOST;
    in->auto_tick=true;
    copy_text(in->result,sizeof(in->result),"success");
}

static bool read_input(const char *path, mock_input_t *in)
{
    FILE *f; char line[LINE_MAX_LEN];
    defaults(in);
    if (!path || !(f=fopen(path,"rb"))) return false;
    while (fgets(line,sizeof(line),f)) {
        char *eq,*key,*value;
        strip_bom(line); key=trim(line);
        if (!key || !*key || *key=='#' || !(eq=strchr(key,'='))) continue;
        *eq='\0'; value=trim(eq+1); key=trim(key);
        if (strcmp(key,"runtime_reconnect_enabled")==0) in->enabled=parse_bool(value);
        else if (strcmp(key,"runtime_disconnect_trigger")==0) in->trigger=parse_bool(value);
        else if (strcmp(key,"runtime_disconnect_reason")==0)
            echoear_runtime_reconnect_parse_disconnect_reason(value,&in->reason);
        else if (strcmp(key,"runtime_reconnect_auto_tick")==0) in->auto_tick=parse_bool(value);
        else if (strcmp(key,"runtime_reconnect_now_ms")==0) in->now_ms=(uint32_t)strtoul(value,NULL,10);
        else if (strcmp(key,"runtime_reconnect_result")==0) copy_text(in->result,sizeof(in->result),value);
    }
    fclose(f); return true;
}

static uint32_t resolve_now(const mock_input_t *in)
{
    if (!in->auto_tick) { automatic_now_ms=in->now_ms; return automatic_now_ms; }
    if (automatic_now_ms < in->now_ms) automatic_now_ms=in->now_ms;
    automatic_now_ms += 1000U;
    return automatic_now_ms;
}

static void remember_network(void)
{
    echoear_runtime_network_t *n=echoear_runtime_network_get();
    if (!n || !echoear_runtime_network_is_ready()) return;
    copy_text(last_net.ip,sizeof(last_net.ip),n->ip);
    copy_text(last_net.gateway,sizeof(last_net.gateway),n->gateway);
    copy_text(last_net.netmask,sizeof(last_net.netmask),n->netmask);
    last_net.rssi=n->rssi;
    last_net.valid=last_net.ip[0]!='\0';
}

static echoear_runtime_network_error_t network_error(echoear_runtime_disconnect_reason_t r)
{
    if (r==ECHOEAR_RUNTIME_DISCONNECT_DHCP_LOST)
        return ECHOEAR_RUNTIME_NETWORK_ERROR_DHCP_FAILED;
    if (r==ECHOEAR_RUNTIME_DISCONNECT_MANUAL)
        return ECHOEAR_RUNTIME_NETWORK_ERROR_NONE;
    return ECHOEAR_RUNTIME_NETWORK_ERROR_DRIVER;
}

static void print_state(uint32_t now_ms)
{
    echoear_runtime_reconnect_t *s=echoear_runtime_reconnect_get();
    char log[640];
    if (!s) return;
    snprintf(log,sizeof(log),
        "[RuntimeReconnect] state=%s reason=%s error=%s ready=%d request=%d user_disconnect=%d provisioning_allowed=%d attempt=%u/%u delay=%lu due=%lu now=%lu disconnects=%lu reconnects=%lu generation=%lu",
        echoear_runtime_reconnect_state_name(s->state),
        echoear_runtime_disconnect_reason_name(s->disconnect_reason),
        echoear_runtime_reconnect_error_name(s->error),
        s->network_ready?1:0, s->reconnect_requested?1:0, s->user_disconnect?1:0,
        s->provisioning_allowed?1:0, (unsigned)s->fast_attempt,
        (unsigned)s->policy.fast_attempts, (unsigned long)s->retry_delay_ms,
        (unsigned long)s->retry_due_ms, (unsigned long)now_ms,
        (unsigned long)s->disconnect_count, (unsigned long)s->reconnect_count,
        (unsigned long)s->generation);
    if (strcmp(log,previous_log)!=0) { printf("%s\n",log); copy_text(previous_log,sizeof(previous_log),log); }
}

static void sync_monitoring(void)
{
    echoear_runtime_reconnect_t *r=echoear_runtime_reconnect_get();
    if (!echoear_runtime_network_is_ready()) return;
    remember_network();
    if (r->state==ECHOEAR_RUNTIME_RECONNECT_STATE_IDLE ||
        r->state==ECHOEAR_RUNTIME_RECONNECT_STATE_RESTORED)
        echoear_runtime_reconnect_mark_network_ready();
}

static void inject_disconnect(echoear_runtime_disconnect_reason_t reason, uint32_t now_ms)
{
    remember_network();
    printf("[RuntimeDisconnect] reason=%s\n", echoear_runtime_disconnect_reason_name(reason));
    echoear_runtime_network_mark_offline(network_error(reason));
    echoear_runtime_reconnect_report_disconnect(reason,now_ms);
}

static void restore_network(void)
{
    echoear_runtime_network_t *n=echoear_runtime_network_get();
    const char *ip=last_net.valid?last_net.ip:"192.168.1.88";
    const char *gw=last_net.valid?last_net.gateway:"192.168.1.1";
    const char *mask=last_net.valid?last_net.netmask:"255.255.255.0";
    int16_t rssi=last_net.valid?last_net.rssi:-45;
    echoear_runtime_network_mark_associated(rssi);
    echoear_runtime_network_mark_ip_ready(ip,gw,mask,rssi);
    printf("[RuntimeReconnectRestored] ssid=%s ip=%s gateway=%s rssi=%d\n",
        n->ssid[0]?n->ssid:"<none>", n->ip[0]?n->ip:"<none>",
        n->gateway[0]?n->gateway:"<none>",(int)n->rssi);
}

static void consume_request(const mock_input_t *in, uint32_t now_ms)
{
    echoear_runtime_reconnect_t *r=echoear_runtime_reconnect_get();
    echoear_runtime_network_t *n=echoear_runtime_network_get();
    if (!echoear_runtime_reconnect_take_request()) return;

    echoear_runtime_network_request_reconnect(now_ms);
    (void)echoear_runtime_network_take_connect_request();
    echoear_runtime_network_mark_connecting(now_ms);
    echoear_runtime_reconnect_mark_connecting();

    printf("[RuntimeReconnectAction] ssid=%s attempt=%u result=%s secret=<secure-store>\n",
        n->ssid[0]?n->ssid:"<none>",(unsigned)r->fast_attempt,in->result);
    print_state(now_ms);

    if (strcmp(in->result,"success")==0) {
        restore_network();
        echoear_runtime_reconnect_mark_restored();
        print_state(now_ms);
        echoear_runtime_reconnect_mark_network_ready();
    } else if (strcmp(in->result,"failed")==0) {
        echoear_runtime_network_mark_offline(ECHOEAR_RUNTIME_NETWORK_ERROR_DRIVER);
        echoear_runtime_reconnect_mark_failed(now_ms);
    } else if (strcmp(in->result,"pending")!=0) {
        echoear_runtime_network_mark_offline(ECHOEAR_RUNTIME_NETWORK_ERROR_INTERNAL);
        echoear_runtime_reconnect_suspend();
    }
}

bool echoear_runtime_reconnect_mock_load(const char *path)
{
    mock_input_t in; uint32_t now_ms; bool edge;
    if (!read_input(path,&in)) return false;
    now_ms=resolve_now(&in);
    if (!trigger_init) { previous_trigger=false; trigger_init=true; }
    edge=in.trigger && !previous_trigger;
    previous_trigger=in.trigger;

    if (!in.enabled) { print_state(now_ms); return true; }

    sync_monitoring();
    print_state(now_ms);

    if (edge) { inject_disconnect(in.reason,now_ms); print_state(now_ms); }

    echoear_runtime_reconnect_tick(now_ms);
    print_state(now_ms);
    consume_request(&in,now_ms);
    print_state(now_ms);
    return true;
}
