// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, Yuzhii0718 <admin@yuzhii0718.eu.org>. All rights reserved.
 *
 * XMODEM recovery mode.
 *
 * Whatever prevents BL2 from booting the stored BL31 + U-Boot FIP -- a boot
 * device that cannot be initialized, an erased or corrupted FIP, an image that
 * fails authentication or decompression -- is routed here instead of being
 * turned into a dead device. The user uploads a FIP over the serial port using
 * the XMODEM protocol and BL2 keeps booting from it.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <common/debug.h>
#include <drivers/console.h>
#include <drivers/delay_timer.h>
#include <drivers/io/io_driver.h>
#include <drivers/io/io_memmap.h>
#include <hsuart.h>
#include <lib/mmio.h>
#include <mtk_wdt.h>
#include <platform_def.h>
#include <tools_share/firmware_image_package.h>

#include "bl2_plat_setup.h"
#include "xmodem.h"

/*
 * Window during which the operator can divert an otherwise successful boot
 * into XMODEM recovery, in milliseconds.
 *
 * This countdown runs on every successful boot, so it is a direct boot-time
 * cost, not a one-off recovery expense. It is sized through Kconfig
 * (XMODEM_PROMPT_TIMEOUT_MS), and 0 removes the prompt entirely. Letting the
 * window expire boots the stored image as usual.
 */
#ifndef XMODEM_PROMPT_TIMEOUT_MS
#define XMODEM_PROMPT_TIMEOUT_MS	1000U
#endif

/*
 * Console poll granularity while waiting for the operator. A 10 ms slice makes
 * the prompt feel instantaneous without hammering the UART FIFO.
 */
#define XMODEM_PROMPT_POLL_US		10000U
#define XMODEM_PROMPT_POLLS_PER_S	(1000000U / XMODEM_PROMPT_POLL_US)

/*
 * The UART is driven directly instead of going through the console framework:
 * XMODEM needs a non-blocking getc() while console_getc() is blocking, and the
 * protocol bytes must not be altered by the console (CR/LF translation).
 */
static int xmodem_uart_getc(void)
{
	if (!(mmio_read_32(UART_BASE + UART_LSR) & UART_LSR_DR))
		return -1;

	return (int)(mmio_read_32(UART_BASE + UART_RBR) & 0xff);
}

static void xmodem_uart_putc(int ch)
{
	while (!(mmio_read_32(UART_BASE + UART_LSR) & UART_LSR_THRE))
		;

	mmio_write_32(UART_BASE + UART_THR, ch & 0xff);
}

static const struct xmodem_io xmodem_io = {
	.getc = xmodem_uart_getc,
	.putc = xmodem_uart_putc,
};

static io_block_spec_t xmodem_fip_spec = {
	.offset = XMODEM_BUF_OFFSET,
	.length = XMODEM_BUF_SIZE,
};

static uintptr_t xmodem_memmap_handle;

/*
 * Open the memmap device exposing the downloaded FIP.
 *
 * It is registered at most once: recovery can be entered several times in a
 * row and io_register_device() consumes a slot of the fixed I/O device pool.
 */
static int xmodem_fip_source_setup(uintptr_t *dev_handle, uintptr_t *image_spec,
				   size_t size)
{
	const io_dev_connector_t *dev_con;
	uintptr_t handle;
	int ret;

	/*
	 * io_dev_open() reports the device handle through the pointer it is
	 * given. Carry it in a local and only then publish it: reading the
	 * static back proved to be miscompiled here (the pre-call value was
	 * reused after the call), which left the handle NULL and made the
	 * policy check dereference address 0.
	 */
	if (xmodem_memmap_handle) {
		handle = xmodem_memmap_handle;
	} else {
		ret = register_io_dev_memmap(&dev_con);
		if (ret) {
			ERROR("XMODEM: register_io_dev_memmap failed: %d\n", ret);
			return ret;
		}

		ret = io_dev_open(dev_con, (uintptr_t)NULL, &handle);
		if (ret) {
			ERROR("XMODEM: io_dev_open failed: %d\n", ret);
			return ret;
		}

		xmodem_memmap_handle = handle;
	}

	xmodem_fip_spec.length = size;
	*dev_handle = handle;
	*image_spec = (uintptr_t)&xmodem_fip_spec;

	return 0;
}

#ifdef MTK_XMODEM_DEBUG
/*
 * Report what was actually received.
 *
 * A FIP ToC header can be well-formed while the images it points at are absent
 * or lie outside the received data. That is impossible to tell from the header
 * alone, and it only shows up much later as a boot that goes nowhere, so spell
 * the entries out instead of leaving them to be guessed.
 */
