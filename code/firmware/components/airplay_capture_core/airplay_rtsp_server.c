/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Single-client RTSP socket lifecycle for the ESP-IDF AirPlay session router.
 * AirPlay service discovery and authorization material are provided outside
 * this component; it never fabricates an authenticated session.
 */
#include "airplay_rtsp_server.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "airplay_bplist.h"
#include "airplay_diagnostics.h"
#include "airplay_control_cipher.h"
#include "airplay_control_port.h"
#include "airplay_activation.h"
#include "esp_timer.h"
#include "capture_focus_plists.h"
#include "airplay_event_port.h"
#include "airplay_event_lifecycle.h"
#include "airplay_screen_lifecycle.h"
#include "airplay_media_bridge.h"
#include "airplay_timing_port.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "mbedtls/platform_util.h"
#include "net_port.h"

#define RTSP_SERVER_TASK_STACK_BYTES 10240U
#define RTSP_SERVER_TASK_PRIORITY 5U
#define RTSP_SERVER_POLL_TIMEOUT_MS 250U
#define RTSP_SERVER_RX_CHUNK_BYTES 1200U
#define RTSP_SERVER_STREAM_BUFFER_BYTES 6144U
#define RTSP_SERVER_RESPONSE_BYTES AIRPLAY_CONTROL_CIPHER_MAX_FRAME_BYTES
#define RTSP_SERVER_CIPHER_FRAME_BYTES \
    (AIRPLAY_CONTROL_CIPHER_MAX_FRAME_BYTES + AIRPLAY_CONTROL_CIPHER_OVERHEAD_BYTES)

extern void probe_receiver_connection(bool);
extern void probe_receiver_request(const char *,const char *,bool,unsigned,unsigned,int);
extern void probe_receiver_event(unsigned,int,unsigned);
extern void probe_receiver_setup_response(const uint8_t *,size_t);
extern void probe_receiver_keepalive_note(unsigned,int);
extern void probe_receiver_audio_note(unsigned,unsigned);
extern unsigned probe_trace_response_code(const uint8_t *,size_t);
static const char *TAG = "airplay_rtsp";

typedef struct {
    uint8_t stream[RTSP_SERVER_STREAM_BUFFER_BYTES];
    uint8_t response[RTSP_SERVER_RESPONSE_BYTES];
    uint8_t io[RTSP_SERVER_CIPHER_FRAME_BYTES];
    airplay_rtsp_request_t request;
    airplay_control_cipher_t *cipher;
    size_t stream_len;
} rtsp_connection_t;

struct airplay_rtsp_server {
    airplay_rtsp_server_config_t config;
    uint8_t info_plist[AIRPLAY_RTSP_SESSION_INFO_MAX_BYTES];
    int listener_fd;
    int client_fd;
    uint16_t port;
    volatile bool stop_requested;
    volatile bool event_connected;
    volatile bool event_key_used;
    volatile bool event_failed;
    volatile bool event_stopping;
    bool record_started,mode_observed,ui_requested,response_overflow;
    struct {unsigned code;int cseq;bool body_ok;} responses[4];
    unsigned response_count;
    airplay_activation_t activation;
    airplay_activation_stage_t last_activation_stage;
    TaskHandle_t task;
    SemaphoreHandle_t stopped;
    airplay_event_port_t *event_port;
    airplay_timing_port_t *timing_port;
    /* 0.6.3: minimal CarPlay audio (type 100) receive sink - UDP, count-only. */
    int audio_fd;
    uint16_t audio_port;
    TaskHandle_t audio_task;
    /* 0.7.2: CarPlay keep-alive probe sink (DiPlay keepAlivePort): UDP socket
     * bound on request, packets received and dropped. */
    int keepalive_fd;
    uint16_t keepalive_port;
    TaskHandle_t keepalive_task;
    SemaphoreHandle_t keepalive_stopped;
    volatile bool keepalive_stop_requested;
};

static portMUX_TYPE s_server_mux = portMUX_INITIALIZER_UNLOCKED;
static airplay_rtsp_server_t *s_server;
static void advance_activation(airplay_rtsp_server_t *s);

/* Called under the event cipher mutex: record observations only. Sending here
 * would re-enter that mutex. The RTSP task drains this bounded mailbox. */
void airplay_rtsp_server_event_response(unsigned code,int cseq,bool body_ok)
{
    portENTER_CRITICAL(&s_server_mux);
    airplay_rtsp_server_t *s=s_server;
    if(s&&s->record_started&&!s->stop_requested){
        if(s->response_count<4){unsigned i=s->response_count++;s->responses[i].code=code;s->responses[i].cseq=cseq;s->responses[i].body_ok=body_ok;}
        else s->response_overflow=true;
    }
    portEXIT_CRITICAL(&s_server_mux);
}
void airplay_rtsp_server_modes_observed(void)
{
    portENTER_CRITICAL(&s_server_mux);
    if(s_server&&s_server->record_started)s_server->mode_observed=true;
    portEXIT_CRITICAL(&s_server_mux);
}
/* 0.6.7: the phone initiated requestUI (POST /command accepted, or it acked
 * our own requestUI). Record it and advance: TakeScreen follows immediately. */
