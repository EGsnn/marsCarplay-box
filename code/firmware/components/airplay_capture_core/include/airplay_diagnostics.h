/* Numeric receiver diagnostics; keep event IDs unique across tasks. */
#pragma once
enum {
    PROBE_EVENT_READINESS = 120,
    PROBE_EVENT_KEEPALIVE_READY = 140,
    PROBE_EVENT_KEEPALIVE_START_FAILED = 141,
    PROBE_EVENT_INITIAL_SETUP_SENT = 142,
    PROBE_EVENT_STREAM_SETUP_SENT = 143,
    PROBE_EVENT_KEEPALIVE_RX = 144,
    PROBE_EVENT_KEEPALIVE_RX_FAILED = 145,
    PROBE_EVENT_SETUP_BRANCH = 146,
    PROBE_EVENT_SCREEN_CONNECTED = 150,
    PROBE_EVENT_SCREEN_HEADER = 151,
    PROBE_EVENT_SCREEN_FRAME = 152,
    PROBE_EVENT_SCREEN_CONFIG = 153,
    PROBE_EVENT_SCREEN_IGNORED = 154,
    PROBE_EVENT_SCREEN_EOF = 155,
    PROBE_EVENT_SCREEN_RECV_FAILED = 156,
    PROBE_EVENT_SCREEN_HEADER_FAILED = 157,
    PROBE_EVENT_SCREEN_ALLOC_FAILED = 158,
    PROBE_EVENT_SCREEN_PROCESS_FAILED = 159,
    PROBE_EVENT_SCREEN_CLOSE_RTSP = 160,
    PROBE_EVENT_SCREEN_STOPPED = 161,
    PROBE_EVENT_SCREEN_DISCARDED = 162,
    PROBE_EVENT_SCREEN_ACCEPT_FAILED = 163,
};
/* Header/frame progress is stored persistently; trace entries are rate limited.
 * On receive failures opcode is the phase: 1=header, 2=video body, 3=ignored body. */
void probe_receiver_screen_note(unsigned event,unsigned opcode,unsigned bytes,int detail);
