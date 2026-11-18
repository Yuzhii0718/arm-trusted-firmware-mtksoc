/*
 * Copyright (c) 2026, OpenWrt.org
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Enough of the MediaTek PWM block to light a backlight from BL2.
 *
 * Channel 0 sits MTK_PWM_CHAN_BASE into the block and the channels are
 * MTK_PWM_CHAN_WIDTH apart. Both come from the platform, because the two
 * layouts in use disagree: 0x80 and 0x40 on MT7988, 0x100 and 0x100 on MT7987.
 * The figures are the ones drivers/pwm/pwm-mtk.c and drivers/pwm/pwm-mediatek.c
 * carry for each SoC.
 *
 * The clocks are the caller's job. Their mux and gate registers move between
 * SoCs, and the backlight period below only holds if the block runs off the
 * 40 MHz crystal.
 */

#include <stdint.h>

#include <lib/mmio.h>
#include <lib/utils_def.h>
#include <platform_def.h>

#include "mtk_pwm.h"

#define PWM_ENABLE		0x0000
#define PWM_CHANNEL(n)		(MTK_PWM_CHAN_BASE + (n) * MTK_PWM_CHAN_WIDTH)

#define PWMCON			0x0000
#define PWMDWIDTH		0x002c
#define PWMTHRES		0x0030

/* Bit 15 rides on every PWMCON write in pwm-mtk.c, which does not name it */
#define PWMCON_VAL		(BIT(15) | 0)

void mtk_pwm_enable(uint32_t channel, uint32_t period, uint32_t duty)
{
	uintptr_t chan = PWM_BASE + PWM_CHANNEL(channel);

	mmio_write_32(chan + PWMCON, PWMCON_VAL);
	mmio_write_32(chan + PWMDWIDTH, period);
	mmio_write_32(chan + PWMTHRES, duty);

	mmio_setbits_32(PWM_BASE + PWM_ENABLE, BIT(channel));
}
