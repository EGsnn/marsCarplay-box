#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "probe_identity.h"
#include "probe_trace.h"
typedef struct {
    bool mounted;
    unsigned sessions,rx_bytes,tx_bytes,rx_frames,bad_frames,messages;
    int state;
    uint16_t last_message;
    unsigned control_count;
    uint8_t last_request,last_type;
    uint16_t last_value,last_index;
    char reason[56];
    char auth[24];
    bool signer_ready;
    char identity[24];
    unsigned net_rx,net_tx,net_drop,net_ipv6_rx;
    uint16_t net_filter;
    uint16_t message_history[8];
    probe_identity_rejection_t rejection;
    unsigned main_stack_min_bytes;
    char discovery_stage[24];
    int discovery_error;
    unsigned discovery_rx,discovery_tx,discovery_bad,discovery_responses;
    unsigned discovery_airplay_queries,discovery_control_ptr;
    uint16_t discovery_port;
    char receiver_stage[24];
    int receiver_error,receiver_http;
    unsigned receiver_requests,receiver_connects;
    unsigned captured_frames;
    bool receiver_listening,receiver_active;
    /* 0.6.0: event-channel RX diagnostics. reason: 0=connected 1=peer_closed
     * 2=receive_failed(errno) 3=decrypt/parse_fail(esp_err); head ring keeps
     * the first line(s) of the last few plaintext RTSP messages received on
     * the event channel so the iPhone's response text is observable.
     * 0.6.1: capture the FULL plaintext (status line + any headers/body) of
     * the last 2 messages, up to 512 B each, so an iPhone 400 body is visible. */
    unsigned event_rx_reason;
    int event_rx_errno;
    char event_rx_dump[2][512];
    size_t event_rx_dump_len[2];
    unsigned event_rx_head_index;
    /* 0.6.3: minimal audio sink counters (UDP receive only, no decode). */
    unsigned audio_rx_packets;
    unsigned audio_rx_bytes;
    /* Last actually sent SETUP metadata, retained after socket disconnect. */
    unsigned initial_setup_port, initial_setup_bytes;
    unsigned stream_setup_port, stream_setup_bytes, stream_setup_count;
    unsigned keepalive_rx_packets, keepalive_rx_bytes, keepalive_last_rx_ms;
    int keepalive_rx_error;
    struct {
        bool active;
        unsigned frames,configs,ignored,last_opcode,last_body_bytes,last_frame_ms;
        unsigned exit_event,exit_phase,exit_bytes,exit_ms,local_close_ms;
        int error;
    } screen;
    probe_trace_t trace;
} probe_status_t;
extern SemaphoreHandle_t probe_lock;
extern probe_status_t probe_status;
void probe_ui_draw_idle(void);
void probe_usb_install(void);
