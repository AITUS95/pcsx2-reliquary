/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Sector reading and writing implementations.
 *
 * Version:	@(#)io.c	1.0.3	2021/04/16
 *
 * Author:	Sherman Perry, <shermperry@gmail.com>
 *
 *		Copyright 2019-2021 Sherman Perry.
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
#include <string.h>
#include "internal.h"
static int validate(MVHDMeta* v, uint32_t sector, int count, const void* data)
{
	if (!v || v->error)
		return -1;
	uint64_t sectors = v->footer.curr_sz / 512;
	if (count < 0 || sector > sectors || (uint64_t)count > sectors - sector || (count && !data))
		return mvhd_fail(v, MVHD_ERR_INVALID_PARAMS);
	return 0;
}
static bool all_zero(const uint8_t* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (data[i])
			return false;
	return true;
}
static int allocate_block(MVHDMeta* v, uint32_t block)
{
	uint64_t start = v->file_size - 512;
	uint64_t extent = (uint64_t)v->sparse.block_sz + v->bitmap_size;
	/* BAT offsets are 32-bit sectors, including metadata overhead. */
	if (start / 512 >= UINT32_MAX || (start + extent) / 512 >= UINT32_MAX)
		return mvhd_fail(v, MVHD_ERR_INVALID_SIZE);
	uint8_t footer[512], zeros[64 * 1024] = {0};
	mvhd_footer_to_buffer(&v->footer, footer);
	/* Preserve a durable trailing footer before overwriting the old one. An
     * interrupted allocation leaves an unreferenced, fully bounded extent. */
	if (mvhd_write_at(v, start + extent, footer, 512) || mvhd_flush(v))
		return -1;
	/* Every sector in a newly initialized block is valid zero data. Marking all
     * bits present avoids later bitmap publication for our own allocations. */
	memset(zeros, 0xff, v->bitmap_size);
	for (uint64_t pos = 0; pos < extent;)
	{
		size_t count = extent - pos > sizeof(zeros) ? sizeof(zeros) : (size_t)(extent - pos);
		if (mvhd_write_at(v, start + pos, zeros, count))
			return -1;
		if (!pos)
			memset(zeros, 0, v->bitmap_size);
		pos += count;
	}
	if (mvhd_flush(v))
		return -1;
	uint32_t entry = mvhd_to_be32((uint32_t)(start / 512));
	if (mvhd_write_at(v, v->sparse.bat_offset + (uint64_t)block * 4, &entry, 4) || mvhd_flush(v))
		return -1;
	v->block_offset[block] = (uint32_t)(start / 512);
	return 0;
}
int mvhd_read_sectors(MVHDMeta* v, uint32_t sector, int count, void* data)
{
	if (validate(v, sector, count, data))
		return -1;
	if (!count)
		return 0;
	if (v->footer.disk_type == MVHD_TYPE_FIXED)
		return mvhd_read_at(v, (uint64_t)sector * 512, data, (size_t)count * 512);
	uint8_t* dst = data;
	uint32_t per_block = v->sparse.block_sz / 512;
	while (count)
	{
		uint32_t block = sector / per_block, index = sector % per_block;
		uint32_t transfer = per_block - index < (uint32_t)count ? per_block - index : (uint32_t)count;
		if (v->block_offset[block] == MVHD_SPARSE_BLK)
		{
			memset(dst, 0, (size_t)transfer * 512);
		}
		else
		{
			uint64_t start = (uint64_t)v->block_offset[block] * 512;
			if (mvhd_read_at(v, start, v->bitmap, v->bitmap_size))
				return -1;
			/* Coalesce adjacent present/absent sectors. Bitmap bits are MSB-first. */
			for (uint32_t i = 0; i < transfer;)
			{
				uint32_t bit = index + i;
				bool present = (v->bitmap[bit / 8] & (0x80 >> (bit % 8))) != 0;
				uint32_t end = i + 1;
				while (end < transfer)
				{
					uint32_t next = index + end;
					if (((v->bitmap[next / 8] & (0x80 >> (next % 8))) != 0) != present)
						break;
					end++;
				}
				size_t bytes = (size_t)(end - i) * 512;
				if (present)
				{
					if (mvhd_read_at(v, start + v->bitmap_size + (uint64_t)bit * 512, dst + (size_t)i * 512, bytes))
						return -1;
				}
				else
					memset(dst + (size_t)i * 512, 0, bytes);
				i = end;
			}
		}
		sector += transfer;
		count -= (int)transfer;
		dst += (size_t)transfer * 512;
	}
	return 0;
}
int mvhd_write_sectors(MVHDMeta* v, uint32_t sector, int count, const void* data)
{
	if (validate(v, sector, count, data))
		return -1;
	if (v->readonly)
		return mvhd_fail(v, MVHD_ERR_READONLY);
	if (!count)
		return 0;
	if (v->footer.disk_type == MVHD_TYPE_FIXED)
	{
		if (mvhd_write_at(v, (uint64_t)sector * 512, data, (size_t)count * 512))
			return -1;
		return fflush(v->f) == 0 ? 0 : mvhd_fail(v, MVHD_ERR_FILE);
	}
	const uint8_t* src = data;
	uint32_t per_block = v->sparse.block_sz / 512;
	while (count)
	{
		uint32_t block = sector / per_block, index = sector % per_block;
		uint32_t transfer = per_block - index < (uint32_t)count ? per_block - index : (uint32_t)count;
		if (v->block_offset[block] != MVHD_SPARSE_BLK || !all_zero(src, (size_t)transfer * 512))
		{
			if (v->block_offset[block] == MVHD_SPARSE_BLK && allocate_block(v, block))
				return -1;
			uint64_t start = (uint64_t)v->block_offset[block] * 512;
			if (mvhd_read_at(v, start, v->bitmap, v->bitmap_size) ||
				mvhd_write_at(v, start + v->bitmap_size + (uint64_t)index * 512, src, (size_t)transfer * 512))
				return -1;
			bool changed = false;
			for (uint32_t i = index; i < index + transfer; i++)
			{
				uint8_t mask = (uint8_t)(0x80 >> (i % 8));
				changed |= !(v->bitmap[i / 8] & mask);
				v->bitmap[i / 8] |= mask;
			}
			if (changed)
			{
				/* Publish presence only after the corresponding data is durable. */
				if (mvhd_flush(v) || mvhd_write_at(v, start, v->bitmap, v->bitmap_size) || mvhd_flush(v))
					return -1;
			}
		}
		sector += transfer;
		count -= (int)transfer;
		src += (size_t)transfer * 512;
	}
	/* Check buffered storage errors now; mvhd_flush supplies durability. */
	return fflush(v->f) == 0 ? 0 : mvhd_fail(v, MVHD_ERR_FILE);
}
