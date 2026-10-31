/* Experimental authentication only. No CarPlay network/video capability claim. */
#include <string.h>
#include <stdio.h>
#include "probe_auth.h"
#include "probe.h"
#include "probe_identity.h"
#include "auth_port.h"
#include "iap2_mfi_auth.h"
#include "esp_timer.h"

static iap2_mfi_auth_t auth;
static bool certificate_sent, response_pending;
static uint8_t certificate[1540];
static uint8_t identity_body[512];
static bool identity_sent;
static void identity_status(const char *name)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    strlcpy(probe_status.identity,name,sizeof(probe_status.identity));
    xSemaphoreGive(probe_lock);
}
static void status(const char *name)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    strlcpy(probe_status.auth,name,sizeof(probe_status.auth));
    xSemaphoreGive(probe_lock);
}
static void failed(const char *why)
{
    iap2_mfi_auth_mark_failed(&auth);
    status(why);
}
void probe_auth_reset(void)
{
    iap2_mfi_auth_reset(&auth);
    certificate_sent=false;response_pending=false;
    identity_sent=false;identity_status("WAIT IDENTIFY");
    xSemaphoreTake(probe_lock,portMAX_DELAY);memset(&probe_status.rejection,0,sizeof(probe_status.rejection));xSemaphoreGive(probe_lock);
    status("WAIT CERT REQUEST");
}
static void response_acked(uint16_t id,void *unused)
{
    (void)unused;response_pending=false;
    if(id==0xaa03 && iap2_mfi_auth_mark_challenge_response_acked(&auth)) status("WAIT AUTH RESULT");
    else failed("AUTH ACK ERROR");
}
static bool challenge_parameter(const uint8_t *body,size_t length,const uint8_t **data,size_t *size)
{
    /* Exactly one parameter 0; reject duplicate/truncated/trailing data. */
    if(!body||length<4||body[2]!=0||body[3]!=0) return false;
    size_t total=((size_t)body[0]<<8)|body[1];
    if(total!=length||(total!=24&&total!=36)) return false;
    *data=body+4;*size=total-4;return true;
}
void probe_auth_message(iap2_core_t *core,uint16_t id,const uint8_t *body,size_t length)
{
    uint64_t now=esp_timer_get_time()/1000;
    if(id==0xaa00) {
        if(response_pending) {failed("AUTH BUSY");return;}
        iap2_mfi_auth_reset(&auth);certificate_sent=false;
        size_t count=sizeof(certificate)-4;
        if(auth_port_read_certificate(certificate+4,&count)!=ESP_OK||!count||count>sizeof(certificate)-4) {
            failed("CERT UNAVAILABLE");return;
        }
        size_t total=count+4;
        certificate[0]=total>>8;certificate[1]=total;certificate[2]=certificate[3]=0;
        if(!iap2_core_send_csm(core,0xaa01,certificate,total,now)) {failed("CERT SEND FAILED");return;}
        certificate_sent=true;status("CERT SENT");
    } else if(id==0xaa02) {
        const uint8_t *challenge;size_t count;
        uint8_t response[68];size_t signature_len=64;
        if(!certificate_sent||response_pending||!iap2_mfi_auth_can_respond_to_challenge(&auth)
            ||!challenge_parameter(body,length,&challenge,&count)) {failed("BAD CHALLENGE");return;}
        if(auth_port_sign_challenge(challenge,count,response+4,&signature_len)!=ESP_OK||signature_len!=64) {
            failed("SIGN FAILED");return;
        }
        response[0]=0;response[1]=68;response[2]=response[3]=0;
        if(!iap2_mfi_auth_mark_challenge_response_queued(&auth)) {failed("AUTH STATE ERROR");return;}
        response_pending=true;
        if(!iap2_core_send_csm_with_ack_callback(core,0xaa03,response,sizeof(response),response_acked,NULL,now)) {
            response_pending=false;failed("SIGN SEND FAILED");return;
        }
        status("SIGN SENT");
    } else if(id==0xaa04) {
        failed("PHONE REJECTED");
    } else if(id==0xaa05) {
        if(certificate_sent&&iap2_mfi_auth_mark_authenticated(&auth)) status("AUTH ACCEPTED");
        else failed("AUTH ORDER ERROR");
    } else if(id==0x1d00) {
        size_t used=0;
        identity_sent=false;
        xSemaphoreTake(probe_lock,portMAX_DELAY);memset(&probe_status.rejection,0,sizeof(probe_status.rejection));xSemaphoreGive(probe_lock);
        if(!probe_identity_build(identity_body,sizeof(identity_body),&used)
           ||!iap2_core_send_csm(core,0x1d01,identity_body,used,now))identity_status("IDENTIFY SEND FAILED");
        else {identity_sent=true;identity_status("IDENTIFY SENT");}
    } else if(id==0x1d02) {
        identity_status(identity_sent?"IDENTIFY ACCEPTED":"IDENTIFY ORDER ERROR");
    } else if(id==0x1d03) {
        probe_identity_rejection_t r;
        bool valid=probe_identity_parse_rejection(body,length,&r);
        xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.rejection=r;xSemaphoreGive(probe_lock);
        char line[24];
        if(!valid)strlcpy(line,"ID REJECT MALFORMED",sizeof(line));
        else if(!r.field_count)strlcpy(line,"ID REJECT EMPTY",sizeof(line));
        else if(r.sent_count||r.received_count)snprintf(line,sizeof(line),"ID REJ F%u M%04X",r.fields[0],r.sent_count?r.sent[0]:r.received[0]);
        else snprintf(line,sizeof(line),"ID REJ F%u",r.fields[0]);
        identity_status(line);
    }
}
