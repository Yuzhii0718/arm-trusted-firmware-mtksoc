/*
 * Copyright (c) 2026, OpenWrt.org
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Sitronix ST7789P3 over SPI in MIPI DBI type C mode, where a D/C GPIO says
 * whether a byte is a command or a parameter.
 *
 * Transfers use the raw mtk_spi_fifo_transfer() and mtk_spi_dma_transfer()
 * entry points rather than the spi-mem API the SPI NAND uses. Nothing else in
 * this tree calls either of them. They take a plain full duplex descriptor,
 * which is what a display needs and what U-Boot's spi-mem only driver could
 * not offer.
 *
 * The init sequence is the vendor one, the same table the U-Boot driver sends
 * and the same one the panel-mipi-dbi blob carries for Linux. Only MADCTL comes
 * from the caller, because that is the one byte two boards with the same panel
 * mounted differently disagree on.
 */

#include <cdefs.h>
#include <stdint.h>
#include <string.h>

#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <drivers/gpio.h>
#include <drivers/spi_mem.h>
#include <lib/utils_def.h>

#include <mtk_spi.h>

#include "st7789p3.h"

#define MIPI_DCS_SOFT_RESET		0x01
#define MIPI_DCS_EXIT_SLEEP_MODE	0x11
#define MIPI_DCS_ENTER_INVERT_MODE	0x21
#define MIPI_DCS_SET_DISPLAY_ON		0x29
#define MIPI_DCS_SET_COLUMN_ADDRESS	0x2a
#define MIPI_DCS_SET_PAGE_ADDRESS	0x2b
#define MIPI_DCS_WRITE_MEMORY_START	0x2c
#define MIPI_DCS_SET_TEAR_ON		0x35
#define MIPI_DCS_SET_ADDRESS_MODE	0x36
#define MIPI_DCS_SET_PIXEL_FORMAT	0x3a

#define ST7789P3_DELAY		0x00

/*
 * Command timing is far tighter than pixel timing on this controller, so
 * anything short goes out slowly and only bulk pixel data runs at full speed,
 * as mipi_dbi_spi_cmd_max_speed() does in Linux.
 */
#define ST7789P3_SLOW_MAX_LEN	64
#define ST7789P3_SLOW_HZ	10000000

/* MTK_SPI_MAX_FIFO_SIZE, which mtk_spi.c keeps to itself */
#define ST7789P3_FIFO_MAX	32

/* SPI_TX_BUF_SIZE plus SPI_RX_BUF_SIZE, both private to mtk_spi.c */
#define ST7789P3_SPI_SCRATCH	(10 * 1024)

/* iowrite32_rep() walks the FIFO buffer as u32, and BL2 runs with SCTLR.A set */
static uint8_t cmd_buf[ST7789P3_FIFO_MAX] __aligned(4);
static uint8_t spi_scratch[ST7789P3_SPI_SCRATCH] __aligned(64);

static struct mtk_chip_config panel_chip_config = {
	.sample_sel = 0,
	.get_tick_dly = 2,
	.inter_loopback = 0,
};

static struct spi_device panel_spi = {
	.chip_config = &panel_chip_config,
	.cs_timing = NULL,
	.mode = SPI_MODE_3,
};

/* Records are <cmd> <nparams> <params...>; ST7789P3_DELAY carries milliseconds */
static const uint8_t st7789p3_init_seq[] = {
	MIPI_DCS_SOFT_RESET, 0,
	ST7789P3_DELAY, 1, 5,
	MIPI_DCS_EXIT_SLEEP_MODE, 0,
	ST7789P3_DELAY, 1, 120,
	MIPI_DCS_SET_ADDRESS_MODE, 1, 0x00,		/* panel->madctl */
	MIPI_DCS_SET_PIXEL_FORMAT, 1, 0x05,
	0xb2, 5, 0x05, 0x05, 0x00, 0x33, 0x33,		/* PORCTRL */
	0xb7, 1, 0x35,					/* GCTRL */
	0xbb, 1, 0x21,					/* VCOMS */
	0xc0, 1, 0x2c,					/* LCMCTRL */
	0xc2, 1, 0x01,					/* VDVVRHEN */
	0xc3, 1, 0x0b,					/* VRHS */
	0xc4, 1, 0x20,					/* VDVS */
	0xc6, 1, 0x0a,					/* FRCTRL2 */
	0xd0, 2, 0xa7, 0xa1,				/* PWCTRL1 */
	0xd0, 2, 0xa4, 0xa1,				/* PWCTRL1 */
	MIPI_DCS_SET_TEAR_ON, 1, 0x00,
	0xd6, 1, 0xa1,					/* vendor */
	0xe0, 14, 0xd0, 0x04, 0x08, 0x0a, 0x09, 0x05, 0x2d,
		  0x43, 0x49, 0x09, 0x16, 0x15, 0x26, 0x2b,	/* PVGAMCTRL */
	0xe1, 14, 0xd0, 0x03, 0x09, 0x0a, 0x0a, 0x06, 0x2e,
		  0x44, 0x40, 0x3a, 0x15, 0x15, 0x26, 0x2a,	/* NVGAMCTRL */
	MIPI_DCS_ENTER_INVERT_MODE, 0,
	ST7789P3_DELAY, 1, 10,
	MIPI_DCS_SET_DISPLAY_ON, 0,
	ST7789P3_DELAY, 1, 120,
};

