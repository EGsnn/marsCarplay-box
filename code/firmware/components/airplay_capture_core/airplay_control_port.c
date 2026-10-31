/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 ESP32 CarPlay Web Relay contributors
 * Event RTSP and HID command framing follows DiPlay's AirPlaySession.kt.
 * See THIRD_PARTY_NOTICES.md and COPYING for attribution and terms.
 */
#include "airplay_control_port.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airplay_command_plist.h"
#include "airplay_bplist.h"
#include "airplay_commands.h"
#include "airplay_hid_plist.h"
#include "airplay_control_stream.h"
#include "airplay_control_memory.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"

#ifdef ESP_PLATFORM
/* Event-channel diagnostics surfaced by the main app probe receiver. */
extern void probe_receiver_event(unsigned,int,unsigned);
extern void probe_receiver_event_rx_note(const uint8_t*,size_t,unsigned,int);
#endif

#define AIRPLAY_CONTROL_MAX_PLAINTEXT 512U
#define AIRPLAY_CONTROL_RESPONSE_BYTES 256U
#define AIRPLAY_KEYFRAME_REQUEST_INTERVAL_MS 250U

typedef struct {
    uint32_t stream_id;
    uint32_t cseq;
    airplay_control_write_cb_t write_cb;
    void *context;
    airplay_control_cipher_t *cipher;
    airplay_control_stream_t *stream;
    uint8_t *plaintext_scratch;
} airplay_control_session_t;

static SemaphoreHandle_t s_session_mutex;
static airplay_control_session_t *s_session;
static TickType_t s_keyframe_request_tick;
static bool s_keyframe_request_tick_valid;

static void destroy_session(airplay_control_session_t *session)
{
    if (session == NULL) return;
    airplay_control_cipher_destroy(session->cipher);
    airplay_control_stream_destroy(session->stream);
    if (session->plaintext_scratch != NULL) {
        mbedtls_platform_zeroize(session->plaintext_scratch,
                                 AIRPLAY_CONTROL_CIPHER_MAX_FRAME_BYTES);
        free(session->plaintext_scratch);
    }
    mbedtls_platform_zeroize(session, sizeof(*session));
    free(session);
}

