/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		VHD management functions (open, close, read write etc)
 *
 * Version:	@(#)manage.c	1.0.4	2021/04/16
 *
 * Authors:	Sherman Perry, <shermperry@gmail.com>
 *		Fred N. van Kempen, <waltje@varcem.com>
 *
 *		Copyright 2019-2021 Sherman Perry.
 *		Copyright 2021 Fred N. van Kempen.
 *
 *		MIT License
 *
 *		Permission is hereby granted, free of  charge, to any person
 *		obtaining a copy of this software  and associated documenta-
 *		tion files (the "Software"), to deal in the Software without
 *		restriction, including without limitation the rights to use,
 *		copy, modify, merge, publish, distribute, sublicense, and/or
 *		sell copies of  the Software, and  to permit persons to whom
 *		the Software is furnished to do so, subject to the following
 *		conditions:
 *
 *		The above  copyright notice and this permission notice shall
 *		be included in  all copies or  substantial  portions of  the
 *		Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING  BUT NOT LIMITED TO THE  WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN  NO EVENT  SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER  IN AN ACTION OF  CONTRACT, TORT OR  OTHERWISE, ARISING
 * FROM, OUT OF  O R IN  CONNECTION WITH THE  SOFTWARE OR  THE USE  OR  OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <string.h>
#include "internal.h"
MVHDAPI MVHDGeom
mvhd_calculate_geometry(uint64_t size)
{
	MVHDGeom chs;
	uint32_t ts = (uint32_t)(size / MVHD_SECTOR_SIZE);
	uint32_t spt, heads, cyl, cth;

	if (ts > 65535 * 16 * 255)
	{
		ts = 65535 * 16 * 255;
	}

	if (ts >= 65535 * 16 * 63)
	{
		spt = 255;
		heads = 16;
		cth = ts / spt;
	}
	else
	{
		spt = 17;
		cth = ts / spt;
		heads = (cth + 1023) / 1024;
		if (heads < 4)
		{
			heads = 4;
		}
		if (cth >= (heads * 1024) || heads > 16)
		{
			spt = 31;
			heads = 16;
			cth = ts / spt;
		}
		if (cth >= (heads * 1024))
		{
			spt = 63;
			heads = 16;
			cth = ts / spt;
		}
	}

	cyl = cth / heads;
	chs.heads = heads;
	chs.spt = spt;
	chs.cyl = cyl;

	return chs;
}



static int compare_offsets(const void* a, const void* b)
{
	uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
	return (x > y) - (x < y);
}
MVHDMeta* mvhd_open_file(FILE* file, int readonly, int* error)
{
	if (!error || !file)
		return NULL;
	*error = 0;
	MVHDMeta* v = calloc(1, sizeof(*v));
	if (!v)
	{
		*error = MVHD_ERR_MEM;
		return NULL;
	}
	v->f = file;
	v->readonly = readonly != 0;
	uint8_t footer[512], header[1024], copy[512];
	if (mvhd_fseeko64(file, 0, SEEK_END) != 0)
		goto io_error;
	int64_t file_size = mvhd_ftello64(file);
	if (file_size < 512)
		goto metadata_error;
	v->file_size = (uint64_t)file_size;
	if (mvhd_read_at(v, v->file_size - 512, footer, 512))
		goto failure;
	if (memcmp(footer, "conectix", 8))
	{
		v->error = MVHD_ERR_NOT_VHD;
		goto failure;
	}
	mvhd_buffer_to_footer(&v->footer, footer);
	if (v->footer.checksum != mvhd_checksum(footer, 512, 64))
	{
		v->error = MVHD_ERR_FOOTER_CHECKSUM;
		goto failure;
	}
	if (v->footer.fi_fmt_vers != 0x10000 || !(v->footer.features & 2) || (v->footer.features & ~3u))
		goto metadata_error;
	if (!v->footer.curr_sz || v->footer.curr_sz > MVHD_MAX_SIZE_IN_BYTES || v->footer.curr_sz % 512)
	{
		v->error = MVHD_ERR_INVALID_SIZE;
		goto failure;
	}
	if (v->footer.disk_type == MVHD_TYPE_FIXED)
	{
		if (v->file_size != v->footer.curr_sz + 512 || v->footer.data_offset != UINT64_MAX)
			goto metadata_error;
		return v;
	}
	if (v->footer.disk_type != MVHD_TYPE_DYNAMIC)
	{
		v->error = MVHD_ERR_TYPE;
		goto failure;
	}
	if (v->file_size % 512 || v->footer.data_offset < 512 || v->footer.data_offset % 512 ||
		v->footer.data_offset > v->file_size - 512 ||
		v->file_size - 512 - v->footer.data_offset < 1024)
		goto metadata_error;
	if (mvhd_read_at(v, 0, copy, 512) || mvhd_read_at(v, v->footer.data_offset, header, 1024))
		goto failure;
	if (memcmp(copy, footer, 512) || memcmp(header, "cxsparse", 8))
		goto metadata_error;
	mvhd_buffer_to_header(&v->sparse, header);
	if (v->sparse.checksum != mvhd_checksum(header, 1024, 36))
	{
		v->error = MVHD_ERR_SPARSE_CHECKSUM;
		goto failure;
	}
	if (v->sparse.head_vers != 0x10000 || v->sparse.data_offset != UINT64_MAX)
		goto metadata_error;
	uint32_t block_size = v->sparse.block_sz;
	if (block_size < 512 * 1024 || block_size > 32 * 1024 * 1024 || (block_size & (block_size - 1)))
	{
		v->error = MVHD_ERR_INVALID_BLOCK_SIZE;
		goto failure;
	}
	uint64_t required = (v->footer.curr_sz + block_size - 1) / block_size;
	uint64_t maximum = (MVHD_MAX_SIZE_IN_BYTES + block_size - 1) / block_size;
	if (v->sparse.max_bat_ent < required || v->sparse.max_bat_ent > maximum)
		goto metadata_error;
	uint64_t bat_size = ((uint64_t)v->sparse.max_bat_ent * 4 + 511) & ~UINT64_C(511);
	if (v->sparse.bat_offset < v->footer.data_offset + 1024 || v->sparse.bat_offset % 512 ||
		v->sparse.bat_offset > v->file_size - 512 || bat_size > v->file_size - 512 - v->sparse.bat_offset)
		goto metadata_error;
	v->bitmap_size = ((block_size / 512 / 8 + 511) / 512) * 512;
	v->bitmap = calloc(1, v->bitmap_size);
	v->block_offset = calloc(v->sparse.max_bat_ent, 4);
	uint32_t* sorted = calloc(v->sparse.max_bat_ent, 4);
	if (!v->bitmap || !v->block_offset || !sorted)
	{
		free(sorted);
		v->error = MVHD_ERR_MEM;
		goto failure;
	}
	if (mvhd_read_at(v, v->sparse.bat_offset, v->block_offset, (size_t)v->sparse.max_bat_ent * 4))
	{
		free(sorted);
		goto failure;
	}
	uint32_t allocated = 0;
	uint64_t metadata_end = v->sparse.bat_offset + bat_size;
	uint64_t extent = (uint64_t)block_size + v->bitmap_size;
	for (uint32_t i = 0; i < v->sparse.max_bat_ent; i++)
	{
		uint32_t entry = mvhd_from_be32(v->block_offset[i]);
		v->block_offset[i] = entry;
		if (entry == MVHD_SPARSE_BLK)
			continue;
		uint64_t start = (uint64_t)entry * 512;
		if (i >= required || start < metadata_end || start > v->file_size - 512 ||
			extent > v->file_size - 512 - start)
		{
			free(sorted);
			goto metadata_error;
		}
		sorted[allocated++] = entry;
	}
	qsort(sorted, allocated, 4, compare_offsets);
	for (uint32_t i = 1; i < allocated; i++)
	{
		if ((uint64_t)sorted[i - 1] * 512 + extent > (uint64_t)sorted[i] * 512)
		{
			free(sorted);
			goto metadata_error;
		}
	}
	free(sorted);
	return v;
io_error:
	v->error = MVHD_ERR_FILE;
	goto failure;
metadata_error:
	v->error = MVHD_ERR_METADATA;
failure:
	*error = v->error;
	mvhd_close(v);
	return NULL;
}
void mvhd_close(MVHDMeta* v)
{
	if (!v)
		return;
	free(v->block_offset);
	free(v->bitmap);
	free(v);
}
