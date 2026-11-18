/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, OpenWrt.org
 */

#ifndef PANEL_SPLASH_H
#define PANEL_SPLASH_H

#include <stdint.h>

#include "st7789p3.h"

int panel_splash_show(const struct st7789p3 *panel);
uint32_t panel_splash_elapsed_ms(void);

#endif /* PANEL_SPLASH_H */
