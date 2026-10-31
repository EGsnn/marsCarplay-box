/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "airplay_commands.h"
#include "airplay_bplist.h"
#include <string.h>
static bool dictionary(const airplay_bplist_doc_t *d,airplay_bplist_object_t o)
{airplay_bplist_type_t t;return airplay_bplist_get_type(d,o,&t,NULL)==AIRPLAY_BPLIST_OK&&t==AIRPLAY_BPLIST_DICTIONARY;}
unsigned airplay_command_parse(const uint8_t *body,size_t len,airplay_command_t *out)
{
    if(!out)return 400;
    memset(out,0,sizeof(*out));
    airplay_bplist_doc_t d;airplay_bplist_object_t root,o,params;
    char type[40];size_t n=0;
    if(len>2048||airplay_bplist_open(body,len,&d)!=AIRPLAY_BPLIST_OK||
       airplay_bplist_root(&d,&root)!=AIRPLAY_BPLIST_OK||!dictionary(&d,root)||
       airplay_bplist_dict_get(&d,root,"type",&o)!=AIRPLAY_BPLIST_OK||
       airplay_bplist_get_string(&d,o,type,sizeof(type),&n)!=AIRPLAY_BPLIST_OK)return 400;
    /* Reject embedded NULs rather than accepting a known-name prefix. */
    if(n!=strlen(type))return 400;
    unsigned kind=!strcmp(type,"modesChanged")?1:!strcmp(type,"requestUI")?2:0;
    airplay_bplist_result_t p=airplay_bplist_dict_get(&d,root,"params",&params);
    if(p!=AIRPLAY_BPLIST_NOT_FOUND&&(p!=AIRPLAY_BPLIST_OK||!dictionary(&d,params)))return 400;
    /* 0.6.9: unknown commands get a lenient 200 like DiPlay's onCommand -
     * the phone posts e.g. enhancedRequestCarUI/startSession after modes and
     * a 501 Not Implemented tears the session down (0.6.8 trace: 115B POST
     * /command -> 501 -> iPhone disconnect). */
    if(!kind){out->kind=0;return 200;}
    if(kind==2){out->kind=2;return 200;}
    if(p!=AIRPLAY_BPLIST_OK)return 400;
    airplay_command_t next={.kind=1};
    airplay_bplist_object_t list;airplay_bplist_type_t t;uint64_t count=0;
    p=airplay_bplist_dict_get(&d,params,"resources",&list);
    if(p==AIRPLAY_BPLIST_OK){
        if(airplay_bplist_get_type(&d,list,&t,&count)!=AIRPLAY_BPLIST_OK||t!=AIRPLAY_BPLIST_ARRAY||count>8)return 400;
        for(uint64_t i=0;i<count;i++){
            airplay_bplist_object_t r,id,e;uint64_t rid=0,entity=0;
            if(airplay_bplist_array_get(&d,list,i,&r)!=AIRPLAY_BPLIST_OK||!dictionary(&d,r)||
               airplay_bplist_dict_get(&d,r,"resourceID",&id)!=AIRPLAY_BPLIST_OK||
               airplay_bplist_get_uint(&d,id,&rid)!=AIRPLAY_BPLIST_OK)return 400;
            p=airplay_bplist_dict_get(&d,r,"entity",&e);
            if(p!=AIRPLAY_BPLIST_NOT_FOUND&&(p!=AIRPLAY_BPLIST_OK||airplay_bplist_get_uint(&d,e,&entity)!=AIRPLAY_BPLIST_OK||entity>UINT32_MAX))return 400;
            if(rid==1){if(next.screen_present)return 400;next.screen_present=true;next.screen_entity_known=p==AIRPLAY_BPLIST_OK;next.screen_entity=(uint32_t)entity;}
            if(rid==2){if(next.audio_present)return 400;next.audio_present=true;next.audio_entity_known=p==AIRPLAY_BPLIST_OK;next.audio_entity=(uint32_t)entity;}
        }
    }else if(p!=AIRPLAY_BPLIST_NOT_FOUND)return 400;
    p=airplay_bplist_dict_get(&d,params,"appStates",&list);
    if(p==AIRPLAY_BPLIST_OK){
        if(airplay_bplist_get_type(&d,list,&t,&count)!=AIRPLAY_BPLIST_OK||t!=AIRPLAY_BPLIST_ARRAY||count>8)return 400;
        for(uint64_t i=0;i<count;i++)if(airplay_bplist_array_get(&d,list,i,&o)!=AIRPLAY_BPLIST_OK||!dictionary(&d,o))return 400;
    }else if(p!=AIRPLAY_BPLIST_NOT_FOUND)return 400;
    *out=next;return 200;
}
bool airplay_command_response_ok(const uint8_t *body,size_t len)
{
    if(!len)return true;
    airplay_bplist_doc_t d;airplay_bplist_object_t root,status;uint64_t value;
    if(len>2048||airplay_bplist_open(body,len,&d)!=AIRPLAY_BPLIST_OK||
       airplay_bplist_root(&d,&root)!=AIRPLAY_BPLIST_OK||!dictionary(&d,root))return false;
    airplay_bplist_result_t r=airplay_bplist_dict_get(&d,root,"status",&status);
    return r==AIRPLAY_BPLIST_NOT_FOUND||(r==AIRPLAY_BPLIST_OK&&airplay_bplist_get_uint(&d,status,&value)==AIRPLAY_BPLIST_OK&&value==0);
}