void airplay_rtsp_server_ui_requested(void)
{
    portENTER_CRITICAL(&s_server_mux);
    if(s_server&&s_server->record_started)s_server->ui_requested=true;
    portEXIT_CRITICAL(&s_server_mux);
    if(s_server)advance_activation(s_server);
}
static void advance_activation(airplay_rtsp_server_t *s)
{
    unsigned codes[4],count;int seqs[4];bool valid[4],modes,ui,ready,failed;
    portENTER_CRITICAL(&s_server_mux);
    ready=s->record_started&&s->event_connected&&!s->stop_requested;
    failed=s->event_failed||s->response_overflow;
    modes=s->mode_observed;s->mode_observed=false;
    ui=s->ui_requested;s->ui_requested=false;
    count=s->response_count;s->response_count=0;
    for(unsigned i=0;i<count;i++){codes[i]=s->responses[i].code;seqs[i]=s->responses[i].cseq;valid[i]=s->responses[i].body_ok;}
    portEXIT_CRITICAL(&s_server_mux);
    if(failed&&s->activation.stage!=ACT_IDLE)s->activation.stage=ACT_FAILED;
    if(modes)airplay_activation_modes(&s->activation);
    if(ui)airplay_activation_ui_requested(&s->activation);
    for(unsigned i=0;i<count;i++)airplay_activation_response(&s->activation,codes[i],seqs[i],valid[i]);
    unsigned action=ready?airplay_activation_next(&s->activation,esp_timer_get_time()/1000):0;
    if(action){
        uint32_t cseq=0;
        bool sent=airplay_control_port_send_command(action==1?capture_request_ui:capture_take_screen,
                        action==1?sizeof(capture_request_ui):sizeof(capture_take_screen),&cseq);
        airplay_activation_sent(&s->activation,sent,cseq);
        probe_receiver_event(action==1?130:131,sent?(int)cseq:-1,0);
    }
    if(s->last_activation_stage!=s->activation.stage){
        s->last_activation_stage=s->activation.stage;
        probe_receiver_event(139,(int)s->activation.stage,0);
    }
}

static void *server_calloc(size_t count, size_t size)
{
    if (count == 0U || size == 0U || count > SIZE_MAX / size) return NULL;
    size_t bytes = count * size;
    void *buffer = heap_caps_calloc(count, size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_calloc(count, size,
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (buffer == NULL) return NULL;
    memset(buffer, 0, bytes);
    return buffer;
}

static void event_state_changed(bool connected, void *context);
static void screen_stream_exited(void *context);
static void stop_event(void *context);
static void stop_timing(void *context);

static void server_free(void *buffer, size_t size)
{
    if (buffer == NULL) return;
    mbedtls_platform_zeroize(buffer, size);
    heap_caps_free(buffer);
}

static bool socket_write_all(int fd, const uint8_t *bytes, size_t len)
{
    size_t offset = 0U;
    while (offset < len) {
        int sent = send(fd, bytes + offset, len - offset, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        offset += (size_t)sent;
    }
    return true;
}

static bool start_screen(const airplay_rtsp_pairing_t *pairing,
                         uint64_t stream_connection_id,
                         uint16_t requested_port,
                         uint16_t *bound_port,
                         void *context)
{
    airplay_rtsp_server_t *server = context;
    esp_err_t result = airplay_media_bridge_start_screen_stream(
               pairing, stream_connection_id, requested_port,
               screen_stream_exited, server, bound_port);
    if(result!=ESP_OK)probe_receiver_event(114,result,0);
    return result==ESP_OK;
}

static void screen_stream_exited(void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL) return;
    int rtsp_fd = -1;
    bool unexpected_exit;
    portENTER_CRITICAL(&s_server_mux);
    unexpected_exit = airplay_screen_lifecycle_is_unexpected_exit(
        server->stop_requested);
    if (unexpected_exit) rtsp_fd = server->client_fd;
    portEXIT_CRITICAL(&s_server_mux);
    if (!unexpected_exit) return;
    ESP_LOGW(TAG, "screen_stream=lost rtsp_session=closing key_reuse=blocked");
    if (rtsp_fd >= 0) {
        probe_receiver_screen_note(PROBE_EVENT_SCREEN_CLOSE_RTSP,0,0,0);
        shutdown(rtsp_fd, SHUT_RDWR);
    }
}

static bool start_event(const airplay_rtsp_pairing_t *pairing,
                        uint16_t *bound_port,
                        void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL || bound_port == NULL) return false;
    portENTER_CRITICAL(&s_server_mux);
    bool event_failed = server->event_failed;
    bool event_key_used = server->event_key_used;
    bool listener_active = server->event_port != NULL;
    bool may_start = airplay_event_lifecycle_may_start(
        listener_active, event_key_used, event_failed);
    if (listener_active) {
        uint16_t existing_port = airplay_event_port_get_port(server->event_port);
        portEXIT_CRITICAL(&s_server_mux);
        if (!may_start) return false;
        *bound_port = existing_port;
        return *bound_port != 0U;
    }
    portEXIT_CRITICAL(&s_server_mux);
    /* Event AEAD counters restart at zero on attach. Do not reuse Pair-Verify
     * event keys after a connected listener has been stopped in this session. */
    if (!may_start) return false;
    airplay_pair_verify_event_keys_t keys;
    airplay_pair_verify_result_t key_result =
        airplay_rtsp_pairing_get_event_keys(pairing, &keys);
    if (key_result != AIRPLAY_PAIR_VERIFY_OK) {
        mbedtls_platform_zeroize(&keys, sizeof(keys));
        probe_receiver_event(110,key_result,0);
        ESP_LOGW(TAG, "event_setup=pair_verify_not_ready result=%d", (int)key_result);
        return false;
    }
    airplay_event_port_config_t event_config = {
        .keys = keys,
        .stream_id = 0U,
        .initial_cseq = 0U,
        .on_state = event_state_changed,
        .context = server,
    };
    airplay_event_port_t *event_port = NULL;
    esp_err_t result = airplay_event_port_start(&event_config,
                                                &event_port);
    mbedtls_platform_zeroize(&keys, sizeof(keys));
    mbedtls_platform_zeroize(&event_config, sizeof(event_config));
    if (result != ESP_OK) {
        probe_receiver_event(111,result,0);
        ESP_LOGE(TAG, "event_setup=listener_failed error=%s", esp_err_to_name(result));
        return false;
    }
    portENTER_CRITICAL(&s_server_mux);
    server->event_port = event_port;
    bool event_failed_during_start = server->event_failed;
    portEXIT_CRITICAL(&s_server_mux);
    if (event_failed_during_start) {
        stop_event(server);
        return false;
    }
    *bound_port = airplay_event_port_get_port(event_port);
    if (*bound_port == 0U) {
        stop_event(server);
        return false;
    }
    ESP_LOGI(TAG, "event_setup=ready port=%u", (unsigned)*bound_port);
    return true;
}

static bool start_timing(uint16_t peer_port,
                         uint16_t *bound_port,
                         void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL || bound_port == NULL || server->client_fd < 0) return false;
    if (server->timing_port != NULL) {
        *bound_port = airplay_timing_port_get_port(server->timing_port);
        return *bound_port != 0U;
    }
    struct sockaddr_in6 peer;
    socklen_t peer_len = sizeof(peer);
    memset(&peer, 0, sizeof(peer));
    if (getpeername(server->client_fd, (struct sockaddr *)&peer, &peer_len) != 0 ||
        peer.sin6_family != AF_INET6) {
        probe_receiver_event(112,errno,0);
        ESP_LOGW(TAG, "timing_setup=peer_address_unavailable errno=%d", errno);
        return false;
    }
    airplay_timing_port_config_t config = {
        .peer = peer,
        .peer_port = peer_port,
    };
    esp_err_t result = airplay_timing_port_start(&config, &server->timing_port);
    if (result != ESP_OK) {
        probe_receiver_event(113,result,0);
        ESP_LOGE(TAG, "timing_setup=listener_failed error=%s", esp_err_to_name(result));
        return false;
    }
    *bound_port = airplay_timing_port_get_port(server->timing_port);
    ESP_LOGI(TAG, "timing_setup=ready port=%u peer_port=%u",
             (unsigned)*bound_port, (unsigned)peer_port);
    return *bound_port != 0U;
}