static int st7789p3_xfer(const struct st7789p3 *panel, int dc,
			 const uint8_t *buf, uint32_t len)
{
	struct spi_transfer xfer = { 0 };

	gpio_set_value(panel->dc_gpio, dc);

	xfer.tx_buf = (uint8_t *)buf;
	xfer.len = len;
	xfer.speed_hz = len > ST7789P3_SLOW_MAX_LEN ? panel->max_hz :
			MIN(panel->max_hz, (uint32_t)ST7789P3_SLOW_HZ);

	if (len <= ST7789P3_FIFO_MAX)
		return mtk_spi_fifo_transfer(&panel_spi, &xfer);

	return mtk_spi_dma_transfer(&panel_spi, &xfer);
}

static int st7789p3_cmd(const struct st7789p3 *panel, uint8_t cmd)
{
	cmd_buf[0] = cmd;

	return st7789p3_xfer(panel, GPIO_LEVEL_LOW, cmd_buf, 1);
}

static int st7789p3_params(const struct st7789p3 *panel, const uint8_t *buf,
			   uint32_t len)
{
	if (!len)
		return 0;

	memcpy(cmd_buf, buf, len);

	return st7789p3_xfer(panel, GPIO_LEVEL_HIGH, cmd_buf, len);
}

static int st7789p3_init_display(const struct st7789p3 *panel)
{
	uint32_t i = 0;
	int ret;

	while (i < ARRAY_SIZE(st7789p3_init_seq)) {
		uint8_t cmd = st7789p3_init_seq[i++];
		uint8_t len = st7789p3_init_seq[i++];

		if (cmd == ST7789P3_DELAY) {
			mdelay(st7789p3_init_seq[i]);
			i += len;
			continue;
		}

		ret = st7789p3_cmd(panel, cmd);
		if (ret)
			return ret;

		if (cmd == MIPI_DCS_SET_ADDRESS_MODE)
			ret = st7789p3_params(panel, &panel->madctl, 1);
		else
			ret = st7789p3_params(panel, &st7789p3_init_seq[i], len);
		if (ret)
			return ret;

		i += len;
	}

	return 0;
}

int st7789p3_probe(const struct st7789p3 *panel)
{
	int ret;

	panel_spi.src_clk_hz = panel->src_clk_hz;
	panel_spi.max_speed_hz = panel->max_hz;

	mtk_qspi_setup_buffer(spi_scratch);

	ret = mtk_qspi_init_by_path(panel->spi_path, panel->src_clk_hz);
	if (ret) {
		ERROR("ST7789P3: no SPI bus at %s: %d\n", panel->spi_path, ret);
		return ret;
	}

	gpio_set_direction(panel->dc_gpio, GPIO_DIR_OUT);
	gpio_set_direction(panel->reset_gpio, GPIO_DIR_OUT);
	gpio_set_value(panel->dc_gpio, GPIO_LEVEL_LOW);

	/* RESX is active low, so released is logical 1, as mipi_dbi_hw_reset() */
	gpio_set_value(panel->reset_gpio, GPIO_LEVEL_LOW);
	mdelay(1);
	gpio_set_value(panel->reset_gpio, GPIO_LEVEL_HIGH);
	mdelay(120);

	return st7789p3_init_display(panel);
}

int st7789p3_frame_start(const struct st7789p3 *panel)
{
	uint8_t col[4] = { 0, 0, (ST7789P3_WIDTH - 1) >> 8,
			   (ST7789P3_WIDTH - 1) & 0xff };
	uint8_t row[4] = { 0, 0, (ST7789P3_HEIGHT - 1) >> 8,
			   (ST7789P3_HEIGHT - 1) & 0xff };
	int ret;

	ret = st7789p3_cmd(panel, MIPI_DCS_SET_COLUMN_ADDRESS);
	if (ret)
		return ret;

	ret = st7789p3_params(panel, col, sizeof(col));
	if (ret)
		return ret;

	ret = st7789p3_cmd(panel, MIPI_DCS_SET_PAGE_ADDRESS);
	if (ret)
		return ret;

	ret = st7789p3_params(panel, row, sizeof(row));
	if (ret)
		return ret;

	return st7789p3_cmd(panel, MIPI_DCS_WRITE_MEMORY_START);
}

int st7789p3_frame_data(const struct st7789p3 *panel, const void *buf,
			uint32_t len)
{
	const uint8_t *p = buf;

	while (len) {
		uint32_t n = MIN(len, (uint32_t)ST7789P3_BURST_BYTES);
		int ret = st7789p3_xfer(panel, GPIO_LEVEL_HIGH, p, n);

		if (ret)
			return ret;

		p += n;
		len -= n;
	}

	return 0;
}
