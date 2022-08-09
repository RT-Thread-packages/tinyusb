/*
 * Copyright (c) 2021 hpmicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <tusb.h>
#include "board.h"

extern void tud_descriptor_set_serial(char *serial_number, uint8_t length);

#ifdef PKG_TINYUSB_DEVICE_ENABLE
TU_ATTR_WEAK void generate_serial_number(void)
{
    char serial_number[32] = {"00001"};

    tud_descriptor_set_serial(serial_number, sizeof(serial_number));
}
#endif

TU_ATTR_WEAK int tusb_board_init(void)
{
#ifdef PKG_TINYUSB_DEVICE_ENABLE
    generate_serial_number();
#endif
    return 0;
}

TU_ATTR_WEAK void isr_usb0(void)
{
    rt_interrupt_enter();
#if defined(PKG_TINYUSB_DEVICE_ENABLE) && (PKG_TINYUSB_DEVICE_RHPORT_NUM == 0)
    dcd_int_handler(0);
#endif
#if defined(PKG_TINYUSB_HOST_ENABLE) && (PKG_TINYUSB_HOST_RHPORT_NUM == 0)
    hcd_int_handler(0);
#endif
    rt_interrupt_leave();
}
SDK_DECLARE_EXT_ISR_M(IRQn_USB0, isr_usb0)

TU_ATTR_WEAK void isr_usb1(void)
{
    rt_interrupt_enter();
#if defined(PKG_TINYUSB_DEVICE_ENABLE) && (PKG_TINYUSB_DEVICE_RHPORT_NUM == 1)
    dcd_int_handler(1);
#endif
#if defined(PKG_TINYUSB_HOST_ENABLE) && (PKG_TINYUSB_HOST_RHPORT_NUM == 1)
    hcd_int_handler(1);
#endif
    rt_interrupt_leave();
}
SDK_DECLARE_EXT_ISR_M(IRQn_USB1, isr_usb1)
