#pragma once
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>
/* All receiver sockets bind both the USB device and its link-local address. */
static inline bool capture_usb_address(int fd,struct sockaddr_in6 *address,uint16_t port)
{
    esp_netif_t *usb=esp_netif_get_handle_from_ifkey("USB_CARPLAY_PROBE");
    esp_ip6_addr_t ip;struct ifreq iface={0};
    if(!usb||esp_netif_get_ip6_linklocal(usb,&ip)!=ESP_OK||
       esp_netif_get_netif_impl_name(usb,iface.ifr_name)!=ESP_OK)return false;
    if(setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&iface,sizeof(iface)))return false;
    memset(address,0,sizeof(*address));address->sin6_family=AF_INET6;
    address->sin6_port=htons(port);address->sin6_scope_id=esp_netif_get_netif_impl_index(usb);
    memcpy(&address->sin6_addr,ip.addr,16);return true;
}
