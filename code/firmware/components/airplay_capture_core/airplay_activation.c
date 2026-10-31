/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "airplay_activation.h"
/* 0.7.1: give the phone up to 5s to complete the focus handshake. */
#define ACT_DEADLINE_MS 5000U
void airplay_activation_begin(airplay_activation_t *s,uint64_t now)
{*s=(airplay_activation_t){.stage=ACT_UI_READY,.deadline=now+ACT_DEADLINE_MS};}
unsigned airplay_activation_next(airplay_activation_t *s,uint64_t now)
{
    if(s->stage!=ACT_IDLE&&s->stage!=ACT_DONE&&s->stage!=ACT_FAILED&&now>=s->deadline){s->stage=ACT_FAILED;return 0;}
    if(s->stage==ACT_UI_READY){
        /* 0.6.7: wait up to the deadline for the phone to initiate requestUI
         * (DiPlay/C-reference: requestUI is posted BY the phone). Only fall
         * back to sending our own requestUI after the window expires. */
        if(now>=s->deadline){s->stage=ACT_UI_WAIT;s->cseq=0;return 1;}
        return 0;
    }
    if(s->stage==ACT_TAKE_READY){s->stage=ACT_TAKE_WAIT;s->cseq=0;return 2;}
    return 0;
}
void airplay_activation_sent(airplay_activation_t *s,bool ok,uint32_t cseq)
{if(s->stage==ACT_UI_WAIT||s->stage==ACT_TAKE_WAIT){if(!ok||!cseq)s->stage=ACT_FAILED;else s->cseq=cseq;}}
void airplay_activation_modes(airplay_activation_t *s)
{
    /* 0.7.1: the phone posts modesChanged as the activation completion signal
     * (c_flow: "modesChanged ACCEPTED" ends the handshake). In 0.7.0 we stayed
     * in TAKE_WAIT waiting for a TakeScreen ack the phone never sends, then
     * timed out to FAILED. Now modes on TAKE_WAIT completes the activation. */
    if(s->stage!=ACT_IDLE&&s->stage!=ACT_FAILED){
        s->modes_seen=true;
        if(s->stage==ACT_MODES_WAIT)s->stage=ACT_TAKE_READY;
        else if(s->stage==ACT_TAKE_WAIT)s->stage=ACT_DONE;
    }
}
/* 0.6.7: the phone initiated requestUI (POST /command, accepted 200) or the
 * phone acked our requestUI; take the screen right away. */
void airplay_activation_ui_requested(airplay_activation_t *s)
{if(s->stage==ACT_UI_READY||s->stage==ACT_UI_WAIT)s->stage=ACT_TAKE_READY;}
void airplay_activation_response(airplay_activation_t *s,unsigned code,int cseq,bool body_ok)
{
    if((s->stage!=ACT_UI_WAIT&&s->stage!=ACT_TAKE_WAIT)||!s->cseq)return;
    /* Missing CSeq is accepted only for our sole in-flight command. */
    if(cseq==-2||(cseq>=0&&(uint32_t)cseq!=s->cseq))return;
    if(code<200)return;
    if(code!=200||!body_ok){s->stage=ACT_FAILED;return;}
    /* 0.6.6 (C-reference flow): requestUI acked -> take the screen immediately.
     * The phone waits for TakeScreen and posts modesChanged afterwards
     * (c_flow_chatty.log:613 sent requestUI -> 615 phone POST /command
     * requestUI -> 625 TakeScreen -> 650-670 phone POST modesChanged).
     * 0.6.3 evidence: waiting for modesChanged before TakeScreen deadlocks -
     * phone never posts modesChanged because it is waiting for TakeScreen. */
    s->stage=s->stage==ACT_UI_WAIT?ACT_TAKE_READY:ACT_DONE;
}
