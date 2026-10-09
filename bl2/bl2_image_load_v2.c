/*
 * Copyright (c) 2016-2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdint.h>

#include <arch.h>
#include <arch_helpers.h>
#include "bl2_private.h"
#include <common/bl_common.h>
#include <common/debug.h>
#include <common/desc_image_load.h>
#include <drivers/auth/auth_mod.h>
#include <plat/common/platform.h>

#include <platform_def.h>

#if defined(DUAL_FIP) || defined(MTK_XMODEM_RECOVERY)
void bl2_plat_handle_post_image_load_err(unsigned int image_id);
#endif
#ifdef DUAL_FIP
int mtk_fip_image_setup_next_slot(void);
#endif
#ifdef MTK_XMODEM_RECOVERY
/*
 * MediaTek XMODEM recovery hook, used by the last-resort path below. This file
 * is shared with every other platform and cannot include the platform's
 * bl2_plat_setup.h, where the very same prototype lives: keep both in sync.
 *
 * The manual diversion is offered by the platform before anything is read from
 * the boot device (bl2_plat_preload_setup()), not from here.
 */
void mtk_xmodem_recovery(const char *reason);
#endif

/*******************************************************************************
 * This function loads SCP_BL2/BL3x images and returns the ep_info for
 * the next executable image.
 ******************************************************************************/
struct entry_point_info *bl2_load_images(void)
{
	bl_params_t *bl2_to_next_bl_params;
	bl_load_info_t *bl2_load_info;
	const bl_load_info_node_t *bl2_node_info;
	int plat_setup_done = 0;
	int err;

#ifdef DUAL_FIP
	bool dual_fip_retry = true;
	int ret;
#endif

#if defined(DUAL_FIP) || defined(MTK_XMODEM_RECOVERY)
retry:
	plat_setup_done = 0;
#endif

	/*
	 * Get information about the images to load.
	 */
	bl2_load_info = plat_get_bl_image_load_info();
	assert(bl2_load_info != NULL);
	assert(bl2_load_info->head != NULL);
	assert(bl2_load_info->h.type == PARAM_BL_LOAD_INFO);
	assert(bl2_load_info->h.version >= VERSION_2);
	bl2_node_info = bl2_load_info->head;

	while (bl2_node_info != NULL) {
		/*
		 * Perform platform setup before loading the image,
		 * if indicated in the image attributes AND if NOT
		 * already done before.
		 */
		if ((bl2_node_info->image_info->h.attr &
		    IMAGE_ATTRIB_PLAT_SETUP) != 0U) {
			if (plat_setup_done != 0) {
				WARN("BL2: Platform setup already done!!\n");
			} else {
				INFO("BL2: Doing platform setup\n");
				bl2_platform_setup();
				plat_setup_done = 1;
			}
		}

		err = bl2_plat_handle_pre_image_load(bl2_node_info->image_id);
		if (err != 0) {
			ERROR("BL2: Failure in pre image load handling (%i)\n", err);
			goto image_error;
		}

		if ((bl2_node_info->image_info->h.attr &
		    IMAGE_ATTRIB_SKIP_LOADING) == 0U) {
			INFO("BL2: Loading image id %u\n", bl2_node_info->image_id);
			err = load_auth_image(bl2_node_info->image_id,
				bl2_node_info->image_info);
			if (err != 0) {
				ERROR("BL2: Failed to load image id %u (%i)\n",
				      bl2_node_info->image_id, err);
				goto image_error;
			}
		} else {
			INFO("BL2: Skip loading image id %u\n", bl2_node_info->image_id);
		}

		/* Allow platform to handle image information. */
		err = bl2_plat_handle_post_image_load(bl2_node_info->image_id);
		if (err != 0) {
			ERROR("BL2: Failure in post image load handling (%i)\n", err);
			goto image_error;
		}

		/* Go to next image */
		bl2_node_info = bl2_node_info->next_load_info;
		continue;

image_error:
#if defined(DUAL_FIP) || defined(MTK_XMODEM_RECOVERY)
		/* Unwind whatever the failed attempt changed in image_info */
		bl2_plat_handle_post_image_load_err(bl2_node_info->image_id);
#endif

#ifdef DUAL_FIP
		if (dual_fip_retry) {
			/* Try next FIP slot */
			ret = mtk_fip_image_setup_next_slot();
			if (!ret) {
				dual_fip_retry = false;
				goto retry;
			}
		}
#endif

#ifdef MTK_XMODEM_RECOVERY
		/*
		 * Nothing below this point is allowed to brick the device: no
		 * matter what made the image unusable (damaged or missing FIP,
		 * corrupted BL3x, failed decompression or authentication), fall
		 * back to XMODEM. mtk_xmodem_recovery() returns only once a FIP
		 * with a valid ToC has been downloaded, and the whole loading
		 * sequence is then retried from the beginning -- for as long as
		 * it takes.
		 */
		mtk_xmodem_recovery("stored BL31 + U-Boot FIP is unusable");
		NOTICE("BL2: restarting the boot sequence from the downloaded FIP\n");
		goto retry;
#endif

		plat_error_handler(err);
	}

	/*
	 * Get information to pass to the next image.
	 */
	bl2_to_next_bl_params = plat_get_next_bl_params();
	assert(bl2_to_next_bl_params != NULL);
	assert(bl2_to_next_bl_params->head != NULL);
	assert(bl2_to_next_bl_params->h.type == PARAM_BL_PARAMS);
	assert(bl2_to_next_bl_params->h.version >= VERSION_2);
	assert(bl2_to_next_bl_params->head->ep_info != NULL);

	/* Populate arg0 for the next BL image if not already provided */
	if (bl2_to_next_bl_params->head->ep_info->args.arg0 == (u_register_t)0)
		bl2_to_next_bl_params->head->ep_info->args.arg0 =
					(u_register_t)bl2_to_next_bl_params;

	/* Flush the parameters to be passed to next image */
	plat_flush_next_bl_params();

	return bl2_to_next_bl_params->head->ep_info;
}