static void stop_screen(void *context)
{
    (void)context;
    airplay_media_bridge_stop_screen_stream();
}

static void event_state_changed(bool connected, void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL) return;
    bool unexpected_exit = false;
    portENTER_CRITICAL(&s_server_mux);
    server->event_connected = connected;
    if (connected) {
        server->event_key_used = true;
    } else if (airplay_event_lifecycle_is_unexpected_exit(
                   connected, server->event_stopping,
                   server->stop_requested)) {
        /* The event task reports false on every unrequested exit, including
         * listener/attach failures before a successful connection. */
        server->event_failed = true;
        unexpected_exit = true;
    }
    portEXIT_CRITICAL(&s_server_mux);
    if (unexpected_exit) {
        ESP_LOGW(TAG, "event_channel=lost rtsp_session=continuing input=disabled key_reuse=blocked");
    }
    if (server->config.on_event_state != NULL) {
        server->config.on_event_state(connected,
                                      server->config.event_state_context);
    }
    probe_receiver_event(132,connected,0);
    /* Activation advances on the RTSP task after RECORD is acknowledged. */
}

static void stop_event(void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL) return;
    portENTER_CRITICAL(&s_server_mux);
    airplay_event_port_t *event_port = server->event_port;
    if (event_port != NULL) {
        server->event_port = NULL;
        server->event_connected = false;
    }
    if (event_port != NULL) {
        /* Suppress a fatal-session transition for an intentional stop. */
        server->event_stopping = true;
    }
    portEXIT_CRITICAL(&s_server_mux);
    if (event_port == NULL) return;
    airplay_event_port_stop(event_port);
    portENTER_CRITICAL(&s_server_mux);
    server->event_stopping = false;
    portEXIT_CRITICAL(&s_server_mux);
}

static void stop_timing(void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL || server->timing_port == NULL) return;
    airplay_timing_port_t *timing_port = server->timing_port;
    server->timing_port = NULL;
    airplay_timing_port_stop(timing_port);
}

/* --- 0.6.3 minimal audio receive sink (type 100, count-only, no decode) --- */
static void stop_audio(void *context);
static void audio_rx_task(void *ctx)
{
    airplay_rtsp_server_t *server = ctx;
    uint8_t buf[2048];
    for (;;) {
        ssize_t n = recvfrom(server->audio_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n > 0) probe_receiver_audio_note((unsigned)n, 1U);
    }
    vTaskDelete(NULL);
}

