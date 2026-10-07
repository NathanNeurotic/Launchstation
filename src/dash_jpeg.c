//--------------------------------------------------------------
//File name:   dash_jpeg.c
//Bounded JPEG decoding for the native dashboard image viewer.
//
//The historical wLaunchELF JPG viewer used libjpeg_ps2_addons. This keeps
//the same PS2 libjpeg foundation, but decodes from wLaunchELF's gen* file
//layer so JPG/JPEG files work anywhere the native File Browser can read.
//Rows are downsampled directly into a bounded CT32 texture instead of
//keeping a full-size decoded RGB image in EE RAM.
//--------------------------------------------------------------
#include "dash_internal.h"
#include <jpeglib.h>
#include <setjmp.h>

#define DASH_JPEG_MAX_FILE (4 * 1024 * 1024)
#define DASH_JPEG_MAX_DIM 8192

typedef struct
{
	struct jpeg_error_mgr pub;
	jmp_buf jump;
} DashJpegError;

static void dashJpegErrorExit(j_common_ptr cinfo)
{
	DashJpegError *err = (DashJpegError *)cinfo->err;
	longjmp(err->jump, 1);
}

static void flushRow(u32 *dst, int dy, int dw, u32 *acc, u32 *acc_n)
{
	int x;

	for (x = 0; x < dw; x++) {
		u32 n = acc_n[x] ? acc_n[x] : 1;
		u32 r = acc[x * 3] / n;
		u32 g = acc[x * 3 + 1] / n;
		u32 b = acc[x * 3 + 2] / n;
		dst[dy * dw + x] = r | (g << 8) | (b << 16) | (0x80u << 24);
	}
}

int dashJpegDecode(const u8 *data, int size, int max_w, int max_h,
                   u32 **out, int *out_w, int *out_h)
{
	struct jpeg_decompress_struct cinfo;
	DashJpegError jerr;
	JSAMPROW row_ptr[1];
	u8 *volatile row = NULL;
	u32 *volatile dst = NULL;
	u32 *volatile acc = NULL;
	u32 *volatile acc_n = NULL;
	int started = 0;
	int sw, sh, dw, dh, y, x, cur_dy = 0;

	if (out == NULL || out_w == NULL || out_h == NULL)
		return -1;
	*out = NULL;
	*out_w = *out_h = 0;
	if (data == NULL || size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
	    max_w < 1 || max_h < 1)
		return -1;

	memset(&cinfo, 0, sizeof(cinfo));
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = dashJpegErrorExit;
	if (setjmp(jerr.jump)) {
		if (started)
			jpeg_abort_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		free((void *)row);
		free((void *)dst);
		free((void *)acc);
		free((void *)acc_n);
		return -1;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, (unsigned char *)data, (unsigned long)size);
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
		longjmp(jerr.jump, 1);
	if (cinfo.image_width == 0 || cinfo.image_height == 0 ||
	    cinfo.image_width > DASH_JPEG_MAX_DIM ||
	    cinfo.image_height > DASH_JPEG_MAX_DIM)
		longjmp(jerr.jump, 1);

	/* Let libjpeg do a coarse 1/2, 1/4 or 1/8 reduction first. Keep enough
	 * samples for the final box filter so the viewer does not decode giant
	 * camera images at full resolution just to show them on a PS2 screen. */
	cinfo.scale_num = 1;
	cinfo.scale_denom = 1;
	while (cinfo.scale_denom < 8 &&
	       ((cinfo.image_width / cinfo.scale_denom) > (unsigned)max_w * 2 ||
	        (cinfo.image_height / cinfo.scale_denom) > (unsigned)max_h * 2))
		cinfo.scale_denom *= 2;
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);
	started = 1;

	sw = (int)cinfo.output_width;
	sh = (int)cinfo.output_height;
	if (sw < 1 || sh < 1 || cinfo.output_components != 3)
		longjmp(jerr.jump, 1);

	dw = sw;
	dh = sh;
	if (dw > max_w) {
		dh = (int)((u64)dh * max_w / dw);
		dw = max_w;
	}
	if (dh > max_h) {
		dw = (int)((u64)dw * max_h / dh);
		dh = max_h;
	}
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;

	dst = (u32 *)memalign(128, dw * dh * 4);
	row = (u8 *)malloc(sw * 3);
	acc = (u32 *)calloc(dw * 3, sizeof(u32));
	acc_n = (u32 *)calloc(dw, sizeof(u32));
	if (dst == NULL || row == NULL || acc == NULL || acc_n == NULL)
		longjmp(jerr.jump, 1);

	row_ptr[0] = (JSAMPROW)row;
	for (y = 0; y < sh; y++) {
		int dy = (int)((u64)y * dh / sh);
		if (jpeg_read_scanlines(&cinfo, row_ptr, 1) != 1)
			longjmp(jerr.jump, 1);
		if (dy != cur_dy) {
			flushRow((u32 *)dst, cur_dy, dw, (u32 *)acc, (u32 *)acc_n);
			memset((void *)acc, 0, dw * 3 * sizeof(u32));
			memset((void *)acc_n, 0, dw * sizeof(u32));
			cur_dy = dy;
		}
		for (x = 0; x < sw; x++) {
			int dx = (int)((u64)x * dw / sw);
			u8 *px = (u8 *)row + x * 3;
			((u32 *)acc)[dx * 3] += px[0];
			((u32 *)acc)[dx * 3 + 1] += px[1];
			((u32 *)acc)[dx * 3 + 2] += px[2];
			((u32 *)acc_n)[dx]++;
		}
	}
	flushRow((u32 *)dst, cur_dy, dw, (u32 *)acc, (u32 *)acc_n);

	jpeg_finish_decompress(&cinfo);
	started = 0;
	jpeg_destroy_decompress(&cinfo);
	free((void *)row);
	free((void *)acc);
	free((void *)acc_n);

	SyncDCache((void *)dst, (u8 *)dst + dw * dh * 4);
	*out = (u32 *)dst;
	*out_w = dw;
	*out_h = dh;
	return 0;
}

int dashJpegLoadFile(const char *path, int max_w, int max_h,
                     u32 **out, int *out_w, int *out_h)
{
	int len = 0, ret;
	char *buf;

	if (out == NULL)
		return -1;
	*out = NULL;
	buf = dashLoadTextFile(path, DASH_JPEG_MAX_FILE, &len);
	if (buf == NULL)
		return -1;
	ret = dashJpegDecode((const u8 *)buf, len, max_w, max_h, out, out_w, out_h);
	free(buf);
	return ret;
}
//--------------------------------------------------------------
//End of file: dash_jpeg.c
//--------------------------------------------------------------
