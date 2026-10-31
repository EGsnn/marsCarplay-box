#include "probe.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

static const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,
    .bcdUSB=0x0200,.bDeviceClass=0xef,.bDeviceSubClass=2,.bDeviceProtocol=1,
    .bMaxPacketSize0=64,.idVendor=0x303a,.idProduct=0x4002,.bcdDevice=0x0400,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1,
};
static const uint8_t configuration[]={
    TUD_CONFIG_DESCRIPTOR(1,3,0,32+TUD_CDC_NCM_DESC_LEN,0,500),
    9,TUSB_DESC_INTERFACE,0,0,2,0xff,0xf0,0,6,
    7,TUSB_DESC_ENDPOINT,0x01,TUSB_XFER_BULK,64,0,0,
    7,TUSB_DESC_ENDPOINT,0x81,TUSB_XFER_BULK,64,0,0,
    TUD_CDC_NCM_DESCRIPTOR(1,4,5,0x82,16,0x03,0x83,64,1514),
};
/* Match esp_tinyusb's NCM string slots: NET=4, MAC=5, vendor=6. */
static const char *strings[]={"\x09\x04","MarsSnow","ATK BOX CarPlay","ATK-BOX-0001","USB NCM","020211223301","iAP Interface"};

const uint16_t *__real_tud_descriptor_string_cb(uint8_t index,uint16_t langid);
const uint16_t *__wrap_tud_descriptor_string_cb(uint8_t index,uint16_t langid)
{
    static const uint16_t os_string[]={0x0312,'M','S','F','T','1','0','0',0x20};
    return index==0xee?os_string:__real_tud_descriptor_string_cb(index,langid);
}

bool tud_vendor_control_xfer_cb(uint8_t port,uint8_t stage,const tusb_control_request_t *request)
{
    if(stage!=CONTROL_STAGE_SETUP) return true;
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_status.control_count++;
    probe_status.last_request=request->bRequest;probe_status.last_type=request->bmRequestType;
    probe_status.last_value=request->wValue;probe_status.last_index=request->wIndex;
    xSemaphoreGive(probe_lock);
    /* Microsoft OS 1.0 extended compatibility descriptor: interface 0, iAP2.
     * This identifies a transport; it does not authenticate the accessory. */
    static const uint8_t compatible_id[40]={
        40,0,0,0, 0,1, 4,0, 1,0,0,0,0,0,0,0,
        0,1,'i','A','P','2',0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,
    };
    if(request->bmRequestType==0xc0 && request->bRequest==0x20 && request->wIndex==4)
        return tud_control_xfer(port,request,(void *)compatible_id,sizeof(compatible_id));
    return false; /* Unknown requests stall, never fabricate success. */
}

void probe_usb_install(void)
{
    tinyusb_config_t config=TINYUSB_DEFAULT_CONFIG();
    config.descriptor.device=&device;
    config.descriptor.full_speed_config=configuration;
    config.descriptor.string=strings;
    config.descriptor.string_count=sizeof(strings)/sizeof(strings[0]);
    ESP_ERROR_CHECK(tinyusb_driver_install(&config));
}