static bool start_audio(uint64_t stream_connection_id,
                        uint16_t *bound_port,
                        void *context)
{
    (void)stream_connection_id;
    airplay_rtsp_server_t *server = context;
    if (server == NULL || bound_port == NULL) return false;
    portENTER_CRITICAL(&s_server_mux);
    bool already = server->audio_fd >= 0;
    uint16_t existing_port = server->audio_port;
    portEXIT_CRITICAL(&s_server_mux);
    if (already) {
        *bound_port = existing_port;
        return existing_port != 0U;
    }
    int fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        probe_receiver_event(117,errno,0);
        ESP_LOGE(TAG, "audio_socket=failed errno=%d", errno);
        return false;
    }
    struct sockaddr_in6 address;
    if (!capture_usb_address(fd, &address, 0)) {
        close(fd);
        probe_receiver_event(117,ESP_ERR_INVALID_STATE,0);
        return false;
    }
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        probe_receiver_event(117,errno,0);
        ESP_LOGE(TAG, "audio_bind=failed errno=%d", errno);
        close(fd);
        return false;
    }
    socklen_t address_len = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &address_len) != 0 ||
        address.sin6_family != AF_INET6) {
        close(fd);
        probe_receiver_event(117,errno,0);
        return false;
    }
    uint16_t port = ntohs(address.sin6_port);
    if (port == 0U) {
        close(fd);
        probe_receiver_event(117,ESP_ERR_INVALID_STATE,0);
        return false;
    }
    portENTER_CRITICAL(&s_server_mux);
    server->audio_fd = fd;
    server->audio_port = port;
    portEXIT_CRITICAL(&s_server_mux);
    TaskHandle_t task = NULL;
    if (xTaskCreate(audio_rx_task, "audio_rx", 4096, server, 2,
                    &task) != pdPASS) {
        stop_audio(server);
        probe_receiver_event(117,ESP_ERR_NO_MEM,0);
        return false;
    }
    portENTER_CRITICAL(&s_server_mux);
    server->audio_task = task;
    portEXIT_CRITICAL(&s_server_mux);
    ESP_LOGI(TAG, "audio_start=ready port=%u", (unsigned)port);
    *bound_port = port;
    return true;
}

static void stop_audio(void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL) return;
    int fd;
    TaskHandle_t task;
    portENTER_CRITICAL(&s_server_mux);
    fd = server->audio_fd;
    task = server->audio_task;
    server->audio_fd = -1;
    server->audio_port = 0U;
    server->audio_task = NULL;
    portEXIT_CRITICAL(&s_server_mux);
    if (task != NULL) vTaskDelete(task);
    if (fd >= 0) close(fd);
}

/* --- 0.7.2 keep-alive probe sink (DiPlay keepAlivePort) ------------------ */
static void stop_keepalive(void *context);
static void keepalive_rx_task(void *ctx)
{
    airplay_rtsp_server_t *server = ctx;
    uint8_t buf[64];
    while (!server->keepalive_stop_requested) {
        ssize_t n = recvfrom(server->keepalive_fd, buf, sizeof(buf), 0,
                             NULL, NULL);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (!server->keepalive_stop_requested) probe_receiver_keepalive_note(0,errno);
            break;
        }
        probe_receiver_keepalive_note((unsigned)n,0);
        /* DiPlay runKeepAlive: receive and drop; an open reachable port is
         * the liveness signal, no reply is expected. */
    }
    xSemaphoreGive(server->keepalive_stopped);
    vTaskDelete(NULL);
}
static bool start_keepalive(uint16_t *bound_port, void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL || bound_port == NULL) return false;
    portENTER_CRITICAL(&s_server_mux);
    bool already = server->keepalive_fd >= 0;
    uint16_t existing_port = server->keepalive_port;
    portEXIT_CRITICAL(&s_server_mux);
    if (already) {
        *bound_port = existing_port;
        return existing_port != 0U;
    }
    int fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        probe_receiver_event(PROBE_EVENT_KEEPALIVE_START_FAILED,errno,0);
        ESP_LOGE(TAG, "keepalive_socket=failed errno=%d", errno);
        return false;
    }
    struct sockaddr_in6 address;
    if (!capture_usb_address(fd, &address, 0)) {
        close(fd);
        probe_receiver_event(PROBE_EVENT_KEEPALIVE_START_FAILED,ESP_ERR_INVALID_STATE,0);
        return false;
    }
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        probe_receiver_event(PROBE_EVENT_KEEPALIVE_START_FAILED,errno,0);
        ESP_LOGE(TAG, "keepalive_bind=failed errno=%d", errno);
        close(fd);
        return false;
    }
    socklen_t address_len = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &address_len) != 0 ||
        address.sin6_family != AF_INET6) {
        close(fd);
        probe_receiver_event(PROBE_EVENT_KEEPALIVE_START_FAILED,errno,0);
        return false;
    }
    uint16_t port = ntohs(address.sin6_port);
    if (port == 0U) {
        close(fd);
        probe_receiver_event(PROBE_EVENT_KEEPALIVE_START_FAILED,ESP_ERR_INVALID_STATE,0);
        return false;
    }
    struct timeval timeout = {.tv_sec=0,.tv_usec=250000};
    SemaphoreHandle_t stopped=xSemaphoreCreateBinary();
    if (!stopped || setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))) {
        if (stopped) vSemaphoreDelete(stopped);
        close(fd);
        probe_receiver_event(PROBE_EVENT_KEEPALIVE_START_FAILED,ESP_FAIL,0);
        return false;
    }
    portENTER_CRITICAL(&s_server_mux);
    server->keepalive_fd = fd;
    server->keepalive_port = port;
    server->keepalive_stopped = stopped;
    server->keepalive_stop_requested = false;
    portEXIT_CRITICAL(&s_server_mux);
    TaskHandle_t task = NULL;
    if (xTaskCreate(keepalive_rx_task, "kalive_rx", 2048, server, 2,
                    &task) != pdPASS) {
        stop_keepalive(server);
        probe_receiver_event(PROBE_EVENT_KEEPALIVE_START_FAILED,ESP_ERR_NO_MEM,0);
        return false;
    }
    portENTER_CRITICAL(&s_server_mux);
    server->keepalive_task = task;
    portEXIT_CRITICAL(&s_server_mux);
    ESP_LOGI(TAG, "keepalive_start=ready port=%u", (unsigned)port);
    *bound_port = port;
    return true;
}
static void stop_keepalive(void *context)
{
    airplay_rtsp_server_t *server = context;
    if (server == NULL) return;
    int fd;
    TaskHandle_t task;
    SemaphoreHandle_t stopped;
    portENTER_CRITICAL(&s_server_mux);
    fd = server->keepalive_fd;
    task = server->keepalive_task;
    stopped = server->keepalive_stopped;
    server->keepalive_stop_requested = true;
    portEXIT_CRITICAL(&s_server_mux);
    /* Join before freeing the socket or session. A task that exited on a
     * receive error has already signalled; never delete its stale handle. */
    if (task != NULL) xSemaphoreTake(stopped,portMAX_DELAY);
    if (fd >= 0) close(fd);
    if (stopped != NULL) vSemaphoreDelete(stopped);
    portENTER_CRITICAL(&s_server_mux);
    server->keepalive_fd = -1;
    server->keepalive_port = 0U;
    server->keepalive_task = NULL;
    server->keepalive_stopped = NULL;
    portEXIT_CRITICAL(&s_server_mux);
}

