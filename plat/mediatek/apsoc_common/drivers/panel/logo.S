/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026, OpenWrt.org
 */

	.align	3
	.section	".rodata.panel_splash", "a"
	.type	panel_splash, %object
	.global	panel_splash
panel_splash:
	.incbin	PANEL_SPLASH_PATH
	.equ	_panel_splash_size, . - panel_splash
	.size	panel_splash, _panel_splash_size

	.section	".rodata.panel_splash_size", "a"
	.align	3
	.type	panel_splash_size, %object
	.global	panel_splash_size
panel_splash_size:
	.word	_panel_splash_size
	.size	panel_splash_size, . - panel_splash_size
