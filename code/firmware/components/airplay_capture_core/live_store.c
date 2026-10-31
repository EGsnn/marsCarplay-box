#include "live_store.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define LIVE_SLOTS 32U
typedef struct {uint32_t seq;size_t offset,len;bool idr;uint64_t sender,arrival;} entry_t;
static uint8_t *arena,config[LIVE_CONFIG_BYTES];
static size_t config_len,write_at,used,head,count;
static uint32_t epoch,sequence,seen,evicted,oversize;
static uint32_t config_changes,config_duplicates,config_clears;
static entry_t entries[LIVE_SLOTS];
static SemaphoreHandle_t mutex;
static void clear_epoch(void)
{
    if(!++epoch)epoch=1;
    sequence=seen=evicted=oversize=0;write_at=used=head=count=0;
}
bool live_store_init(void)
{
    if(arena&&mutex)return true;
    if(!mutex)mutex=xSemaphoreCreateMutex();
    if(!arena)arena=heap_caps_malloc(LIVE_ARENA_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return arena&&mutex;
}
void live_store_config(const uint8_t *data,size_t len)
{
    if(!arena||!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool valid=data&&len&&len<=sizeof(config);
    /* Repeated SPS/PPS are not a new stream. Keep queued reference pictures
     * and the reader cursor when every configuration byte is identical.
     * An explicit NULL/empty call still always starts a new generation. */
    if(valid&&len==config_len&&!memcmp(config,data,len)){
        ++config_duplicates;xSemaphoreGive(mutex);return;
    }
    if(valid)++config_changes;else ++config_clears;
    clear_epoch();config_len=valid?len:0;
    if(config_len)memcpy(config,data,config_len);
    xSemaphoreGive(mutex);
}
void live_store_frame(const uint8_t *data,size_t len,bool idr,uint64_t sender,uint64_t arrival)
{
    if(!arena||!mutex||!data||!len)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(!config_len){xSemaphoreGive(mutex);return;}
    if(sequence==UINT32_MAX)clear_epoch();
    ++sequence;++seen;
    if(len>LIVE_MAX_PAYLOAD||len>LIVE_ARENA_BYTES){++oversize;xSemaphoreGive(mutex);return;}
    while(count&&(used+len>LIVE_ARENA_BYTES||count==LIVE_SLOTS)){
        used-=entries[head].len;head=(head+1)%LIVE_SLOTS;--count;++evicted;
    }
    entry_t *e=&entries[(head+count)%LIVE_SLOTS];
    *e=(entry_t){.seq=sequence,.offset=write_at,.len=len,.idr=idr,.sender=sender,.arrival=arrival};
    size_t first=len<LIVE_ARENA_BYTES-write_at?len:LIVE_ARENA_BYTES-write_at;
    memcpy(arena+write_at,data,first);memcpy(arena,data+first,len-first);
    write_at=(write_at+len)%LIVE_ARENA_BYTES;used+=len;++count;
    xSemaphoreGive(mutex);
}
static void be(uint8_t *out,uint64_t value,unsigned bytes)
{for(unsigned i=0;i<bytes;++i)out[i]=(uint8_t)(value>>(8*(bytes-1-i)));}
static void header(uint8_t *out,unsigned kind,unsigned flags,uint32_t seq,uint64_t sender,uint64_t arrival,size_t len)
{
    memcpy(out,"MLV1",4);out[4]=kind;out[5]=flags;be(out+6,LIVE_HEADER_BYTES,2);
    be(out+8,epoch,4);be(out+12,seq,4);be(out+16,sender,8);be(out+24,arrival,8);be(out+32,len,4);
}
int live_store_copy(uint32_t requested_epoch,uint32_t after,uint8_t *out,size_t capacity)
{
    if(!out||capacity<LIVE_HEADER_BYTES)return -1;
    if(!arena||!mutex)return 0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(!config_len){xSemaphoreGive(mutex);return 0;}
    if(requested_epoch!=epoch){
        if(capacity-LIVE_HEADER_BYTES<config_len){xSemaphoreGive(mutex);return -1;}
        header(out,1,0,0,0,0,config_len);memcpy(out+LIVE_HEADER_BYTES,config,config_len);
        int n=(int)(LIVE_HEADER_BYTES+config_len);xSemaphoreGive(mutex);return n;
    }
    bool future=after>sequence;
    for(size_t i=0;i<count;++i){
        entry_t *e=&entries[(head+i)%LIVE_SLOTS];
        if(!future&&e->seq<=after)continue;
        if(capacity-LIVE_HEADER_BYTES<e->len){xSemaphoreGive(mutex);return -1;}
        unsigned flags=(e->idr?1U:0U)|((future||e->seq-after!=1)?2U:0U);
        header(out,2,flags,e->seq,e->sender,e->arrival,e->len);
        size_t first=e->len<LIVE_ARENA_BYTES-e->offset?e->len:LIVE_ARENA_BYTES-e->offset;
        memcpy(out+LIVE_HEADER_BYTES,arena+e->offset,first);
        memcpy(out+LIVE_HEADER_BYTES+first,arena,e->len-first);
        int n=(int)(LIVE_HEADER_BYTES+e->len);xSemaphoreGive(mutex);return n;
    }
    xSemaphoreGive(mutex);return 0;
}
static uint32_t read_be32(const uint8_t *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
int live_store_copy_batch(uint32_t requested_epoch,uint32_t after,uint8_t *out,size_t capacity,unsigned max_packets)
{
    if(!out||capacity<LIVE_HEADER_BYTES||!max_packets||max_packets>8)return -1;
    size_t used=0;
    for(unsigned i=0;i<max_packets&&capacity-used>=LIVE_HEADER_BYTES;++i){
        int n=live_store_copy(requested_epoch,after,out+used,capacity-used);
        /* A later AU may not fit the tail: return the complete prefix and
         * leave that AU for the reader's next request. */
        if(n<=0)return used?(int)used:n;
        requested_epoch=read_be32(out+used+8);after=read_be32(out+used+12);
        used+=(size_t)n;
    }
    return (int)used;
}
size_t live_store_status(char *out,size_t capacity)
{
    if(!mutex)return snprintf(out,capacity,"{\"ready\":false}");
    xSemaphoreTake(mutex,portMAX_DELAY);
    int n=snprintf(out,capacity,"{\"ready\":%s,\"epoch\":%u,\"sequence\":%u,\"frames\":%u,\"queued\":%u,\"bytes\":%u,\"evicted\":%u,\"oversize\":%u,\"max_payload\":%u,\"config_changes\":%u,\"config_duplicates\":%u,\"config_clears\":%u}",
        arena?"true":"false",(unsigned)epoch,(unsigned)sequence,(unsigned)seen,(unsigned)count,(unsigned)used,(unsigned)evicted,(unsigned)oversize,LIVE_MAX_PAYLOAD,(unsigned)config_changes,(unsigned)config_duplicates,(unsigned)config_clears);
    xSemaphoreGive(mutex);return n<0?0:(size_t)n;
}

uint32_t live_store_epoch(void){if(!mutex)return 0;xSemaphoreTake(mutex,portMAX_DELAY);uint32_t current=epoch;xSemaphoreGive(mutex);return current;}
