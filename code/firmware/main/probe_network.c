/* USB L2 -> lwIP glue based on ESP-IDF sta2eth/usb_ncm_iface.c
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 * This isolated probe does not bridge, route or offer Internet access. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "probe.h"
#include "probe_network.h"
#include "esp_netif.h"
#include "tinyusb_net.h"
#include "lwip/esp_netif_net_stack.h"
static esp_netif_t *netif;
static void free_rx(void *handle,void *buffer){(void)handle;free(buffer);}
static esp_err_t tx(void *handle,void *buffer,size_t len)
{
    (void)handle;
    if(len>1514)return ESP_ERR_INVALID_SIZE;
    esp_err_t result=tinyusb_net_send_sync(buffer,len,NULL,pdMS_TO_TICKS(50));
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    if(result==ESP_OK)probe_status.net_tx++;else probe_status.net_drop++;
    xSemaphoreGive(probe_lock);
    return result;
}
static esp_err_t rx(void *buffer,uint16_t len,void *context)
{
    (void)context;
    if(!netif||len<14||len>1514)return ESP_ERR_INVALID_SIZE;
    uint8_t *copy=malloc(len);
    if(!copy)return ESP_ERR_NO_MEM;
    memcpy(copy,buffer,len);
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_status.net_rx++;
    if(copy[12]==0x86&&copy[13]==0xdd)probe_status.net_ipv6_rx++;
    xSemaphoreGive(probe_lock);
    /* esp_netif/lwIP assumes ownership and invokes free_rx. */
    return esp_netif_receive(netif,copy,len,NULL);
}
void tud_network_set_packet_filter_cb(uint16_t filter)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.net_filter=filter;xSemaphoreGive(probe_lock);
}
void probe_network_prepare(void)
{
    static const esp_netif_ip_info_t ip={0}; /* IPv6 link-local only; no default route. */
    esp_netif_inherent_config_t base={.flags=ESP_NETIF_FLAG_AUTOUP|ESP_NETIF_FLAG_MLDV6_REPORT,.ip_info=&ip,.if_key="USB_CARPLAY_PROBE",.if_desc="USB NCM experiment",.route_prio=1};
    esp_netif_driver_ifconfig_t driver={.handle=(void *)1,.transmit=tx,.driver_free_rx_buffer=free_rx};
    struct esp_netif_netstack_config stack={.lwip={.init_fn=ethernetif_init,.input_fn=ethernetif_input}};
    esp_netif_config_t cfg={.base=&base,.driver=&driver,.stack=&stack};
    netif=esp_netif_new(&cfg);if(!netif)abort();
    uint8_t own_mac[]={0x02,0x02,0x11,0x22,0x33,0x02};
    ESP_ERROR_CHECK(esp_netif_set_mac(netif,own_mac));
}
void probe_network_start(void)
{
    /* The USB descriptor MAC names the peer adapter; lwIP has a distinct MAC. */
    tinyusb_net_config_t cfg={.mac_addr={0x02,0x02,0x11,0x22,0x33,0x01},.on_recv_callback=rx};
    ESP_ERROR_CHECK(tinyusb_net_init(&cfg));
    esp_netif_action_start(netif,NULL,0,NULL);
}
void probe_network_mounted(bool mounted)
{
    if(mounted){
        esp_netif_action_connected(netif,NULL,0,NULL);
        ESP_ERROR_CHECK(esp_netif_create_ip6_linklocal(netif));
    }else{
        esp_netif_action_disconnected(netif,NULL,0,NULL);
        xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.net_filter=0;xSemaphoreGive(probe_lock);
    }
}
void probe_network_ip6(char *out,size_t capacity)
{
    esp_ip6_addr_t address;
    if(netif&&esp_netif_get_ip6_linklocal(netif,&address)==ESP_OK)
        snprintf(out,capacity,IPV6STR,IPV62STR(address));
    else if(capacity)out[0]=0;
}
int probe_network_index(void){return netif?esp_netif_get_netif_impl_index(netif):0;}
bool probe_network_name(char *out,size_t capacity)
{
    return netif&&capacity>=6&&esp_netif_get_netif_impl_name(netif,out)==ESP_OK;
}