static airplay_rtsp_session_result_t create_session(
    airplay_rtsp_server_t *server,
    airplay_rtsp_session_t **out_session)
{
    /* 0.7.6: prove the keep-alive callback registration reaches the session;
     * start_keepalive is a static symbol in this file (never NULL). */
    probe_receiver_event(121, 0, 1U);
    airplay_rtsp_session_config_t config = {
        .pairing = server->config.pairing,
        .info_plist = server->info_plist,
        .info_plist_len = server->config.info_plist_len,
        .screen_requested_port = server->config.screen_port,
        .start_screen = start_screen,
        .start_event = start_event,
        .start_timing = start_timing,
        .start_audio = start_audio,
        .start_keepalive = start_keepalive,
        .stop_screen = stop_screen,
        .stop_event = stop_event,
        .stop_timing = stop_timing,
        .stop_audio = stop_audio,
        .stop_keepalive = stop_keepalive,
        .media_context = server,
    };
    return airplay_rtsp_session_create(&config, out_session);
}

static void consume_stream(rtsp_connection_t *connection, size_t count)
{
    if (count < connection->stream_len) {
        memmove(connection->stream, connection->stream + count,
                connection->stream_len - count);
    }
    connection->stream_len -= count;
}

static bool append_stream(rtsp_connection_t *connection,
                          const uint8_t *bytes,
                          size_t len)
{
    if (connection == NULL || (bytes == NULL && len != 0U) ||
        len > sizeof(connection->stream) - connection->stream_len) {
        probe_receiver_event(109,0,len);
        return false;
    }
    if (len != 0U) {
        memcpy(connection->stream + connection->stream_len, bytes, len);
        connection->stream_len += len;
    }
    return true;
}

static bool send_response(rtsp_connection_t *connection,
                          int client_fd,
                          size_t response_len)
{
    if (connection->cipher == NULL) {
        return socket_write_all(client_fd, connection->response, response_len);
    }
    size_t encrypted_len = 0U;
    airplay_control_cipher_result_t result = airplay_control_cipher_encrypt(
        connection->cipher, connection->response, response_len,
        connection->io, sizeof(connection->io), &encrypted_len);
    return result == AIRPLAY_CONTROL_CIPHER_OK &&
           socket_write_all(client_fd, connection->io, encrypted_len);
}