esp_err_t airplay_control_port_init(void)
{
    if (s_session_mutex != NULL) return ESP_OK;
    s_session_mutex = xSemaphoreCreateMutex();
    return s_session_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t airplay_control_port_attach(
    const uint8_t read_key[AIRPLAY_CONTROL_KEY_BYTES],
    const uint8_t write_key[AIRPLAY_CONTROL_KEY_BYTES],
    uint32_t stream_id,
    uint32_t initial_cseq,
    airplay_control_write_cb_t write_cb,
    void *context)
{
    if (read_key == NULL || write_key == NULL || write_cb == NULL ||
        s_session_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    airplay_control_session_t *session = calloc(1U, sizeof(*session));
    if (session == NULL) return ESP_ERR_NO_MEM;
    if (airplay_control_cipher_create(read_key, write_key, &session->cipher) !=
        AIRPLAY_CONTROL_CIPHER_OK) {
        destroy_session(session);
        return ESP_FAIL;
    }
    airplay_control_stream_result_t stream_result =
        airplay_control_stream_create(&session->stream);
    if (stream_result != AIRPLAY_CONTROL_STREAM_OK) {
        destroy_session(session);
        return stream_result == AIRPLAY_CONTROL_STREAM_NO_MEMORY
                   ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    session->plaintext_scratch = airplay_control_calloc(
        AIRPLAY_CONTROL_CIPHER_MAX_FRAME_BYTES, 1U);
    if (session->plaintext_scratch == NULL) {
        destroy_session(session);
        return ESP_ERR_NO_MEM;
    }
    session->stream_id = stream_id;
    session->cseq = initial_cseq;
    session->write_cb = write_cb;
    session->context = context;

    (void)xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    airplay_control_session_t *previous = s_session;
    s_session = session;
    s_keyframe_request_tick_valid = false;
    (void)xSemaphoreGive(s_session_mutex);
    destroy_session(previous);
    return ESP_OK;
}

void airplay_control_port_detach(void)
{
    if (s_session_mutex == NULL) return;
    (void)xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    airplay_control_session_t *session = s_session;
    s_session = NULL;
    s_keyframe_request_tick_valid = false;
    (void)xSemaphoreGive(s_session_mutex);
    destroy_session(session);
}

static esp_err_t send_plaintext_locked(airplay_control_session_t *session,
                                       const uint8_t *plaintext,
                                       size_t plaintext_len)
{
    uint8_t frame[AIRPLAY_CONTROL_MAX_PLAINTEXT +
                  AIRPLAY_CONTROL_CIPHER_OVERHEAD_BYTES];
    size_t frame_len = 0U;
    airplay_control_cipher_result_t cipher_result = airplay_control_cipher_encrypt(
        session->cipher, plaintext, plaintext_len, frame, sizeof(frame),
        &frame_len);
    if (cipher_result != AIRPLAY_CONTROL_CIPHER_OK) {
        mbedtls_platform_zeroize(frame, sizeof(frame));
        return ESP_FAIL;
    }
    bool written = session->write_cb(frame, frame_len, session->context);
    mbedtls_platform_zeroize(frame, sizeof(frame));
    return written ? ESP_OK : ESP_FAIL;
}

static bool send_event_ack(const airplay_rtsp_request_t *request,
                           void *context)
{
    airplay_command_t command;
    unsigned code=(!strcmp(request->method,"POST")&&!strcmp(request->path,"/command"))?
        airplay_command_parse(request->body,request->body_len,&command):405;
#if defined(ESP_PLATFORM) || defined(AIRPLAY_CONTROL_OBSERVER_TEST)
    extern void probe_receiver_event(unsigned,int,unsigned);
    probe_receiver_event(136,code==200?(int)command.kind:-(int)code,request->body_len);
    /* 0.6.5: dump the rejected /command body verbatim so the parse-failure
     * branch (a bplist structure the local parser rejects) can be read on
     * the host instead of only seeing EVENT_RESP code=400. */
    if(code!=200){
        extern void probe_receiver_event_rx_note(const uint8_t*,size_t,unsigned,int);
        probe_receiver_event_rx_note(request->body,request->body_len,0,0);
    }
#endif
    airplay_control_session_t *session = context;
    uint8_t response[AIRPLAY_CONTROL_RESPONSE_BYTES];
    size_t response_len = 0U;
    uint8_t body[48];size_t body_len=0;
    if(code==200&&airplay_bplist_build_empty_dictionary(body,sizeof(body),&body_len)!=AIRPLAY_BPLIST_OK)return false;
    int rc=airplay_rtsp_build_response(request,code,code==200?"OK":code==400?"Bad Request":code==501?"Not Implemented":"Method Not Allowed",
                         code==200?"application/x-apple-binary-plist":NULL,
                         code==200?body:NULL,body_len,response,sizeof(response),&response_len);
    if (rc != 0) {
        mbedtls_platform_zeroize(response, sizeof(response));
        return false;
    }
    esp_err_t result = send_plaintext_locked(session, response, response_len);
    mbedtls_platform_zeroize(response, sizeof(response));
#if defined(ESP_PLATFORM) || defined(AIRPLAY_CONTROL_OBSERVER_TEST)
    if(result==ESP_OK&&code==200&&command.kind==1&&command.screen_entity_known){
        extern void airplay_rtsp_server_modes_observed(void);
        airplay_rtsp_server_modes_observed();
    }
    /* 0.6.7: the phone itself posts requestUI; accept it and take the screen
     * (DiPlay AirPlaySession.kt:764 handles phone-initiated requestUI). */
    if(result==ESP_OK&&code==200&&command.kind==2){
        extern void airplay_rtsp_server_ui_requested(void);
        airplay_rtsp_server_ui_requested();
    }
#endif
    return result == ESP_OK;
}

static esp_err_t process_event_plaintext_locked(
    airplay_control_session_t *session,
    const uint8_t *plaintext,
    size_t plaintext_len)
{
    airplay_control_stream_result_t result = airplay_control_stream_feed(
        session->stream, plaintext, plaintext_len, send_event_ack, session);
    if (result == AIRPLAY_CONTROL_STREAM_OK ||
        result == AIRPLAY_CONTROL_STREAM_INCOMPLETE) return ESP_OK;
    return result == AIRPLAY_CONTROL_STREAM_TOO_LARGE
               ? ESP_ERR_INVALID_SIZE
               : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t airplay_control_port_receive_encrypted(const uint8_t *bytes,
                                                 size_t len)
{
    if ((bytes == NULL && len != 0U) || s_session_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    airplay_control_session_t *session = s_session;
    if (session == NULL) {
        (void)xSemaphoreGive(s_session_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    size_t offset = 0U;
    esp_err_t result = ESP_OK;
    do {
        size_t consumed = 0U;
        size_t plaintext_len = 0U;
        airplay_control_cipher_result_t cipher_result =
            airplay_control_cipher_decrypt(
                session->cipher, bytes == NULL ? NULL : bytes + offset,
                len - offset, &consumed, session->plaintext_scratch,
                AIRPLAY_CONTROL_CIPHER_MAX_FRAME_BYTES,
                &plaintext_len);
        offset += consumed;
        if (plaintext_len != 0U) {
#ifdef ESP_PLATFORM
            probe_receiver_event_rx_note(session->plaintext_scratch,
                                         plaintext_len, 0, 0);
#endif
            result = process_event_plaintext_locked(session,
                                                    session->plaintext_scratch,
                                                    plaintext_len);
        }
        mbedtls_platform_zeroize(session->plaintext_scratch,
                                 AIRPLAY_CONTROL_CIPHER_MAX_FRAME_BYTES);
        if (result != ESP_OK) break;
        if (cipher_result == AIRPLAY_CONTROL_CIPHER_OUTPUT_TOO_SMALL) continue;
        if (cipher_result == AIRPLAY_CONTROL_CIPHER_INCOMPLETE ||
            cipher_result == AIRPLAY_CONTROL_CIPHER_OK) {
            if (offset == len) break;
            if (consumed == 0U) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            continue;
        }
        result = ESP_ERR_INVALID_RESPONSE;
        break;
    } while (offset <= len);

    if (result != ESP_OK) {
        s_session = NULL;
        destroy_session(session);
    }
    (void)xSemaphoreGive(s_session_mutex);
    return result;
}

bool airplay_control_port_send_hid_report(uint32_t hid_uid,
                                          const uint8_t *report,
                                          size_t report_len,
                                          void *context)
{
    (void)context;
    if (report == NULL || report_len == 0U || report_len > 128U ||
        s_session_mutex == NULL) {
        return false;
    }
    uint8_t plaintext[AIRPLAY_CONTROL_MAX_PLAINTEXT];
    size_t plist_len = 0U;
    char header[192];
    bool sent = false;

    if (!airplay_hid_plist_encode(hid_uid, report, report_len, plaintext,
                                  sizeof(plaintext), &plist_len)) return false;
    (void)xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    airplay_control_session_t *session = s_session;
    if (session == NULL) goto done;

    ++session->cseq;
    if (session->cseq == 0U) session->cseq = 1U;
    int header_len;
    if (session->stream_id == 0U) {
        header_len = snprintf(header, sizeof(header),
                              "POST /command RTSP/1.0\r\n"
                              "Content-Type: application/x-apple-binary-plist\r\n"
                              "Content-Length: %u\r\n"
                              "CSeq: %" PRIu32 "\r\n\r\n",
                              (unsigned)plist_len, session->cseq);
    } else {
        header_len = snprintf(header, sizeof(header),
                              "POST /command RTSP/1.0\r\n"
                              "X-Apple-StreamID: %" PRIu32 "\r\n"
                              "Content-Type: application/x-apple-binary-plist\r\n"
                              "Content-Length: %u\r\n"
                              "CSeq: %" PRIu32 "\r\n\r\n",
                              session->stream_id, (unsigned)plist_len,
                              session->cseq);
    }
    if (header_len <= 0 ||
        (size_t)header_len + plist_len > AIRPLAY_CONTROL_MAX_PLAINTEXT) {
        goto detach;
    }
    memmove(plaintext + header_len, plaintext, plist_len);
    memcpy(plaintext, header, (size_t)header_len);
    sent = send_plaintext_locked(session, plaintext,
                                 (size_t)header_len + plist_len) == ESP_OK;
    if (!sent) goto detach;
    goto done;

detach:
    s_session = NULL;
    destroy_session(session);
done:
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    mbedtls_platform_zeroize(header, sizeof(header));
    (void)xSemaphoreGive(s_session_mutex);
    return sent;
}

bool airplay_control_port_request_main_screen_keyframe(void)
{
    if (s_session_mutex == NULL) return false;

    uint8_t plaintext[AIRPLAY_CONTROL_MAX_PLAINTEXT] = {0};
    uint8_t body[96U] = {0};
    char header[192] = {0};
    size_t body_len = 0U;
    bool sent = false;
    if (!airplay_command_plist_encode_force_keyframe(
            body, sizeof(body), &body_len)) {
        goto done_without_lock;
    }

    (void)xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    airplay_control_session_t *session = s_session;
    if (session == NULL) goto done;

    TickType_t now = xTaskGetTickCount();
    TickType_t min_interval = pdMS_TO_TICKS(AIRPLAY_KEYFRAME_REQUEST_INTERVAL_MS);
    if (min_interval == 0U) min_interval = 1U;
    if (s_keyframe_request_tick_valid &&
        (TickType_t)(now - s_keyframe_request_tick) < min_interval) {
        sent = true;
        goto done;
    }

    ++session->cseq;
    if (session->cseq == 0U) session->cseq = 1U;
    int header_len = snprintf(header, sizeof(header),
                              "POST /command RTSP/1.0\r\n"
                              "Content-Type: application/x-apple-binary-plist\r\n"
                              "Content-Length: %u\r\n"
                              "CSeq: %" PRIu32 "\r\n\r\n",
                              (unsigned)body_len, session->cseq);
    if (header_len <= 0 ||
        (size_t)header_len + body_len > sizeof(plaintext)) {
        goto detach;
    }
    memcpy(plaintext, header, (size_t)header_len);
    memcpy(plaintext + header_len, body, body_len);
    sent = send_plaintext_locked(session, plaintext,
                                 (size_t)header_len + body_len) == ESP_OK;
    if (!sent) goto detach;
    s_keyframe_request_tick = now;
    s_keyframe_request_tick_valid = true;
    goto done;

detach:
    s_session = NULL;
    s_keyframe_request_tick_valid = false;
    destroy_session(session);
done:
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    mbedtls_platform_zeroize(body, sizeof(body));
    mbedtls_platform_zeroize(header, sizeof(header));
    (void)xSemaphoreGive(s_session_mutex);
    return sent;

done_without_lock:
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    mbedtls_platform_zeroize(body, sizeof(body));
    mbedtls_platform_zeroize(header, sizeof(header));
    return false;
}
bool airplay_control_port_send_command(const uint8_t *body,size_t size,
                                       uint32_t *cseq_used)
{
    if(!s_session_mutex||!body||!size||size>320)return false;
    uint8_t plain[AIRPLAY_CONTROL_MAX_PLAINTEXT]={0};bool sent=false;
    xSemaphoreTake(s_session_mutex,portMAX_DELAY);
    airplay_control_session_t *session=s_session;
    if(!session)goto done;
    ++session->cseq;if(!session->cseq)session->cseq=1;
    if(cseq_used)*cseq_used=session->cseq;
    int n=snprintf((char *)plain,sizeof(plain),"POST /command RTSP/1.0\r\nContent-Type: application/x-apple-binary-plist\r\nContent-Length: %u\r\nCSeq: %" PRIu32 "\r\n\r\n",(unsigned)size,session->cseq);
    if(n<0||(size_t)n+size>sizeof(plain))goto done;
    memcpy(plain+n,body,size);
    sent=send_plaintext_locked(session,plain,(size_t)n+size)==ESP_OK;
    if(!sent){s_session=NULL;destroy_session(session);}
done:
    xSemaphoreGive(s_session_mutex);mbedtls_platform_zeroize(plain,sizeof(plain));return sent;
}
