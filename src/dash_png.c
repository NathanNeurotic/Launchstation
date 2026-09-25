//--------------------------------------------------------------
//File name:   dash_png.c
//Bounded PNG decoding for dashboard artwork (libpng from PS2BUILD).
//
//Files are read through wLaunchELF's gen* file layer (so every device the
//FileBrowser understands works), size-capped, and decoded row by row with a
//streaming box filter straight into the target thumbnail. A 1024x1024 cover
//therefore never needs a 4MB intermediate buffer: memory use is one source
//row plus one accumulator row plus the output.
//Output: CT32 pixels, alpha already in the GS 0..0x80 range.
//--------------------------------------------------------------
#include "dash_internal.h"
#include <png.h>
#include <setjmp.h>

#define DASH_PNG_MAX_DIM 2048

typedef struct
{
	const u8 *data;
	int size;
	int pos;
} PngMemReader;

static void pngReadMem(png_structp png, png_bytep out, png_size_t len)
{
	PngMemReader *r = (PngMemReader *)png_get_io_ptr(png);

	if (r == NULL || r->pos + (int)len > r->size) {
		png_error(png, "truncated");
		return;
	}
	memcpy(out, r->data + r->pos, len);
	r->pos += len;
}

static void pngWarn(png_structp png, png_const_charp msg)
{
	(void)png;
	(void)msg;
}

static void pngErr(png_structp png, png_const_charp msg)
{
	(void)msg;
	longjmp(png_jmpbuf(png), 1);
}

