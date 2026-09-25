/*
 * Vendored from ps2sdk ee/sbv/src/patch_romdrv_set_rom1_info.c (AFL-2.0).
 * Copyright (c) 2003  Marcus R. Brown <mrbrown@0xd6.org>
 *
 * PS2BUILD's libpatches predates this helper, which SifIopRebootBuffer needs.
 * The only change from upstream is a private, cache-line aligned transfer
 * buffer instead of libpatches' internal smem_buf.
 */

#include <tamtypes.h>
#include <kernel.h>
#include <sifdma.h>
#include <sifrpc.h>
#include <smod.h>

int sbv_patch_romdrv_set_rom1_info(void *iop_reset_addr, int romdir_size);

static u8 romdrv_patch_buf[64] __attribute__((aligned(64)));

int sbv_patch_romdrv_set_rom1_info(void *iop_reset_addr, int romdir_size)
{
	SifRpcReceiveData_t rdata;
	smod_mod_info_t romdrv_info;
	u32 *data;
	SifDmaTransfer_t dmat[1];
	int trid;

	/* Find the romdrv module.  */
	if (!smod_get_mod_by_name("ROM_file_driver", &romdrv_info))
		return -1;

	dmat[0].src = romdrv_patch_buf;
	dmat[0].size = 64;
	dmat[0].dest = (void *)(romdrv_info.text_start + romdrv_info.text_size + romdrv_info.data_size + 0x40);
	dmat[0].attr = 0;

	/* Get offset into rom mount information in bss section */
	SyncDCache(dmat[0].src, ((u8 *)dmat[0].src) + dmat[0].size);
	if (sceSifGetOtherData(&rdata, dmat[0].dest, dmat[0].src, dmat[0].size, 0) < 0)
		return 1;
	data = UNCACHED_SEG(dmat[0].src);
	/* Ensure data contains rom0: information */
	if (data[0] != 0xBFC00000 || data[1] <= data[0] || data[2] <= data[1])
		return 1;

	/* ImageStart for rom1: */
	data[3] = (u32)iop_reset_addr;
	/* RomdirStart for rom1: */
	data[4] = data[3];
	/* RomdirEnd for rom1: */
	data[5] = data[3] + romdir_size;

	sceSifWriteBackDCache(dmat[0].src, dmat[0].size);
	while (!(trid = sceSifSetDma(dmat, sizeof(dmat) / sizeof(dmat[0]))));
	while (sceSifDmaStat(trid) >= 0);
	return 0;
}
