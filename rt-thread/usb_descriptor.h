/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-01-15     tfx2001      first version
 */

#ifndef _USB_DESCRIPTORS_H_
#define _USB_DESCRIPTORS_H_

#include <rtconfig.h>

enum
{
    REPORT_ID_BEGIN = 0,
#ifdef PKG_TINYUSB_DEVICE_HID_KEYBOARD
    REPORT_ID_KEYBOARD,
#endif
#ifdef PKG_TINYUSB_DEVICE_HID_MOUSE
    REPORT_ID_MOUSE,
#endif
#ifdef PKG_TINYUSB_DEVICE_HID_CONSUMER
    REPORT_ID_CONSUMER_CONTROL,
#endif
#ifdef PKG_TINYUSB_DEVICE_HID_GAMEPAD
    REPORT_ID_GAMEPAD,
#endif
    REPORT_ID_COUNT
};

#endif /* _USB_DESCRIPTORS_H_ */
