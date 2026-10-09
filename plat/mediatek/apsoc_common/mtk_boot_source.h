/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Yuzhii0718 <admin@yuzhii0718.eu.org>. All rights reserved.
 *
 * MediaTek boot source handoff, BL2 -> BL33 (U-Boot).
 *
 * The BL2 knows how it obtained the FIP it boots: from the boot device, or -
 * the case that matters here - from DRAM, either because it is a RAM boot
 * build (bl2_boot_ram.c, the FIP is pushed over UART / by a debugger) or
 * because its XMODEM recovery replaced the stored FIP with a downloaded one.
 *
 * U-Boot uses that to warn the user in the Web failsafe when the bootloader
 * runs from DRAM only: nothing has been written to the flash in such a
 * session, so a reboot - or an update that installs a firmware not matching
 * the bootloader already in the flash - leaves a device that does not boot.
 *
 * The value travels through the standard image description arguments:
 *
 *	BL2	plat_get_next_bl_params() sets BL33's ep_info.args.arg2
 *	BL31	bl31_early_platform_setup2() reads that value out of the image
 *		parameters BL2 passed in arg0, and puts it in the description
 *		this platform builds for BL33 (mtk_boot_next.c)
 *	BL33	arch/arm/mach-mediatek/boot_params.S captures x2 in
 *		save_boot_params() and the U-Boot failsafe reads it
 *
 * BL31 has to read the value in its *early* platform setup, and must not keep
 * a pointer to BL2's description: BL2 runs from the shared L2 SRAM, which
 * bl31_plat_arch_setup() hands back to the L2 cache (platform_setup_sram() of
 * the SoC), so that memory is gone - and accessing it hangs the CPU - long
 * before BL33 is entered.
 *
 * The full-word magic keeps a stale or unrelated argument from being read as
 * a boot source: U-Boot then reports "unknown" and shows no warning, which is
 * also what an older BL2 (one that does not publish anything) results in.
 */

#ifndef MTK_BOOT_SOURCE_H
#define MTK_BOOT_SOURCE_H

#define MTK_BOOTSRC_MAGIC		0x424C		/* "BL" */
#define MTK_BOOTSRC_FLASH		0x0001
#define MTK_BOOTSRC_RAM			0x0002
#define MTK_BOOTSRC_VALUE(_code)	((MTK_BOOTSRC_MAGIC << 16) | \
					 ((_code) & 0xffff))

/*
 * BL2: record that the FIP being booted comes from DRAM instead of the boot
 * device. Called by the RAM boot FIP source and by the XMODEM recovery path
 * once a downloaded FIP has been installed.
 */
void mtk_fip_source_set_ram(void);

/*
 * BL31: read the boot source out of the image parameters BL2 handed over (its
 * arg0). Called from bl31_early_platform_setup2(); see the note above on why
 * this cannot be deferred to the point where BL33 is entered.
 */
void mtk_bl31_capture_boot_source(void *bl2_params);

#endif /* MTK_BOOT_SOURCE_H */
