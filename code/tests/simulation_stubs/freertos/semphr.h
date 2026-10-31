#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){
 SemaphoreHandle_t m=malloc(sizeof(*m));
 if(m&&pthread_mutex_init(m,NULL)){free(m);return NULL;}return m;
}
static inline int xSemaphoreTake(SemaphoreHandle_t m,unsigned timeout){
 (void)timeout;int e=pthread_mutex_lock(m);assert(!e);return 1;
}
static inline int xSemaphoreGive(SemaphoreHandle_t m){int e=pthread_mutex_unlock(m);assert(!e);return 1;}