static bool process_stream(airplay_rtsp_server_t *server,
                           rtsp_connection_t *connection,
                           airplay_rtsp_session_t *session,
                           int client_fd)
{
    while (connection->stream_len != 0U) {
        airplay_rtsp_parse_result_t parse_result = airplay_rtsp_parse_request(
            connection->stream, connection->stream_len, &connection->request);
        if (parse_result == AIRPLAY_RTSP_PARSE_INCOMPLETE) {
            return connection->stream_len < sizeof(connection->stream);
        }
        if (parse_result != AIRPLAY_RTSP_PARSE_COMPLETE) {
            probe_receiver_event(104,parse_result,connection->stream_len);
            ESP_LOGW(TAG, "rtsp_request=rejected parse=%d buffered=%u",
                     (int)parse_result, (unsigned)connection->stream_len);
            return false;
        }
        unsigned command_count=airplay_rtsp_session_command_count(session);
        size_t response_len = 0U;
        bool enable_cipher = false;
        airplay_rtsp_session_result_t result = airplay_rtsp_session_handle(
            session, &connection->request, connection->response,
            sizeof(connection->response), &response_len, &enable_cipher);
        probe_receiver_request(connection->request.method,connection->request.path,enable_cipher,probe_trace_response_code(connection->response,response_len),connection->request.body_len,result);
        if(!strcmp(connection->request.method,"RECORD")){
            probe_receiver_event(PROBE_EVENT_READINESS,airplay_rtsp_session_readiness(session),0);
            /* 0.6.8: carplayd proven-working timing (session.rs RECORD:
             * accept event channel, then send requestUI+TakeScreen back-to-back
             * BEFORE replying RECORD 200 - the 200 is held until the focus
             * handshake completes; the phone then confirms via POST /command
             * requestUI + modesChanged). Send only on the first RECORD of a
             * session so a re-RECORD never duplicates the focus handshake. */
            portENTER_CRITICAL(&s_server_mux);
            bool first=!server->record_started;
            if(first)server->record_started=true;
            bool connected=server->event_connected&&!server->stop_requested;
            portEXIT_CRITICAL(&s_server_mux);
            if(first){
                airplay_activation_begin(&server->activation,esp_timer_get_time()/1000);
                if(connected){
                    uint32_t c1=0,c2=0;
                    bool u=airplay_control_port_send_command(capture_request_ui,sizeof(capture_request_ui),&c1);
                    bool m=airplay_control_port_send_command(capture_take_screen,sizeof(capture_take_screen),&c2);
                    airplay_activation_sent(&server->activation,u,c1);
                    airplay_activation_sent(&server->activation,m,c2);
                    server->activation.stage=ACT_TAKE_WAIT;
                    probe_receiver_event(130,u?(int)c1:-1,0);
                    probe_receiver_event(131,m?(int)c2:-1,0);
                }
            }
        }
        if (result != AIRPLAY_RTSP_SESSION_OK || response_len == 0U ||
            !send_response(connection, client_fd, response_len)) {
            probe_receiver_event(105,result,response_len);
            ESP_LOGE(TAG, "rtsp_response=failed result=%d method=%s path=%s",
                     (int)result, connection->request.method,
                     connection->request.path);
            return false;
        }
        if(!strcmp(connection->request.method,"SETUP"))
            probe_receiver_setup_response(connection->response,response_len);
        if(airplay_rtsp_session_command_count(session)!=command_count){
            airplay_command_t command=airplay_rtsp_session_last_command(session);
            probe_receiver_event(137,(int)command.kind,connection->request.body_len);
            if(command.kind==1&&command.screen_entity_known){
                airplay_rtsp_server_modes_observed();
                probe_receiver_event(138,(int)command.screen_entity,command.audio_entity);
            }
        }
        if(!strcmp(connection->request.method,"TEARDOWN")){
            portENTER_CRITICAL(&s_server_mux);server->record_started=false;server->response_count=0;server->mode_observed=false;server->ui_requested=false;portEXIT_CRITICAL(&s_server_mux);
            server->activation=(airplay_activation_t){0};
        }
        advance_activation(server);
        size_t consumed = connection->request.consumed;
        consume_stream(connection, consumed);
        ESP_LOGI(TAG, "rtsp_request=handled method=%s path=%s cseq=%s bytes=%u",
                 connection->request.method, connection->request.path,
                 connection->request.cseq, (unsigned)consumed);
        memset(&connection->request, 0, sizeof(connection->request));

        if (enable_cipher) {
            if (connection->stream_len != 0U) {
                probe_receiver_event(107,0,connection->stream_len);
                ESP_LOGW(TAG, "rtsp_cipher=transition_with_pending_plaintext");
                return false;
            }
            airplay_pair_verify_control_keys_t keys;
            airplay_pair_verify_result_t key_result =
                airplay_rtsp_session_get_control_keys(session, &keys);
            if (key_result != AIRPLAY_PAIR_VERIFY_OK ||
                airplay_control_cipher_create(keys.read_key, keys.write_key,
                                              &connection->cipher) !=
                    AIRPLAY_CONTROL_CIPHER_OK) {
                mbedtls_platform_zeroize(&keys, sizeof(keys));
                probe_receiver_event(108,key_result,0);
                ESP_LOGE(TAG, "rtsp_cipher=enable_failed result=%d",
                         (int)key_result);
                return false;
            }
            mbedtls_platform_zeroize(&keys, sizeof(keys));
            airplay_rtsp_session_mark_control_cipher_active(session);
            ESP_LOGI(TAG, "rtsp_cipher=enabled");
        }
    }
    (void)server;
    return true;
}

