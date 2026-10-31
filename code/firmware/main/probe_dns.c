#include "probe_dns.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
static uint16_t u16(const uint8_t *p){return (uint16_t)((p[0]<<8)|p[1]);}
/* Compression jumps are bounded, including cycles and forward references. */
static bool name(const uint8_t *p,size_t n,size_t *offset,char out[256])
{
    size_t pos=*offset,used=0,next=0;bool jumped=false;
    for(unsigned steps=0;steps<128;steps++){
        if(pos>=n)return false;
        unsigned len=p[pos++];
        if((len&0xc0)==0xc0){
            if(pos>=n)return false;
            if(!jumped)next=pos+1;
            pos=((len&63)<<8)|p[pos];jumped=true;continue;
        }
        if(len&0xc0)return false;
        if(!len){out[used]=0;*offset=jumped?next:pos;return true;}
        if(len>n-pos||used+len+(used?1:0)>253)return false;
        if(used)out[used++]='.';
        for(unsigned i=0;i<len;i++){
            unsigned c=p[pos++];
            /* Preserve UTF-8 labels; names never enter the public status JSON. */
            if(c<32||c=='.')return false;
            out[used++]=(char)tolower(c);
        }
    }
    return false;
}
static bool control_instance(const char *s)
{
    const char *suffix="._carplay-ctrl._tcp.local";
    size_t a=strlen(s),b=strlen(suffix);
    return a>b&&!strcmp(s+a-b,suffix);
}
bool probe_dns_parse(const uint8_t *p,size_t n,probe_dns_result_t *out)
{
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!p||n<12||n>2048)return false;
    probe_dns_result_t r={.response=(p[2]&0x80)!=0};
    if((p[2]&0x78)||(p[3]&15))return false; /* standard successful DNS only */
    unsigned questions=u16(p+4),records=(unsigned)u16(p+6)+u16(p+8)+u16(p+10);
    if(questions>64||records>128)return false;
    size_t pos=12;char owner[256];
    for(unsigned i=0;i<questions;i++){
        if(!name(p,n,&pos,owner)||n-pos<4)return false;
        if(!r.response&&!strcmp(owner,"_airplay._tcp.local"))r.airplay_question=true;
        pos+=4;
    }
    for(unsigned i=0;i<records;i++){
        if(!name(p,n,&pos,owner)||n-pos<10)return false;
        uint16_t type=u16(p+pos),klass=u16(p+pos+2)&0x7fff,len=u16(p+pos+8);
        uint32_t ttl=((uint32_t)p[pos+4]<<24)|((uint32_t)p[pos+5]<<16)|u16(p+pos+6);
        pos+=10;if(len>n-pos)return false;
        size_t end=pos+len,at=pos;char target[256];
        if(r.response&&klass==1&&ttl&&type==12&&!strcmp(owner,"_carplay-ctrl._tcp.local")){
            if(!name(p,n,&at,target)||at!=end)return false;
            if(control_instance(target)){r.control_ptr=true;memcpy(r.instance,target,strlen(target)+1);}
        }
        if(r.response&&klass==1&&ttl&&type==33&&control_instance(owner)){
            if(len<7)return false;
            at+=6;if(!name(p,n,&at,target)||at!=end)return false;
            r.control_port=u16(p+pos+4);
            memcpy(r.instance,owner,strlen(owner)+1);memcpy(r.target,target,strlen(target)+1);
        }
        pos=end;
    }
    *out=r;return true;
}
size_t probe_dns_query(const char *s,uint16_t type,uint8_t *out,size_t cap)
{
    if(!s||!*s||!out||cap<17)return 0;
    memset(out,0,12);out[5]=1;size_t pos=12,total=0;
    while(*s){
        const char *dot=strchr(s,'.');size_t len=dot?(size_t)(dot-s):strlen(s);
        if(!len||len>63||total+len+1>254||pos+len+6>cap)return 0;
        out[pos++]=(uint8_t)len;memcpy(out+pos,s,len);pos+=len;total+=len+1;
        if(!dot)break;
        s=dot+1;if(!*s)return 0;
    }
    out[pos++]=0;out[pos++]=(uint8_t)(type>>8);out[pos++]=(uint8_t)type;
    out[pos++]=0;out[pos++]=1;return pos;
}
bool probe_dns_address(const uint8_t *p,size_t n,const char *host,uint8_t ip6[16])
{
    probe_dns_result_t valid;
    if(!host||!ip6||!probe_dns_parse(p,n,&valid)||!valid.response)return false;
    size_t pos=12;char owner[256];unsigned questions=u16(p+4);
    unsigned records=(unsigned)u16(p+6)+u16(p+8)+u16(p+10);
    for(unsigned i=0;i<questions;i++){if(!name(p,n,&pos,owner)||n-pos<4)return false;pos+=4;}
    for(unsigned i=0;i<records;i++){
        if(!name(p,n,&pos,owner)||n-pos<10)return false;
        unsigned type=u16(p+pos),klass=u16(p+pos+2)&0x7fff,len=u16(p+pos+8);
        bool live=p[pos+4]||p[pos+5]||p[pos+6]||p[pos+7];pos+=10;
        if(len>n-pos)return false;
        if(type==28&&klass==1&&len==16&&live&&!strcmp(owner,host)&&p[pos]==0xfe&&(p[pos+1]&0xc0)==0x80){memcpy(ip6,p+pos,16);return true;}
        pos+=len;
    }
    return false;
}
static size_t put_name(const char *s,uint8_t *p,size_t cap)
{
    uint8_t q[280];size_t n=probe_dns_query(s,1,q,sizeof(q));
    if(!n||n-16>cap)return 0;
    memcpy(p,q+12,n-16);return n-16;
}
size_t probe_dns_announce(const uint8_t ip6[16],const char *pk,uint8_t *p,size_t cap)
{
    if(!ip6||!pk||strlen(pk)!=64||!p||cap<1100)return 0;
    const char *service="_airplay._tcp.local",*instance="ATK-BOX-CarPlay._airplay._tcp.local",*host="atkcarplay.local";
    memset(p,0,12);p[2]=0x84;p[7]=4;size_t pos=12;
    const char *owners[]={service,instance,instance,host};unsigned types[]={12,33,16,28};
    for(unsigned i=0;i<4;i++){
        pos+=put_name(owners[i],p+pos,cap-pos);
        p[pos++]=0;p[pos++]=types[i];p[pos++]=i==0?0:0x80;p[pos++]=1;
        p[pos++]=0;p[pos++]=0;p[pos++]=0;p[pos++]=120;
        size_t length_at=pos;pos+=2;size_t start=pos;
        if(i==0)pos+=put_name(instance,p+pos,cap-pos);
        else if(i==1){memset(p+pos,0,4);pos+=4;p[pos++]=5000>>8;p[pos++]=5000&255;pos+=put_name(host,p+pos,cap-pos);}
        else if(i==2){
            const char *txt[]={"deviceid=02:02:11:22:33:02","features=0x44440B80,0x61","flags=0x4","model=ATK-DNESP32S3-BOX","protovers=1.0","pi=9a307d11-8b7a-4fd6-9001-000000000146","srcvers=320.17"};
            for(unsigned j=0;j<sizeof(txt)/sizeof(txt[0]);j++){size_t n=strlen(txt[j]);p[pos++]=n;memcpy(p+pos,txt[j],n);pos+=n;}
            p[pos++]=67;memcpy(p+pos,"pk=",3);pos+=3;memcpy(p+pos,pk,64);pos+=64;
        }else{memcpy(p+pos,ip6,16);pos+=16;}
        size_t len=pos-start;p[length_at]=len>>8;p[length_at+1]=len;
    }
    return pos;
}
