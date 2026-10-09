/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2023, MediaTek Inc. All rights reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#ifndef BL2_PLAT_SETUP_H
#define BL2_PLAT_SETUP_H

#include <common/debug.h>
#include <stddef.h>
#include <stdint.h>

/* BL2 initcalls */
struct initcall {
	void (*callfn)(void);
#if (LOG_LEVEL >= LOG_LEVEL_VERBOSE) && FPGA
	const char* name;
#endif
};

#if (LOG_LEVEL >= LOG_LEVEL_VERBOSE) && FPGA
#define INITCALL(_fn)	{ .callfn = (_fn), .name = #_fn }
#else
#define INITCALL(_fn)	{ .callfn = (_fn) }
#endif

extern const struct initcall bl2_initcalls[];

/* QSPI buffer */
#define QSPI_BUF_OFFSET			0x40100000
#define QSPI_BUF_SIZE			0x100000

/* Scratch buffer */
#define SCRATCH_BUF_OFFSET		0x40400000
#define SCRATCH_BUF_SIZE		0x400000

/* FIP XZ decompression buffer */
#define FIP_DECOMP_BUF_OFFSET		0x40800000
#define FIP_DECOMP_BUF_SIZE		0x400000

/* Block read buffer */
#define IO_BLOCK_BUF_OFFSET		0x41000000
#define IO_BLOCK_BUF_SIZE		0xe00000

/* Dual-FIP buffer */
#define DUAL_FIP_BUF_OFFSET		0x43400000
#define DUAL_FIP_BUF_SIZE		0x1000000

/*
 * XMODEM recovery download buffer.
 *
 * The scratch buffer is only used while the boot device is being set up, so it
 * can be safely recycled once that is done (which is exactly when the recovery
 * mode can be entered).
 */
#define XMODEM_BUF_OFFSET		SCRATCH_BUF_OFFSET
#define XMODEM_BUF_SIZE			SCRATCH_BUF_SIZE

int mtk_mmc_gpt_image_setup(uintptr_t *dev_handle, uintptr_t *image_spec,
			    uintptr_t *bkup_image_spec);
int mtk_fip_image_setup(uintptr_t *dev_handle, uintptr_t *image_spec);
void mtk_fip_location(size_t *fip_off, size_t *fip_size);
void mtk_bl2_set_dram_size(size_t size);

#ifdef MTK_XMODEM_RECOVERY
/*
 * Step-by-step tracing of the recovery hand-over, enabled by CONFIG_XMODEM_DEBUG.
 * These lines only appear when recovery actually runs, so the switch controls
 * how much a recovery says, not the cost of a normal boot.
 */
#ifdef MTK_XMODEM_DEBUG
#define XMODEM_TRACE(...)	NOTICE(__VA_ARGS__)
#else
#define XMODEM_TRACE(...)	do { } while (0)
#endif

/*
 * mtk_xmodem_recovery() and mtk_xmodem_recovery_prompt() are also declared
 * locally in bl2/bl2_image_load_v2.c: that file is shared with every other
 * platform and cannot include this header. Keep both copies in sync.
 */
/*
 * Enter XMODEM recovery mode. Returns only after a FIP with a valid ToC has
 * been received and installed as the FIP image source; anything else is
 * rejected and the user is asked to send the image again.
 */
void mtk_xmodem_recovery(const char *reason);

/*
 * Offer the operator a short window to divert the boot into XMODEM recovery,
 * for the case of a FIP that is well-formed but does not actually boot. Called
 * before anything is read from the boot device. Returns 1 if recovery was
 * requested, 0 if the window expired.
 */
int mtk_xmodem_recovery_prompt(void);

int mtk_fip_set_xmodem_source(uintptr_t dev_handle, uintptr_t image_spec);
#endif

/* Provided by each SoC */
void bl2_el3_plat_arch_setup(void);

/* The following function prototypes are provided by platform's boot device */
int mtk_plat_nor_setup(void);
int mtk_plat_nand_setup(size_t *page_size, size_t *block_size, uint64_t *size);
int mtk_plat_mmc_setup(uint32_t *num_sectors);

void mtk_plat_fip_location(size_t *fip_off, size_t *fip_size);

struct mtk_snand_platdata;
const struct mtk_snand_platdata *mtk_plat_get_snfi_platdata(void);

int mtk_plat_qspi_init(void);

#endif /* BL2_PLAT_SETUP_H */
