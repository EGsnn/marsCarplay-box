/* SPDX-License-Identifier: GPL-3.0-or-later
 * Report slots follow the existing DiPlay-compatible /info HID descriptor. */
#include "airplay_touch_report.h"
#include "airplay_display_config.h"
#include <string.h>
bool airplay_touch_report(const uint8_t *command,size_t length,uint8_t report[12])
{
    if(!command||!report||length!=5||command[0]>1)return false;
    unsigned x=command[1]|((unsigned)command[2]<<8);
    unsigned y=command[3]|((unsigned)command[4]<<8);
    if(x>AIRPLAY_DISPLAY_WIDTH||y>AIRPLAY_DISPLAY_HEIGHT)return false;
    memset(report,0,12);report[1]=command[0];
    memcpy(report+2,command+1,4);report[6]=1;
    return true;
}
