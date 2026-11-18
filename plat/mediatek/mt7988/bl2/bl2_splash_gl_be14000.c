/*
 * Copyright (c) 2026, OpenWrt.org
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Draws the OpenWrt logo and the stage name on the GL.iNet BE14000 panel from
 * BL2, ahead of DRAM init, the FIP load, BL31 and the whole of U-Boot.
 *
 * Only the pins and the clocks are here. The panel and the picture are common
 * code, in plat/mediatek/apsoc_common/drivers/panel.
 *
 * The board carries the same ST7789P3 as the BE10000, turned 180 degrees, so
 * only MADCTL differs between the two panel descriptions.
 */

#include <stdint.h>

#include <common/debug.h>
#include <lib/mmio.h>
#include <lib/utils_def.h>

#include <boot_spi.h>
#include <mt7988_def.h>
#include <mt7988_gpio.h>
#include <mtk_pwm.h>
#include <splash.h>
#include <st7789p3.h>

#define PANEL_SPI_PATH		"/soc/spi@11008000"
#define PANEL_SPI_HZ		52000000
#define PANEL_DC_GPIO		GPIO62
#define PANEL_RESET_GPIO	GPIO68
/* MV|MX, the vendor's 90 degree case, which is how this panel is mounted */
#define PANEL_MADCTL		0x60

#define BL_PWM_CHANNEL		3
#define BL_PERIOD_NS		50000
#define BL_PERIOD		(BL_PERIOD_NS / MTK_PWM_TICK_NS)
/* level 12 of the DT brightness-levels table, which is 192 of 255 */
#define BL_LEVEL		192
#define BL_LEVEL_MAX		255
#define BL_DUTY			(((BL_PERIOD * BL_LEVEL) + (BL_LEVEL_MAX / 2)) \
				 / BL_LEVEL_MAX)

/*
 * SPI1 hangs off CLK_TOP_SPIM_MST_SEL, not the CLK_TOP_SPI_SEL that
 * mtk_spi_source_clock_select() drives for the boot flash on SPI0 and SPI2, and
 * an infracfg mux in front of the block picks between CLK_TOP_I2C_SEL and that
 * one. Field positions from drivers/clk/mediatek/clk-mt7988.c, which both
 * U-Boot and the kernel carry and agree on.
 */
#define CLK_SPIM_MST_SEL_S	8
#define CLK_SPIM_MST_SEL_MASK	GENMASK(10, 8)
#define CLK_SPIM_MST_PDN	BIT(15)
#define SPIM_MST_CK_UPDATE	(1U << 13)

/*
 * CLK_TOP_PWM_SEL lives in CLK_CFG_4, which mt7988_def.h does not reach.
 *
 * Its parents run from 32 kHz to 156 MHz, so pin it to the 40 MHz crystal: no
 * PLL is involved, one count is 25 ns, and the counts above then give the same
 * 20 kHz carrier U-Boot and Linux program later.
 */
#define CLK_CFG_4_SET		(TOPCKGEN_BASE + 0x044)
#define CLK_CFG_4_CLR		(TOPCKGEN_BASE + 0x048)
#define CLK_PWM_SEL_S		0
#define CLK_PWM_SEL_MASK	GENMASK(2, 0)
#define CLK_PWM_PDN		BIT(7)
#define PWM_CK_UPDATE		(1U << 16)
#define CLK_PWM_CKSQ_40M	0

#define INFRA_MODULE_CLK_SET	(INFRACFG_AO_BASE + 0x0010)
#define INFRA_MODULE_CLK_CLR	(INFRACFG_AO_BASE + 0x0014)
#define INFRA_SPI1_SEL_MASK	BIT(5)
#define INFRA_SPI1_SEL_SPIM_MST	BIT(5)
#define INFRA_PWM_BCK_SEL_MASK	GENMASK(15, 14)
#define INFRA_PWM_BCK_SEL_TOP	(3U << 14)
/* The counter of channel 3 runs off pwm_ck4, one mux and one gate of its own */
#define INFRA_PWM_CK4_SEL_MASK	GENMASK(23, 22)
#define INFRA_PWM_CK4_SEL_TOP	(3U << 22)

