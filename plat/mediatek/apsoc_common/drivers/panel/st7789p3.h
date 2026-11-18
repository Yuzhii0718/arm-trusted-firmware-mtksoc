/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, OpenWrt.org
 */

#ifndef ST7789P3_H
#define ST7789P3_H

#include <stdint.h>

#define ST7789P3_WIDTH		320
#define ST7789P3_HEIGHT		240
#define ST7789P3_BPP		2
#define ST7789P3_LINE_BYTES	(ST7789P3_WIDTH * ST7789P3_BPP)

/*
 * mtk_spi_dma_transfer() copies into SPI_TXDMA_BUF, which is SPI_TX_BUF_SIZE,
 * 5120 bytes. That is exactly eight lines.
 */
#define ST7789P3_BURST_LINES	8
#define ST7789P3_BURST_BYTES	(ST7789P3_BURST_LINES * ST7789P3_LINE_BYTES)

/*
 * madctl says how the panel is mounted. Both values below set MV, so both give
 * 320x240 landscape. 0xa0 is MV|MY, 0x60 is MV|MX, which is the same picture
 * turned 180 degrees.
 */
struct st7789p3 {
	const char *spi_path;
	uint32_t src_clk_hz;
	uint32_t max_hz;
	uint32_t dc_gpio;
	uint32_t reset_gpio;
	uint8_t madctl;
};

int st7789p3_probe(const struct st7789p3 *panel);
int st7789p3_frame_start(const struct st7789p3 *panel);
int st7789p3_frame_data(const struct st7789p3 *panel, const void *buf,
			uint32_t len);

#endif /* ST7789P3_H */
