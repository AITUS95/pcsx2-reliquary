/*
 * MiniVHD	Minimalist VHD implementation in C.
 *		MiniVHD is a minimalist implementation of read/write/creation
 *		of VHD files. It is designed to read and write to VHD files
 *		at a sector level. It does not enable file access, or provide
 *		mounting options. Those features are left to more advanced
 *		libraries and/or the operating system.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Definitions for the MiniVHD library.
 *
 * Version:	@(#)minivhd.h	1.0.3	2021/04/16
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
#ifndef MINIVHD_H
#define MINIVHD_H
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MVHDAPI
#define MVHD_MAX_SIZE_IN_BYTES UINT64_C(0x1fe00000000)
typedef enum MVHDError
{
	MVHD_ERR_MEM = -128,
	MVHD_ERR_FILE,
	MVHD_ERR_NOT_VHD,
	MVHD_ERR_TYPE,
	MVHD_ERR_FOOTER_CHECKSUM,
	MVHD_ERR_SPARSE_CHECKSUM,
	MVHD_ERR_INVALID_SIZE,
	MVHD_ERR_INVALID_BLOCK_SIZE,
	MVHD_ERR_INVALID_PARAMS,
	MVHD_ERR_METADATA,
	MVHD_ERR_READONLY
} MVHDError;
typedef enum MVHDType
{
	MVHD_TYPE_FIXED = 2,
	MVHD_TYPE_DYNAMIC = 3,
	MVHD_TYPE_DIFF = 4
} MVHDType;
typedef struct MVHDGeom
{
	uint16_t cyl;
	uint8_t heads;
	uint8_t spt;
} MVHDGeom;
typedef struct MVHDMeta MVHDMeta;
/* The caller owns FILE, including on failure. All APIs return -1 on I/O failure;
 * errors poison the handle so later operations cannot report success. */
MVHDMeta* mvhd_open_file(FILE* file, int readonly, int* error);
MVHDMeta* mvhd_create_file(FILE* file, uint64_t size, int* error);
void mvhd_close(MVHDMeta* vhd);
int mvhd_flush(MVHDMeta* vhd);
int mvhd_get_error(const MVHDMeta* vhd);
uint64_t mvhd_get_current_size(MVHDMeta* vhd);
MVHDType mvhd_get_type(MVHDMeta* vhd);
MVHDGeom mvhd_calculate_geometry(uint64_t size);
const char* mvhd_strerr(int error);
/* Zero means the complete request succeeded, -1 means failure (no truncation). */
int mvhd_read_sectors(MVHDMeta* vhd, uint32_t sector, int count, void* data);
int mvhd_write_sectors(MVHDMeta* vhd, uint32_t sector, int count, const void* data);
#ifdef __cplusplus
}
#endif
#endif
