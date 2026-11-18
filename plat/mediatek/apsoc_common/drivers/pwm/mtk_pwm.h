/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, OpenWrt.org
 */

#ifndef MTK_PWM_H
#define MTK_PWM_H

#include <stdint.h>

/*
 * One count is 25 ns. The block runs off CLK_TOP_PWM_SEL, and the caller has to
 * pin that mux to the 40 MHz crystal before it asks for a period.
 */
#define MTK_PWM_TICK_NS		25

void mtk_pwm_enable(uint32_t channel, uint32_t period, uint32_t duty);

#endif /* MTK_PWM_H */