#define INFRA_PDN_CLR0		(INFRACFG_AO_BASE + 0x0044)
#define INFRA_PWM_HCK_PDN	BIT(1)
#define INFRA_PWM_BCK_PDN	BIT(2)
#define INFRA_PWM_CK4_PDN	BIT(6)

#define INFRA_PDN_CLR1		(INFRACFG_AO_BASE + 0x0054)
#define INFRA_SPI1_PDN		BIT(13)
#define INFRA_SPI1_HCK_PDN	BIT(16)

static const struct st7789p3 gl_be14000_panel = {
	.spi_path = PANEL_SPI_PATH,
	.src_clk_hz = CB_MPLL_D2,
	.max_hz = PANEL_SPI_HZ,
	.dc_gpio = PANEL_DC_GPIO,
	.reset_gpio = PANEL_RESET_GPIO,
	.madctl = PANEL_MADCTL,
};

static void splash_pinmux(void)
{
	mt_set_pinmux_mode(GPIO28, GPIO_MODE_01);
	mt_set_pinmux_mode(GPIO29, GPIO_MODE_01);
	mt_set_pinmux_mode(GPIO30, GPIO_MODE_01);
	mt_set_pinmux_mode(GPIO31, GPIO_MODE_01);

	mt_set_pinmux_mode(GPIO59, GPIO_MODE_05);

	mt_set_pinmux_mode(PANEL_DC_GPIO, GPIO_MODE_00);
	mt_set_pinmux_mode(PANEL_RESET_GPIO, GPIO_MODE_00);
}

static void splash_spi_clock(void)
{
	mmio_write_32(CLK_CFG_3_CLR, CLK_SPIM_MST_SEL_MASK | CLK_SPIM_MST_PDN);
	mmio_write_32(CLK_CFG_3_SET, CLK_SPIM_208MHz << CLK_SPIM_MST_SEL_S);
	mmio_write_32(CLK_CFG_UPDATE, SPIM_MST_CK_UPDATE);

	mmio_write_32(INFRA_MODULE_CLK_CLR, INFRA_SPI1_SEL_MASK);
	mmio_write_32(INFRA_MODULE_CLK_SET, INFRA_SPI1_SEL_SPIM_MST);

	mmio_write_32(INFRA_PDN_CLR1, INFRA_SPI1_PDN | INFRA_SPI1_HCK_PDN);
}

static void splash_pwm_clock(void)
{
	mmio_write_32(CLK_CFG_4_CLR, CLK_PWM_SEL_MASK | CLK_PWM_PDN);
	mmio_write_32(CLK_CFG_4_SET, CLK_PWM_CKSQ_40M << CLK_PWM_SEL_S);
	mmio_write_32(CLK_CFG_UPDATE, PWM_CK_UPDATE);

	mmio_write_32(INFRA_MODULE_CLK_CLR,
		      INFRA_PWM_BCK_SEL_MASK | INFRA_PWM_CK4_SEL_MASK);
	mmio_write_32(INFRA_MODULE_CLK_SET,
		      INFRA_PWM_BCK_SEL_TOP | INFRA_PWM_CK4_SEL_TOP);

	mmio_write_32(INFRA_PDN_CLR0, INFRA_PWM_HCK_PDN | INFRA_PWM_BCK_PDN |
		      INFRA_PWM_CK4_PDN);
}

void mt7988_splash_init(void)
{
	splash_pinmux();
	splash_spi_clock();
	splash_pwm_clock();

	if (panel_splash_show(&gl_be14000_panel))
		return;

	mtk_pwm_enable(BL_PWM_CHANNEL, BL_PERIOD, BL_DUTY);

	NOTICE("Panel: logo up ~%u ms after reset\n", panel_splash_elapsed_ms());
}
