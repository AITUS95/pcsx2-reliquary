/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 * Version:	@(#)create.c	1.0.3	2021/04/16
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
#include <time.h>
#include "internal.h"
MVHDMeta* mvhd_create_file(FILE* file, uint64_t size, int* error)
{
	if (!error || !file)
		return NULL;
	*error = 0;
	if (!size || size > MVHD_MAX_SIZE_IN_BYTES || size % 512)
	{
		*error = MVHD_ERR_INVALID_SIZE;
		return NULL;
	}
	MVHDMeta v = {0};
	v.f = file;
	const uint32_t block_size = 512 * 1024;
	memcpy(v.footer.cookie, "conectix", 8);
	v.footer.features = 2;
	v.footer.fi_fmt_vers = 0x10000;
	v.footer.data_offset = 512;
	time_t now = time(NULL);
	v.footer.timestamp = now > 946684800 ? (uint32_t)(now - 946684800) : 0;
	memcpy(v.footer.cr_app, "P2HD", 4);
	v.footer.cr_vers = 0x10000;
	memcpy(v.footer.cr_host_os, "Wi2k", 4);
	v.footer.orig_sz = v.footer.curr_sz = size;
	MVHDGeom geom = mvhd_calculate_geometry(size);
	v.footer.geom.cyl = geom.cyl;
	v.footer.geom.heads = geom.heads;
	v.footer.geom.spt = geom.spt;
	v.footer.disk_type = MVHD_TYPE_DYNAMIC;
	/* UUID is informational; PS2 HDD identity comes from ATA and .hddid. */
	uint64_t seed = (uint64_t)now ^ (uintptr_t)file ^ (uint64_t)clock();
	for (unsigned i = 0; i < 16; i++)
	{
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		v.footer.uuid[i] = (uint8_t)seed;
	}
	v.footer.uuid[6] = (v.footer.uuid[6] & 15) | 64;
	v.footer.uuid[8] = (v.footer.uuid[8] & 63) | 128;
	memcpy(v.sparse.cookie, "cxsparse", 8);
	v.sparse.data_offset = UINT64_MAX;
	v.sparse.bat_offset = 1536;
	v.sparse.head_vers = 0x10000;
	v.sparse.max_bat_ent = (uint32_t)((size + block_size - 1) / block_size);
	v.sparse.block_sz = block_size;
	uint8_t footer[512], header[1024], bat[4096];
	mvhd_footer_to_buffer(&v.footer, footer);
	v.footer.checksum = mvhd_checksum(footer, 512, 64);
	mvhd_footer_to_buffer(&v.footer, footer);
	mvhd_header_to_buffer(&v.sparse, header);
	v.sparse.checksum = mvhd_checksum(header, 1024, 36);
	mvhd_header_to_buffer(&v.sparse, header);
	if (mvhd_write_at(&v, 0, footer, 512) || mvhd_write_at(&v, 512, header, 1024))
		goto failure;
	memset(bat, 0xff, sizeof(bat));
	uint64_t bat_size = ((uint64_t)v.sparse.max_bat_ent * 4 + 511) & ~UINT64_C(511);
	for (uint64_t offset = 0; offset < bat_size;)
	{
		size_t count = bat_size - offset > sizeof(bat) ? sizeof(bat) : (size_t)(bat_size - offset);
		if (mvhd_write_at(&v, 1536 + offset, bat, count))
			goto failure;
		offset += count;
	}
	if (mvhd_write_at(&v, 1536 + bat_size, footer, 512) || mvhd_flush(&v))
		goto failure;
	return mvhd_open_file(file, 0, error);
failure:
	*error = v.error;
	return NULL;
}
