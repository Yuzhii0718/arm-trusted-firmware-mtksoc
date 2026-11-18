#
# Copyright (c) 2026, OpenWrt.org
#
# SPDX-License-Identifier: BSD-3-Clause
#

# The including platform has to supply, through BL2_CPPFLAGS:
#
#   PANEL_SPLASH_PATH	the blob logo.S takes its bytes from
#   MTK_PWM_CHAN_BASE	offset of PWM channel 0 from PWM_BASE
#   MTK_PWM_CHAN_WIDTH	distance between two PWM channels
#
# scripts/gen-boot-logos.py in the OpenWrt tree writes the blob. It rasterises
# the stage text with U-Boot's 8x16 console font, so a change to
# CONFIG_VIDEO_FONT_* in the U-Boot defconfig needs the blob regenerated.
BL2_CPPFLAGS		+=	-I$(APSOC_COMMON)/drivers/panel			\
				-I$(APSOC_COMMON)/drivers/pwm

BL2_SOURCES		+=	$(APSOC_COMMON)/drivers/panel/logo.S		\
				$(APSOC_COMMON)/drivers/panel/splash.c		\
				$(APSOC_COMMON)/drivers/panel/st7789p3.c	\
				$(APSOC_COMMON)/drivers/pwm/mtk_pwm.c

# mtk_spi.c registers the bus through spi_mem_init_slave(). A board that boots
# from a SPI flash already carries that file, one that boots from eMMC does not.
ifeq ($(filter drivers/mtd/spi-mem/spi_mem.c,$(BL2_SOURCES)),)
BL2_SOURCES		+=	drivers/mtd/spi-mem/spi_mem.c
endif
