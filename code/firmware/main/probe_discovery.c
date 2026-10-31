/* USB-only discovery and advertisement after the receiver has started. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "probe.h"
#include "probe_dns.h"
#include "probe_discovery.h"
#include "probe_network.h"
#include "probe_receiver.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
static void stage(const char *s,int error)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    snprintf(probe_status.discovery_stage,sizeof(probe_status.discovery_stage),"%s",s);
    probe_status.discovery_error=error;
    xSemaphoreGive(probe_lock);
}
static void task(void *unused)
{
    (void)unused;int fd=-1;unsigned session=0;int64_t next=0;
    struct sockaddr_in6 dest={.sin6_family=AF_INET6,.sin6_port=htons(5353)};
    inet6_aton("ff02::fb",&dest.sin6_addr);
    uint8_t packet[2048],control_ip[16];char instance[256]={0},host[256]={0};
    bool resolved=false;uint16_t control_port=0;int64_t dial_at=0;
    for(;;){
        xSemaphoreTake(probe_lock,portMAX_DELAY);
        bool mounted=probe_status.mounted;unsigned current=probe_status.sessions;
        xSemaphoreGive(probe_lock);
        if(!mounted||current!=session){
            if(fd>=0){close(fd);fd=-1;}
            instance[0]=host[0]=0;session=current;next=dial_at=0;resolved=false;control_port=0;
            xSemaphoreTake(probe_lock,portMAX_DELAY);
            probe_status.discovery_port=0;
            xSemaphoreGive(probe_lock);
        }
        char ip[48];probe_network_ip6(ip,sizeof(ip));
        if(!mounted||!ip[0]){stage("wait_usb",0);vTaskDelay(pdMS_TO_TICKS(500));continue;}
        if(fd<0){
            fd=socket(AF_INET6,SOCK_DGRAM,IPPROTO_UDP);
            if(fd<0){stage("socket_error",errno);vTaskDelay(pdMS_TO_TICKS(2000));continue;}
            struct ifreq iface={0};
            int reuse=1;struct timeval timeout={.tv_sec=1};
            uint8_t hops=255,loop=0;
            struct sockaddr_in6 local={.sin6_family=AF_INET6,.sin6_port=htons(5353)};
            struct ipv6_mreq group={.ipv6mr_multiaddr=dest.sin6_addr,.ipv6mr_interface=probe_network_index()};
            dest.sin6_scope_id=group.ipv6mr_interface;
            if(!probe_network_name(iface.ifr_name,sizeof(iface.ifr_name))||!dest.sin6_scope_id){
                stage("interface_error",0);close(fd);fd=-1;vTaskDelay(pdMS_TO_TICKS(1000));continue;
            }
            /* Both reception and transmission are pinned to USB, never the status AP. */
            if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))<0||
               setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))<0||
               setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&iface,sizeof(iface))<0||
               /* lwIP's IP_MULTICAST_* options update the shared UDP PCB for IPv6 too. */
               setsockopt(fd,IPPROTO_IP,IP_MULTICAST_TTL,&hops,sizeof(hops))<0||
               setsockopt(fd,IPPROTO_IP,IP_MULTICAST_LOOP,&loop,sizeof(loop))<0||
               bind(fd,(struct sockaddr *)&local,sizeof(local))<0||
               setsockopt(fd,IPPROTO_IPV6,IPV6_JOIN_GROUP,&group,sizeof(group))<0){
                int error=errno;close(fd);fd=-1;stage("bind_error",error);vTaskDelay(pdMS_TO_TICKS(2000));continue;
            }
            stage("browsing",0);
        }
        int64_t now=esp_timer_get_time();
        if(now>=next){
            const char *query=host[0]?host:instance[0]?instance:"_carplay-ctrl._tcp.local";
            size_t n=probe_dns_query(query,host[0]?28:instance[0]?33:12,packet,sizeof(packet));
            int sent=sendto(fd,packet,n,0,(struct sockaddr *)&dest,sizeof(dest));
            xSemaphoreTake(probe_lock,portMAX_DELAY);
            if(sent==(int)n)probe_status.discovery_tx++;else probe_status.discovery_error=errno;
            xSemaphoreGive(probe_lock);
            if(probe_receiver_ready()){
                struct in6_addr own;inet6_aton(ip,&own);
                n=probe_dns_announce((const uint8_t *)&own,probe_receiver_public_hex(),packet,sizeof(packet));
                if(n)sendto(fd,packet,n,0,(struct sockaddr *)&dest,sizeof(dest));
            }
            next=now+5000000;
        }
        if(resolved&&control_port&&now>=dial_at&&probe_receiver_ready()){
            probe_receiver_connect(control_ip,control_port);dial_at=esp_timer_get_time()+30000000;
        }
        struct sockaddr_in6 peer={0};socklen_t peer_size=sizeof(peer);
        int n=recvfrom(fd,packet,sizeof(packet),0,(struct sockaddr *)&peer,&peer_size);
        if(n<0){
            if(errno!=EAGAIN&&errno!=EWOULDBLOCK){int error=errno;close(fd);fd=-1;stage("receive_error",error);}
            continue;
        }
        if(peer.sin6_family!=AF_INET6||ntohs(peer.sin6_port)!=5353)continue;
        probe_dns_result_t result;
        bool valid=probe_dns_parse(packet,(size_t)n,&result);
        xSemaphoreTake(probe_lock,portMAX_DELAY);
        probe_status.discovery_rx++;
        if(!valid)probe_status.discovery_bad++;
        else{
            if(result.response)probe_status.discovery_responses++;
            if(result.airplay_question)probe_status.discovery_airplay_queries++;
            if(result.control_ptr)probe_status.discovery_control_ptr++;
            if(result.control_port)probe_status.discovery_port=result.control_port;
        }
        xSemaphoreGive(probe_lock);
        if(valid&&result.instance[0])snprintf(instance,sizeof(instance),"%s",result.instance);
        if(valid&&result.control_port){
            stage("control_service_seen",0);control_port=result.control_port;
            if(strcmp(host,result.target)){resolved=false;snprintf(host,sizeof(host),"%s",result.target);}
        }
        if(valid&&host[0]&&probe_dns_address(packet,(size_t)n,host,control_ip))resolved=true;
    }
}
void probe_discovery_start(void)
{
    if(xTaskCreate(task,"usb-discovery",8192,NULL,3,NULL)!=pdPASS)abort();
}
