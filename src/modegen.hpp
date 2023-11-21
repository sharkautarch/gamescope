#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <xf86drmMode.h>
#include <span>

void generate_cvt_mode(drmModeModeInfo *mode, int hdisplay, int vdisplay,
	float vrefresh, bool reduced, bool interlaced);
void generate_fixed_mode(drmModeModeInfo *mode, const drmModeModeInfo *base,
	int vrefresh, bool use_tuned_clocks, unsigned int use_vfp);
void initialize_custom_modes(const char *dir);
std::span<uint32_t> get_custom_framerates(const char *pnp, const char *model);
void generate_custom_mode(drmModeModeInfo *mode, const drmModeModeInfo *base, int vrefresh, const char *pnp, const char *model);
