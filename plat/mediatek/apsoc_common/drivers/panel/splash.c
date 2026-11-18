/*
 * Copyright (c) 2026, OpenWrt.org
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Puts the picture a boot logo blob carries onto an ST7789P3 panel from BL2.
 *
 * The frame is streamed rather than composed: eight lines at a time are built
 * white, any band that covers those lines gets its pixels copied in, and the
 * burst goes straight at the panel. Only the bands are stored, not a 320x240
 * frame, and the pixels are raw RGB565 that goes on the wire unchanged, so BL2
 * needs no bitmap decoder.
 *
 * The blob comes from scripts/gen-boot-logos.py in the OpenWrt tree, which
 * carries the geometry and the text so that neither reaches this file.
 */

#include <stdint.h>
#include <string.h>

#include <arch_helpers.h>
#include <common/debug.h>
#include <lib/cassert.h>
#include <lib/utils_def.h>

#include "splash.h"
#include "st7789p3.h"

#define SPLASH_MAGIC		0x5053574fU	/* "OWSP" */
#define SPLASH_VERSION		1
#define SPLASH_MAX_BANDS	4

extern const uint8_t panel_splash[];
extern const uint32_t panel_splash_size;

struct splash_header {
	uint32_t magic;
	uint16_t version;
	uint16_t bands;
};

struct splash_band {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	uint32_t offset;
	uint32_t len;
};

CASSERT(sizeof(struct splash_header) == 8, assert_splash_header_size);
CASSERT(sizeof(struct splash_band) == 16, assert_splash_band_size);

static const struct splash_band *splash_bands;
static uint32_t splash_band_count;

static uint8_t frame_burst[ST7789P3_BURST_BYTES] __aligned(64);

static int splash_band_check(const struct splash_band *band, uint32_t used)
{
	if (band->x + band->w > ST7789P3_WIDTH ||
	    band->y + band->h > ST7789P3_HEIGHT)
		return -1;

	if (band->len != (uint32_t)band->w * band->h * ST7789P3_BPP)
		return -1;

	if (band->offset < used || band->offset > panel_splash_size ||
	    panel_splash_size - band->offset < band->len)
		return -1;

	return 0;
}

static int splash_parse(void)
{
	const struct splash_header *hdr = (const struct splash_header *)panel_splash;
	const struct splash_band *band;
	uint32_t i, used;

	if (panel_splash_size < sizeof(*hdr) || hdr->magic != SPLASH_MAGIC ||
	    hdr->version != SPLASH_VERSION)
		return -1;

	if (hdr->bands == 0 || hdr->bands > SPLASH_MAX_BANDS)
		return -1;

	used = sizeof(*hdr) + hdr->bands * sizeof(*band);
	if (panel_splash_size < used)
		return -1;

	band = (const struct splash_band *)(panel_splash + sizeof(*hdr));

	for (i = 0; i < hdr->bands; i++) {
		if (splash_band_check(&band[i], used))
			return -1;
	}

	splash_bands = band;
	splash_band_count = hdr->bands;

	return 0;
}

static void splash_line(uint8_t *line, uint32_t y)
{
	uint32_t i;

	memset(line, 0xff, ST7789P3_LINE_BYTES);

	for (i = 0; i < splash_band_count; i++) {
		const struct splash_band *band = &splash_bands[i];
		uint32_t row = y - band->y;

		if (y < band->y || row >= band->h)
			continue;

		memcpy(line + band->x * ST7789P3_BPP,
		       panel_splash + band->offset +
		       row * band->w * ST7789P3_BPP,
		       band->w * ST7789P3_BPP);
	}
}

static int splash_draw(const struct st7789p3 *panel)
{
	uint32_t y, i;
	int ret;

	ret = st7789p3_frame_start(panel);
	if (ret)
		return ret;

	for (y = 0; y < ST7789P3_HEIGHT; y += ST7789P3_BURST_LINES) {
		for (i = 0; i < ST7789P3_BURST_LINES; i++)
			splash_line(frame_burst + i * ST7789P3_LINE_BYTES,
				    y + i);

		ret = st7789p3_frame_data(panel, frame_burst,
					  ST7789P3_BURST_BYTES);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * The blob is checked before the panel is touched, so a bad one costs nothing.
 */
int panel_splash_show(const struct st7789p3 *panel)
{
	int ret;

	if (splash_parse()) {
		ERROR("Panel: splash blob is not usable\n");
		return -1;
	}

	ret = st7789p3_probe(panel);
	if (ret) {
		ERROR("Panel: init failed: %d\n", ret);
		return ret;
	}

	ret = splash_draw(panel);
	if (ret)
		ERROR("Panel: draw failed: %d\n", ret);

	return ret;
}

/*
 * The platform PLL init moves CNTFRQ partway through BL2, so the ticks banked
 * before that read back short. The figure is close, not exact.
 */
uint32_t panel_splash_elapsed_ms(void)
{
	uint64_t freq = read_cntfrq_el0();

	if (!freq)
		return 0;

	return (uint32_t)((read_cntpct_el0() * 1000ULL) / freq);
}