static void xmodem_dump_fip_toc(size_t size)
{
	static const char hex[] = "0123456789abcdef";
	const fip_toc_header_t *hdr =
		(const fip_toc_header_t *)XMODEM_BUF_OFFSET;
	size_t remaining;
	const uint8_t *p;
	char uuid_str[2 * sizeof(uuid_t) + 1];
	unsigned int i;

	if (size < sizeof(*hdr)) {
		ERROR("XMODEM: only 0x%zx bytes, too small for a FIP\n", size);
		return;
	}

	if (hdr->name != TOC_HEADER_NAME) {
		ERROR("XMODEM: bad ToC header 0x%08x (expected 0x%08x)\n",
		      hdr->name, TOC_HEADER_NAME);
		return;
	}

	NOTICE("XMODEM: FIP ToC serial 0x%08x, received 0x%zx bytes\n",
	       hdr->serial_number, size);

	p = (const uint8_t *)XMODEM_BUF_OFFSET + sizeof(*hdr);
	remaining = size - sizeof(*hdr);

	while (remaining >= sizeof(fip_toc_entry_t)) {
		const fip_toc_entry_t *e = (const fip_toc_entry_t *)p;
		bool terminated = true;

		for (i = 0; i < sizeof(e->uuid); i++) {
			if (((const uint8_t *)&e->uuid)[i] != 0U) {
				terminated = false;
				break;
			}
		}
		if (terminated)
			break;

		for (i = 0; i < sizeof(e->uuid); i++) {
			uint8_t b = ((const uint8_t *)&e->uuid)[i];

			uuid_str[i * 2] = hex[b >> 4];
			uuid_str[i * 2 + 1] = hex[b & 0xfU];
		}
		uuid_str[sizeof(uuid_str) - 1] = '\0';

		NOTICE("XMODEM:   %s off 0x%" PRIx64 " size 0x%" PRIx64 "%s\n",
		       uuid_str, e->offset_address, e->size,
		       ((e->offset_address + e->size) > (uint64_t)size) ?
		       "  <-- OUTSIDE RECEIVED DATA" : "");

		p += sizeof(*e);
		remaining -= sizeof(*e);
	}

	if (remaining < sizeof(fip_toc_entry_t))
		WARN("XMODEM: ToC not terminated within the received data\n");
}
#endif /* MTK_XMODEM_DEBUG */

/*
 * Receive one FIP and install it as the FIP image source.
 *
 * Returns 0 only once a FIP with a valid ToC has been received.
 */
static int xmodem_load_fip(void)
{
	uintptr_t dev_handle, image_spec;
	size_t size;
	int ret;

	ret = xmodem_receive(&xmodem_io, XMODEM_BUF_OFFSET, XMODEM_BUF_SIZE,
			     &size);
	if (ret) {
		ERROR("XMODEM: transfer failed: %d\n", ret);
		return ret;
	}

	if (!size) {
		ERROR("XMODEM: no data received\n");
		return -EIO;
	}

	NOTICE("XMODEM: received 0x%zx bytes @ 0x%08x\n", size,
	       XMODEM_BUF_OFFSET);

#ifdef MTK_XMODEM_DEBUG
	xmodem_dump_fip_toc(size);
#endif

	XMODEM_TRACE("XMODEM: installing the downloaded FIP as the image source\n");
	ret = xmodem_fip_source_setup(&dev_handle, &image_spec, size);
	if (ret)
		return ret;

	XMODEM_TRACE("XMODEM: validating the downloaded FIP\n");
	/* This also validates the FIP ToC of the downloaded image */
	ret = mtk_fip_set_xmodem_source(dev_handle, image_spec);
	if (ret)
		ERROR("XMODEM: downloaded image is not a valid FIP (%d)\n", ret);
	else
		XMODEM_TRACE("XMODEM: FIP accepted, handing over to the loader\n");

	return ret;
}

/*
 * Offer the operator a short window to divert into XMODEM recovery.
 *
 * A FIP that loads, authenticates and decompresses cleanly is not necessarily
 * one that boots: the images may be well-formed yet unable to start up (a
 * U-Boot that never opens its console, for instance). No read or verification
 * path can detect that, so the decision is left to the operator.
 *
 * This runs before anything is read from the boot device, so the diversion
 * ends up taking the same shape as an automatic recovery: the images are
 * loaded once, from the downloaded FIP, with no completed sequence to unwind.
 *
 * Only 'x' is honoured and every other character is discarded, so a stray byte
 * on the line cannot silently drop the device into recovery.
 *
 * Returning 1 only elects recovery: the caller then goes through
 * mtk_xmodem_recovery(), which asks for 'x' once more before actually starting
 * the transfer. That second keypress is deliberate -- it confirms the choice
 * and leaves time to start the XMODEM sender.
 *
 * Returns 1 if recovery was requested, 0 if the window expired.
 */
int mtk_xmodem_recovery_prompt(void)
{
#if XMODEM_PROMPT_TIMEOUT_MS > 0
	unsigned int secs = (XMODEM_PROMPT_TIMEOUT_MS + 999U) / 1000U;
	unsigned int sec, poll;

	/*
	 * Same reasoning as in mtk_xmodem_recovery(): the deadline of a sloppily
	 * configured watchdog could otherwise fire during the countdown.
	 */
	mtk_wdt_control(false);

	console_flush();

	for (sec = secs; sec != 0U; sec--) {
		NOTICE("Press 'x' within %u s to enter XMODEM recovery\n", sec);

		for (poll = 0U; poll < XMODEM_PROMPT_POLLS_PER_S; poll++) {
			if (xmodem_uart_getc() == 'x')
				return 1;

			udelay(XMODEM_PROMPT_POLL_US);
		}
	}
#endif

	return 0;
}

void mtk_xmodem_recovery(const char *reason)
{
	unsigned int attempt = 0;

	/*
	 * The transfer may take minutes, do not let the watchdog bite. It stays
	 * disabled for the remainder of BL2; the next boot stage is responsible
	 * for taking it over again.
	 */
	mtk_wdt_control(false);

	console_flush();

	ERROR("BL2: %s; entering XMODEM recovery\n", reason);

	/*
	 * Never give up. A rejected image only means the user has to send it
	 * again, it must not leave the device unusable.
	 */
	for (;;) {
		int ch;

		if (attempt++)
			ERROR("XMODEM: the image just sent was rejected\n");

		NOTICE("Press 'x' to load BL31 + U-Boot FIP via XMODEM\n");

		do {
			ch = xmodem_uart_getc();
		} while (ch != 'x');

		if (!xmodem_load_fip())
			return;
	}
}
