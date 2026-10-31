/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded parameter encoding adapted from the pinned relay iap2_identity.
 * USB group field mapping checked against pinned CatPlay IdentificationInformation.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "probe_identity.h"
static bool parameter(uint8_t *out,size_t capacity,size_t *used,uint16_t id,const void *data,size_t size)
{
    if(size>65531||*used>capacity||size+4>capacity-*used)return false;
    size_t pos=*used;out[pos]=(size+4)>>8;out[pos+1]=size+4;
    out[pos+2]=id>>8;out[pos+3]=id;
    if(size)memcpy(out+pos+4,data,size);
    *used+=size+4;return true;
}
bool probe_identity_build(uint8_t *out,size_t capacity,size_t *length)
{
    if(!out||!length)return false;
    *length=0;
    const char *strings[]={"ATK BOX CarPlay","ATK-DNESP32S3-BOX","MarsSnow","ATK-BOX-0001","0.1.8-public-source","ESP32-S3"};
    for(unsigned id=0;id<6;id++)if(!parameter(out,capacity,length,id,strings[id],strlen(strings[id])+1))return false;
    /* Only implemented messages; no fabricated Bluetooth, GPS, EA or Wi-Fi transport. */
    const uint8_t sent[]={0xaa,0x01,0xaa,0x03};
    const uint8_t received[]={0xaa,0x00,0xaa,0x02,0xaa,0x04,0xaa,0x05};
    /* F9 is the legacy iAP2 Accessory Power declaration, not USB bMaxPower.
     * This USB-C board has no legacy Accessory Power pin. Use 0 as in the
     * pinned CatPlay/carplayd USB receiver identities. USB VBUS consumption
     * remains declared separately by the configuration descriptor (500mA).
     * Phone acceptance of this correction still requires a real-device test. */
    uint8_t power=0,current[]={0,0};
    if(!parameter(out,capacity,length,6,sent,sizeof(sent))||!parameter(out,capacity,length,7,received,sizeof(received))
       ||!parameter(out,capacity,length,8,&power,1)||!parameter(out,capacity,length,9,current,2)
       ||!parameter(out,capacity,length,12,"en",3)||!parameter(out,capacity,length,13,"en",3))return false;
    uint8_t usb[96];size_t used=0;
    const uint8_t identifier[]={0x03,0xe9},ncm_control=1;
    if(!parameter(usb,sizeof(usb),&used,0,identifier,2)||!parameter(usb,sizeof(usb),&used,1,"USB_USE",8)
       ||!parameter(usb,sizeof(usb),&used,2,NULL,0)||!parameter(usb,sizeof(usb),&used,3,&ncm_control,1)
       ||!parameter(usb,sizeof(usb),&used,4,NULL,0))return false;
    /* CatPlay uses USBHostTransport for this phone-host/accessory-gadget topology.
     * CarPlay transport flag enables the NCM experiment; video service is absent. */
    return parameter(out,capacity,length,16,usb,used);
}

/* Only rejected field numbers, lengths and parameter 6/7 BE16 message IDs are
 * retained. Never retain arbitrary control-message bodies or identity strings.
 * Diagnostic layout checked against DiPlay Iap2IdentificationClient.kt. */
bool probe_identity_parse_rejection(const uint8_t *body,size_t length,probe_identity_rejection_t *r)
{
    if(!r)return false;
    memset(r,0,sizeof(*r));r->present=true;r->bytes=length;
    if(!body&&length){r->malformed=true;return false;}
    size_t pos=0;
    while(pos<length){
        if(length-pos<4){r->malformed=true;return false;}
        size_t size=((size_t)body[pos]<<8)|body[pos+1];
        uint16_t id=((uint16_t)body[pos+2]<<8)|body[pos+3];
        if(size<4||size>length-pos){r->malformed=true;return false;}
        if(r->field_count<PROBE_REJECTION_LIMIT){
            r->fields[r->field_count]=id;r->sizes[r->field_count++]=size-4;
        }else r->truncated=true;
        if(id==6||id==7){
            if((size-4)%2){r->malformed=true;return false;}
            uint16_t *ids=id==6?r->sent:r->received;
            size_t *count=id==6?&r->sent_count:&r->received_count;
            for(size_t i=pos+4;i<pos+size;i+=2){
                if(*count<PROBE_REJECTION_LIMIT)ids[(*count)++]=((uint16_t)body[i]<<8)|body[i+1];
                else r->truncated=true;
            }
        }
        pos+=size;
    }
    return true;
}
static bool json_append(char *out,size_t cap,size_t *used,const char *fmt,...)
{
    if(*used>=cap)return false;
    va_list args;va_start(args,fmt);int n=vsnprintf(out+*used,cap-*used,fmt,args);va_end(args);
    if(n<0||(size_t)n>=cap-*used)return false;
    *used+=(size_t)n;return true;
}
bool probe_identity_rejection_json(const probe_identity_rejection_t *r,char *out,size_t cap)
{
    if(!r||!out||!cap)return false;
    out[0]=0;size_t used=0;
    if(r->field_count>PROBE_REJECTION_LIMIT||r->sent_count>PROBE_REJECTION_LIMIT||r->received_count>PROBE_REJECTION_LIMIT)return false;
    if(!json_append(out,cap,&used,"{\"present\":%s,\"bytes\":%u,\"malformed\":%s,\"truncated\":%s",r->present?"true":"false",(unsigned)r->bytes,r->malformed?"true":"false",r->truncated?"true":"false"))goto fail;
    const char *names[]={"fields","sizes","sent","received"};
    const uint16_t *arrays[]={r->fields,r->sizes,r->sent,r->received};
    size_t counts[]={r->field_count,r->field_count,r->sent_count,r->received_count};
    for(unsigned a=0;a<4;a++){
        if(!json_append(out,cap,&used,",\"%s\":[",names[a]))goto fail;
        for(size_t i=0;i<counts[a];i++)if(!json_append(out,cap,&used,"%s%u",i?",":"",arrays[a][i]))goto fail;
        if(!json_append(out,cap,&used,"]"))goto fail;
    }
    if(!json_append(out,cap,&used,"}"))goto fail;
    return true;
fail:out[0]=0;return false;
}
