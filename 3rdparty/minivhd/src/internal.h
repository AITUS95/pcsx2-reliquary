/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Internal definitions.
 *
 * Version:	@(#)internal.h	1.0.1	2021/03/15
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
#ifndef MINIVHD_INTERNAL_H
#define MINIVHD_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "minivhd.h"
#define MVHD_FOOTER_SIZE 512
#define MVHD_SPARSE_SIZE 1024
#define MVHD_SECTOR_SIZE 512
#define MVHD_SPARSE_BLK UINT32_MAX
typedef struct MVHDFooter
{
	uint8_t cookie[8];
	uint32_t features;
	uint32_t fi_fmt_vers;
	uint64_t data_offset;
	uint32_t timestamp;
	uint8_t cr_app[4];
	uint32_t cr_vers;
	uint8_t cr_host_os[4];
	uint64_t orig_sz;
	uint64_t curr_sz;
	struct
	{
		uint16_t cyl;
		uint8_t heads;
		uint8_t spt;
	} geom;
	uint32_t disk_type;
	uint32_t checksum;
	uint8_t uuid[16];
	uint8_t saved_st;
	uint8_t reserved[427];
} MVHDFooter;

typedef struct MVHDSparseHeader
{
	uint8_t cookie[8];
	uint64_t data_offset;
	uint64_t bat_offset;
	uint32_t head_vers;
	uint32_t max_bat_ent;
	uint32_t block_sz;
	uint32_t checksum;
	uint8_t par_uuid[16];
	uint32_t par_timestamp;
	uint32_t reserved_1;
	uint8_t par_utf16_name[512];
	struct
	{
		uint32_t plat_code;
		uint32_t plat_data_space;
		uint32_t plat_data_len;
		uint32_t reserved;
		uint64_t plat_data_offset;
	} par_loc_entry[8];
	uint8_t reserved_2[256];
} MVHDSparseHeader;


struct MVHDMeta
{
	FILE* f;
	bool readonly;
	int error;
	uint64_t file_size;
	MVHDFooter footer;
	MVHDSparseHeader sparse;
	uint32_t* block_offset;
	uint8_t* bitmap;
	uint32_t bitmap_size;
};
#ifdef __cplusplus
extern "C" {
#endif
uint16_t mvhd_from_be16(uint16_t);
uint32_t mvhd_from_be32(uint32_t);
uint64_t mvhd_from_be64(uint64_t);
uint16_t mvhd_to_be16(uint16_t);
uint32_t mvhd_to_be32(uint32_t);
uint64_t mvhd_to_be64(uint64_t);
void mvhd_buffer_to_footer(MVHDFooter*, uint8_t*);
void mvhd_buffer_to_header(MVHDSparseHeader*, uint8_t*);
void mvhd_footer_to_buffer(MVHDFooter*, uint8_t*);
void mvhd_header_to_buffer(MVHDSparseHeader*, uint8_t*);
uint32_t mvhd_checksum(const uint8_t*, size_t, size_t);
int64_t mvhd_ftello64(FILE*);
int mvhd_fseeko64(FILE*, int64_t, int);
int mvhd_sync_file(FILE*);
int mvhd_fail(MVHDMeta*, int);
int mvhd_read_at(MVHDMeta*, uint64_t, void*, size_t);
int mvhd_write_at(MVHDMeta*, uint64_t, const void*, size_t);
#ifdef __cplusplus
}
#endif
#endif