static bool process_received_bytes(airplay_rtsp_server_t *server,
                                  rtsp_connection_t *connection,
                                  airplay_rtsp_session_t *session,
                                  int client_fd,
                                  const uint8_t *bytes,
                                  size_t length)
{
    if (connection->cipher == NULL) {
        return append_stream(connection, bytes, length) &&
               process_stream(server, connection, session, client_fd);
    }

    size_t offset = 0U;
    bool drain_cipher = true;
    while (offset < length || drain_cipher) {
        size_t consumed = 0U;
        size_t plaintext_len = 0U;
        const uint8_t *input = offset < length ? bytes + offset : NULL;
        size_t input_len = offset < length ? length - offset : 0U;
        airplay_control_cipher_result_t result = airplay_control_cipher_decrypt(
            connection->cipher, input, input_len, &consumed,
            connection->response, sizeof(connection->response), &plaintext_len);
        offset += consumed;
        if (plaintext_len != 0U &&
            (!append_stream(connection, connection->response, plaintext_len) ||
             !process_stream(server, connection, session, client_fd))) {
            return false;
        }
        if (result == AIRPLAY_CONTROL_CIPHER_OUTPUT_TOO_SMALL) {
            drain_cipher = true;
            continue;
        }
        if (result == AIRPLAY_CONTROL_CIPHER_OK ||
            result == AIRPLAY_CONTROL_CIPHER_INCOMPLETE) {
            if (offset < length) {
                if (consumed == 0U) return false;
                drain_cipher = false;
                continue;
            }
            break;
        }
        probe_receiver_event(106,result,length);
        ESP_LOGW(TAG, "rtsp_cipher=decrypt_failed result=%d", (int)result);
        return false;
    }
    return true;
}