int dashPngDecode(const u8 *data, int size, int max_w, int max_h, u32 **out, int *out_w, int *out_h)
{
	png_structp png = NULL;
	png_infop info = NULL;
	PngMemReader reader;
	png_uint_32 w, h;
	int bit_depth, color_type, interlace;
	int dw, dh, y, x;
	/* volatile: these are inspected after a libpng longjmp */
	u8 *volatile row = NULL;
	u32 *volatile acc = NULL;   /* r,g,b,a sums per destination column */
	u16 *volatile acc_n = NULL; /* samples per destination column */
	u32 *volatile dst = NULL;
	int cur_dy = 0;

	*out = NULL;
	if (data == NULL || size < 8 || png_sig_cmp((png_const_bytep)data, 0, 8))
		return -1;

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, pngErr, pngWarn);
	if (png == NULL)
		return -1;
	info = png_create_info_struct(png);
	if (info == NULL) {
		png_destroy_read_struct(&png, NULL, NULL);
		return -1;
	}

	if (setjmp(png_jmpbuf(png))) {
		free(row);
		free(acc);
		free(acc_n);
		free(dst);
		png_destroy_read_struct(&png, &info, NULL);
		return -1;
	}

	reader.data = data;
	reader.size = size;
	reader.pos = 0;
	png_set_read_fn(png, &reader, pngReadMem);
	png_read_info(png, info);
	png_get_IHDR(png, info, &w, &h, &bit_depth, &color_type, &interlace, NULL, NULL);

	if (w == 0 || h == 0 || w > DASH_PNG_MAX_DIM || h > DASH_PNG_MAX_DIM)
		png_error(png, "dimensions");
	/* Interlaced images cannot be streamed row by row; only accept small ones. */
	if (interlace != PNG_INTERLACE_NONE && (w * h > 256 * 256))
		png_error(png, "interlaced too large");

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (bit_depth == 16)
		png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	if (interlace != PNG_INTERLACE_NONE)
		png_set_interlace_handling(png);
	png_read_update_info(png, info);

	/* Fit inside max_w x max_h, never upscale. */
	dw = (int)w;
	dh = (int)h;
	if (dw > max_w) {
		dh = (int)((u64)dh * max_w / dw);
		dw = max_w;
	}
	if (dh > max_h) {
		dw = (int)((u64)dw * max_h / dh);
		dh = max_h;
	}
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;

	dst = (u32 *)memalign(128, dw * dh * 4);
	row = (u8 *)malloc(w * 4);
	acc = (u32 *)malloc(dw * 4 * sizeof(u32));
	acc_n = (u16 *)malloc(dw * sizeof(u16));
	if (dst == NULL || row == NULL || acc == NULL || acc_n == NULL)
		png_error(png, "oom");
	memset(acc, 0, dw * 4 * sizeof(u32));
	memset(acc_n, 0, dw * sizeof(u16));

	if (interlace != PNG_INTERLACE_NONE) {
		/* small interlaced image: decode fully, then box-filter */
		u8 *full = (u8 *)malloc(w * h * 4);
		png_bytep *rows;

		if (full == NULL)
			png_error(png, "oom");
		rows = (png_bytep *)malloc(h * sizeof(png_bytep));
		if (rows == NULL) {
			free(full);
			png_error(png, "oom");
		}
		for (y = 0; y < (int)h; y++)
			rows[y] = full + y * w * 4;
		png_read_image(png, rows);
		free(rows);
		for (y = 0; y < (int)h; y++) {
			int dy = (int)((u64)y * dh / h);
			memcpy(row, full + y * w * 4, w * 4);
			if (dy != cur_dy) {
				for (x = 0; x < dw; x++) {
					u32 n = acc_n[x] ? acc_n[x] : 1;
					u32 a = acc[x * 4 + 3] / n;
					dst[cur_dy * dw + x] = (acc[x * 4] / n) | ((acc[x * 4 + 1] / n) << 8) | ((acc[x * 4 + 2] / n) << 16) | (((a + 1) >> 1) << 24);
				}
				memset(acc, 0, dw * 4 * sizeof(u32));
				memset(acc_n, 0, dw * sizeof(u16));
				cur_dy = dy;
			}
			for (x = 0; x < (int)w; x++) {
				int dx = (int)((u64)x * dw / w);
				u8 *px = row + x * 4;
				acc[dx * 4] += px[0];
				acc[dx * 4 + 1] += px[1];
				acc[dx * 4 + 2] += px[2];
				acc[dx * 4 + 3] += px[3];
				acc_n[dx]++;
			}
		}
		free(full);
	} else {
		for (y = 0; y < (int)h; y++) {
			int dy = (int)((u64)y * dh / h);
			png_read_row(png, row, NULL);
			if (dy != cur_dy) {
				for (x = 0; x < dw; x++) {
					u32 n = acc_n[x] ? acc_n[x] : 1;
					u32 a = acc[x * 4 + 3] / n;
					dst[cur_dy * dw + x] = (acc[x * 4] / n) | ((acc[x * 4 + 1] / n) << 8) | ((acc[x * 4 + 2] / n) << 16) | (((a + 1) >> 1) << 24);
				}
				memset(acc, 0, dw * 4 * sizeof(u32));
				memset(acc_n, 0, dw * sizeof(u16));
				cur_dy = dy;
			}
			for (x = 0; x < (int)w; x++) {
				int dx = (int)((u64)x * dw / w);
				u8 *px = row + x * 4;
				acc[dx * 4] += px[0];
				acc[dx * 4 + 1] += px[1];
				acc[dx * 4 + 2] += px[2];
				acc[dx * 4 + 3] += px[3];
				acc_n[dx]++;
			}
		}
	}
	/* flush final destination row */
	for (x = 0; x < dw; x++) {
		u32 n = acc_n[x] ? acc_n[x] : 1;
		u32 a = acc[x * 4 + 3] / n;
		dst[cur_dy * dw + x] = (acc[x * 4] / n) | ((acc[x * 4 + 1] / n) << 8) | ((acc[x * 4 + 2] / n) << 16) | (((a + 1) >> 1) << 24);
	}

	free(row);
	free(acc);
	free(acc_n);
	row = NULL;
	acc = NULL;
	acc_n = NULL;
	png_destroy_read_struct(&png, &info, NULL);

	SyncDCache(dst, (u8 *)dst + dw * dh * 4);
	*out = dst;
	*out_w = dw;
	*out_h = dh;
	return 0;
}

int dashPngLoadFile(const char *path, int max_w, int max_h, u32 **out, int *out_w, int *out_h)
{
	int len = 0, ret;
	char *buf;

	*out = NULL;
	buf = dashLoadTextFile(path, DASH_PNG_MAX_FILE, &len);
	if (buf == NULL)
		return -1;
	ret = dashPngDecode((const u8 *)buf, len, max_w, max_h, out, out_w, out_h);
	free(buf);
	return ret;
}
//--------------------------------------------------------------
//End of file: dash_png.c
//--------------------------------------------------------------
