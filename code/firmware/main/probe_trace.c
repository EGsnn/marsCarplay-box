#include "probe_trace.h"
#include <stdio.h>
#include <string.h>
void probe_trace_add(probe_trace_t *t,unsigned e,unsigned c,unsigned b,int d,uint32_t ms)
{
    if(!t)return;
    if(t->count>=PROBE_TRACE_LIMIT){memmove(t->entries,t->entries+1,(PROBE_TRACE_LIMIT-1)*sizeof(t->entries[0]));t->count=PROBE_TRACE_LIMIT-1;}
    t->entries[t->count++]=(probe_trace_entry_t){.event=e,.code=c,.bytes=b,.detail=d,.ms=ms};
}
unsigned probe_trace_route(const char *m,const char *p)
{
    if(!m||!p)return 0;
    if(strstr(p,"pair-setup"))return 1;
    if(strstr(p,"pair-verify"))return 2;
    if(strstr(p,"auth-setup"))return 3;
    if(strstr(p,"info"))return 4;
    if(strstr(p,"fp-setup"))return 10;
    if(strstr(p,"feedback"))return 11;
    if(strstr(p,"command"))return 12;
    if(strstr(p,"getProperty"))return 13;
    if(strstr(p,"setProperty"))return 14;
    if(!strcmp(m,"SETUP"))return 5;
    if(!strcmp(m,"RECORD"))return 6;
    if(!strcmp(m,"TEARDOWN"))return 7;
    if(!strcmp(m,"OPTIONS"))return 8;
    if(!strcmp(m,"GET_PARAMETER")||!strcmp(m,"SET_PARAMETER"))return 9;
    return 0;
}
unsigned probe_trace_response_code(const uint8_t *p,size_t n)
{
    if(!p)return 0;
    for(size_t i=0;i<n&&i<16;i++)if(p[i]==' '){
        if(n-i<5||p[i+1]<'1'||p[i+1]>'5'||p[i+2]<'0'||p[i+2]>'9'||p[i+3]<'0'||p[i+3]>'9'||p[i+4]!=' ')return 0;
        return (p[i+1]-'0')*100+(p[i+2]-'0')*10+p[i+3]-'0';
    }
    return 0;
}
int probe_trace_response_cseq(const uint8_t *p,size_t n)
{
    if(!p)return -1;
    int result=-1;
    size_t at=0;
    /* Ignore the status line; inspect bounded header lines only. */
    while(at<n&&p[at]!='\n')at++;
    if(at<n)at++;
    while(at<n){
        size_t end=at;
        while(end<n&&p[end]!='\r'&&p[end]!='\n')end++;
        if(end==at)break;
        if(end-at>=5&&(p[at]=='C'||p[at]=='c')&&
           (p[at+1]=='S'||p[at+1]=='s')&&(p[at+2]=='E'||p[at+2]=='e')&&
           (p[at+3]=='Q'||p[at+3]=='q')&&p[at+4]==':'){
            if(result!=-1)return -2;
            size_t i=at+5;while(i<end&&(p[i]==' '||p[i]=='\t'))i++;
            if(i==end)return -2;
            unsigned value=0;
            for(;i<end&&p[i]>='0'&&p[i]<='9';i++){
                unsigned digit=p[i]-'0';
                if(value>((unsigned)INT32_MAX-digit)/10)return -2;
                value=value*10+digit;
            }
            while(i<end&&(p[i]==' '||p[i]=='\t'))i++;
            if(i!=end)return -2;
            result=(int)value;
        }
        at=end;if(at<n&&p[at]=='\r')at++;
        if(at<n&&p[at]=='\n')at++;
    }
    return result;
}
size_t probe_trace_json(const probe_trace_t *t,char *out,size_t cap)
{
    if(!t||!out||cap<3)return 0;
    size_t pos=0;out[pos++]='[';
    unsigned count=t->count>PROBE_TRACE_LIMIT?PROBE_TRACE_LIMIT:t->count;
    for(unsigned i=0;i<count;i++){
        const probe_trace_entry_t *e=&t->entries[i];
        int n=snprintf(out+pos,cap-pos,"%s{\"ms\":%u,\"event\":%u,\"code\":%u,\"bytes\":%u,\"detail\":%d}",i?",":"",(unsigned)e->ms,e->event,e->code,(unsigned)e->bytes,(int)e->detail);
        if(n<0||(size_t)n>=cap-pos){out[0]=0;return 0;}pos+=(size_t)n;
    }
    if(cap-pos<2){out[0]=0;return 0;}out[pos++]=']';out[pos]=0;return pos;
}