static void serve_client(airplay_rtsp_server_t *server, int client_fd)
{
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 20000};
    int no_delay = 1;
    (void)setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &no_delay,
                     sizeof(no_delay));
    (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     sizeof(timeout));
    (void)setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                     sizeof(timeout));

    rtsp_connection_t *connection = server_calloc(1U, sizeof(*connection));
    airplay_rtsp_session_t *session = NULL;
    portENTER_CRITICAL(&s_server_mux);
    server->event_connected = false;
    server->event_key_used = false;
    server->event_failed = false;
    server->event_stopping = false;
    server->record_started=server->mode_observed=server->ui_requested=server->response_overflow=false;
    server->response_count=0;
    server->activation=(airplay_activation_t){0};server->last_activation_stage=ACT_IDLE;
    portEXIT_CRITICAL(&s_server_mux);
    if (connection == NULL || create_session(server, &session) !=
                                  AIRPLAY_RTSP_SESSION_OK) {
        ESP_LOGE(TAG, "rtsp_session=create_failed free_internal=%u free_psram=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        airplay_rtsp_session_destroy(session);
        server_free(connection, sizeof(*connection));
        return;
    }

    ESP_LOGI(TAG, "rtsp_client=connected free_internal=%u free_psram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    probe_receiver_connection(true);
    uint8_t rx[RTSP_SERVER_RX_CHUNK_BYTES];
    while (!server->stop_requested) {
        advance_activation(server);
        int received = recv(client_fd, rx, sizeof(rx), 0);
        if (received < 0 && errno == EINTR) continue;
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (received == 0) {
            probe_receiver_event(102,0,0);
            ESP_LOGI(TAG, "rtsp_client=peer_closed stage=control");
            break;
        }
        if (received < 0) {
            probe_receiver_event(103,errno,0);
            ESP_LOGW(TAG, "rtsp_client=receive_failed errno=%d", errno);
            break;
        }
        if (!process_received_bytes(server, connection, session, client_fd,
                                    rx, (size_t)received)) {
            break;
        }
    }
    portENTER_CRITICAL(&s_server_mux);server->record_started=false;server->response_count=0;portEXIT_CRITICAL(&s_server_mux);
    probe_receiver_connection(false);
    mbedtls_platform_zeroize(rx, sizeof(rx));
    airplay_control_cipher_destroy(connection->cipher);
    airplay_rtsp_session_destroy(session);
    server_free(connection, sizeof(*connection));
    ESP_LOGI(TAG, "rtsp_client=disconnected stack_hwm=%u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static bool wait_for_client(int listener_fd)
{
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listener_fd, &read_fds);
    struct timeval timeout = {
        .tv_sec = RTSP_SERVER_POLL_TIMEOUT_MS / 1000U,
        .tv_usec = (RTSP_SERVER_POLL_TIMEOUT_MS % 1000U) * 1000U,
    };
    int selected = select(listener_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (selected < 0 && errno != EINTR) return false;
    return selected > 0 && FD_ISSET(listener_fd, &read_fds);
}

static void rtsp_server_task(void *argument)
{
    airplay_rtsp_server_t *server = argument;
    while (!server->stop_requested) {
        if (!wait_for_client(server->listener_fd)) continue;
        struct sockaddr_in6 peer;
        socklen_t peer_len = sizeof(peer);
        int client_fd = accept(server->listener_fd,
                               (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (!server->stop_requested) {
                ESP_LOGW(TAG, "rtsp_accept=failed errno=%d", errno);
            }
            continue;
        }
        portENTER_CRITICAL(&s_server_mux);
        server->client_fd = client_fd;
        portEXIT_CRITICAL(&s_server_mux);
        ESP_LOGI(TAG, "rtsp_peer=%s:%u", "USB-IPv6",
                 (unsigned)ntohs(peer.sin6_port));
        serve_client(server, client_fd);
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        portENTER_CRITICAL(&s_server_mux);
        server->client_fd = -1;
        portEXIT_CRITICAL(&s_server_mux);
    }
    if (server->event_port != NULL) stop_event(server);
    if (server->timing_port != NULL) stop_timing(server);
    airplay_media_bridge_stop_screen_stream();
    if (server->listener_fd >= 0) {
        close(server->listener_fd);
        server->listener_fd = -1;
    }
    (void)xSemaphoreGive(server->stopped);
    vTaskDelete(NULL);
}

esp_err_t airplay_rtsp_server_start(
    const airplay_rtsp_server_config_t *config,
    airplay_rtsp_server_t **out_server)
{
    if (out_server == NULL) return ESP_ERR_INVALID_ARG;
    *out_server = NULL;
    if (config == NULL || config->info_plist == NULL ||
        config->info_plist_len == 0U ||
        config->info_plist_len > AIRPLAY_RTSP_SESSION_INFO_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    airplay_bplist_doc_t info_doc;
    airplay_bplist_object_t info_root;
    airplay_bplist_type_t info_type;
    if (airplay_bplist_open(config->info_plist, config->info_plist_len,
                            &info_doc) != AIRPLAY_BPLIST_OK ||
        airplay_bplist_root(&info_doc, &info_root) != AIRPLAY_BPLIST_OK ||
        airplay_bplist_get_type(&info_doc, info_root, &info_type, NULL) !=
            AIRPLAY_BPLIST_OK || info_type != AIRPLAY_BPLIST_DICTIONARY) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_server_mux);
    bool already_running = s_server != NULL;
    portEXIT_CRITICAL(&s_server_mux);
    if (already_running) return ESP_ERR_INVALID_STATE;

    esp_err_t control_result = airplay_control_port_init();
    if (control_result != ESP_OK) return control_result;
    int listener_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listener_fd < 0) return ESP_FAIL;
    int reuse = 1;
    (void)setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof(reuse));
    struct sockaddr_in6 address;
    if (!capture_usb_address(listener_fd,&address,config->control_port ? config->control_port : AIRPLAY_RTSP_DEFAULT_PORT)) {
        close(listener_fd);return ESP_ERR_INVALID_STATE;
    }
    if (bind(listener_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener_fd, 1) != 0) {
        ESP_LOGE(TAG, "rtsp_bind=failed errno=%d port=%u", errno,
                 (unsigned)ntohs(address.sin6_port));
        close(listener_fd);
        return ESP_FAIL;
    }
    socklen_t address_len = sizeof(address);
    if (getsockname(listener_fd, (struct sockaddr *)&address,
                    &address_len) != 0) {
        close(listener_fd);
        return ESP_FAIL;
    }

    airplay_rtsp_server_t *server = calloc(1U, sizeof(*server));
    if (server == NULL) {
        close(listener_fd);
        return ESP_ERR_NO_MEM;
    }
    server->config = *config;
    memcpy(server->info_plist, config->info_plist, config->info_plist_len);
    server->config.info_plist = server->info_plist;
    server->listener_fd = listener_fd;
    server->client_fd = -1;
    server->audio_fd = -1;
    server->keepalive_fd = -1;
    server->port = ntohs(address.sin6_port);
    server->stopped = xSemaphoreCreateBinary();
    if (server->stopped == NULL) {
        close(listener_fd);
        free(server);
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL(&s_server_mux);
    if (s_server != NULL) {
        portEXIT_CRITICAL(&s_server_mux);
        close(listener_fd);
        vSemaphoreDelete(server->stopped);
        mbedtls_platform_zeroize(server, sizeof(*server));
        free(server);
        return ESP_ERR_INVALID_STATE;
    }
    s_server = server;
    portEXIT_CRITICAL(&s_server_mux);
    if (xTaskCreate(rtsp_server_task, "airplay_rtsp",
                    RTSP_SERVER_TASK_STACK_BYTES, server,
                    RTSP_SERVER_TASK_PRIORITY, &server->task) != pdPASS) {
        portENTER_CRITICAL(&s_server_mux);
        s_server = NULL;
        portEXIT_CRITICAL(&s_server_mux);
        close(listener_fd);
        vSemaphoreDelete(server->stopped);
        mbedtls_platform_zeroize(server, sizeof(*server));
        free(server);
        return ESP_ERR_NO_MEM;
    }
    *out_server = server;
    ESP_LOGI(TAG, "rtsp_listener=ready usb_ipv6=1 port=%u free_internal=%u free_psram=%u",
             (unsigned)server->port,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return ESP_OK;
}

uint16_t airplay_rtsp_server_get_port(const airplay_rtsp_server_t *server)
{
    return server == NULL ? 0U : server->port;
}

bool airplay_rtsp_server_is_running(void)
{
    portENTER_CRITICAL(&s_server_mux);
    bool running = s_server != NULL && !s_server->stop_requested;
    portEXIT_CRITICAL(&s_server_mux);
    return running;
}

void airplay_rtsp_server_stop(airplay_rtsp_server_t *server)
{
    if (server == NULL) return;
    portENTER_CRITICAL(&s_server_mux);
    server->stop_requested = true;
    portEXIT_CRITICAL(&s_server_mux);
    (void)xSemaphoreTake(server->stopped, portMAX_DELAY);
    portENTER_CRITICAL(&s_server_mux);
    if (s_server == server) s_server = NULL;
    portEXIT_CRITICAL(&s_server_mux);
    vSemaphoreDelete(server->stopped);
    mbedtls_platform_zeroize(server, sizeof(*server));
    free(server);
}
