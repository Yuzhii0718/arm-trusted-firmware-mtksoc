// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, Yuzhii0718 <admin@yuzhii0718.eu.org>. All rights reserved.
 *
 * XMODEM recovery mode.
 *
 * When no valid FIP (BL31 + U-Boot) can be fetched from the configured boot
 * device, BL2 asks the user to upload one over the serial port using the
 * XMODEM protocol and keeps booting from the image just downloaded. This
 * allows rescuing a device whose firmware was erased or corrupted.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <common/debug.h>
#include <drivers/console.h>
#include <drivers/io/io_driver.h>
#include <drivers/io/io_memmap.h>
#include <hsuart.h>
#include <lib/mmio.h>
#include <mtk_wdt.h>
#include <platform_def.h>

#include "bl2_plat_setup.h"
#include "xmodem.h"

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

static int xmodem_fip_source_setup(uintptr_t *dev_handle, uintptr_t *image_spec,
				   size_t size)
{
	const io_dev_connector_t *dev_con;
	int ret;

	ret = register_io_dev_memmap(&dev_con);
	if (ret) {
		ERROR("XMODEM: register_io_dev_memmap failed: %d\n", ret);
		return ret;
	}

	ret = io_dev_open(dev_con, (uintptr_t)NULL, dev_handle);
	if (ret) {
		ERROR("XMODEM: io_dev_open failed: %d\n", ret);
		return ret;
	}

	xmodem_fip_spec.length = size;
	*image_spec = (uintptr_t)&xmodem_fip_spec;

	return 0;
}

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

	if (!size)
		return -EIO;

	NOTICE("XMODEM: received 0x%zx bytes @ 0x%08x\n", size,
	       XMODEM_BUF_OFFSET);

	ret = xmodem_fip_source_setup(&dev_handle, &image_spec, size);
	if (ret)
		return ret;

	/* This also validates the FIP ToC of the downloaded image */
	ret = mtk_fip_set_xmodem_source(dev_handle, image_spec);
	if (ret)
		ERROR("XMODEM: downloaded image is not a valid FIP (%d)\n", ret);

	return ret;
}

int mtk_xmodem_recovery(void)
{
	/* The transfer may take minutes, do not let the watchdog bite */
	mtk_wdt_control(false);

	console_flush();

	NOTICE("\nNo valid BL31 + U-Boot FIP available.\n");

	for (;;) {
		int ch;

		NOTICE("Press 'x' to load BL31 + U-Boot FIP via XMODEM\n");

		do {
			ch = xmodem_uart_getc();
		} while (ch != 'x');

		if (!xmodem_load_fip())
			return 0;

		ERROR("XMODEM: please try again\n");
	}
}
