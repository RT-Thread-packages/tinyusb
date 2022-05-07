/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-05-07     Lishuo       first version
 */

#include <tusb.h>
#include "board.h"

int tusb_board_init(void)
{
    rp2040_usb_init();
    return 0;
}