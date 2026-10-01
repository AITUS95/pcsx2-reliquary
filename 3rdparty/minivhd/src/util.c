/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Utility functions.
 *
 * Version:	@(#)util.c	1.0.4	2021/04/16
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
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <errno.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "internal.h"
uint16_t
mvhd_from_be16(uint16_t val)
{
	uint8_t* tmp = (uint8_t*)&val;
	uint16_t ret = 0;

	ret |= (uint16_t)tmp[0] << 8;
	ret |= (uint16_t)tmp[1] << 0;

	return ret;
}


uint32_t
mvhd_from_be32(uint32_t val)
{
	uint8_t* tmp = (uint8_t*)&val;
	uint32_t ret = 0;

	ret = (uint32_t)tmp[0] << 24;
	ret |= (uint32_t)tmp[1] << 16;
	ret |= (uint32_t)tmp[2] << 8;
	ret |= (uint32_t)tmp[3] << 0;

	return ret;
}


uint64_t
mvhd_from_be64(uint64_t val)
{
	uint8_t* tmp = (uint8_t*)&val;
	uint64_t ret = 0;

	ret = (uint64_t)tmp[0] << 56;
	ret |= (uint64_t)tmp[1] << 48;
	ret |= (uint64_t)tmp[2] << 40;
	ret |= (uint64_t)tmp[3] << 32;
	ret |= (uint64_t)tmp[4] << 24;
	ret |= (uint64_t)tmp[5] << 16;
	ret |= (uint64_t)tmp[6] << 8;
	ret |= (uint64_t)tmp[7] << 0;

	return ret;
}


uint16_t
mvhd_to_be16(uint16_t val)
{
	uint16_t ret = 0;
	uint8_t* tmp = (uint8_t*)&ret;

	tmp[0] = (val & 0xff00) >> 8;
	tmp[1] = (val & 0x00ff) >> 0;

	return ret;
}


uint32_t
mvhd_to_be32(uint32_t val)
{
	uint32_t ret = 0;
	uint8_t* tmp = (uint8_t*)&ret;

	tmp[0] = (val & 0xff000000) >> 24;
	tmp[1] = (val & 0x00ff0000) >> 16;
	tmp[2] = (val & 0x0000ff00) >> 8;
	tmp[3] = (val & 0x000000ff) >> 0;

	return ret;
}


uint64_t
mvhd_to_be64(uint64_t val)
{
	uint64_t ret = 0;
	uint8_t* tmp = (uint8_t*)&ret;

	tmp[0] = (uint8_t)((val & 0xff00000000000000) >> 56);
	tmp[1] = (uint8_t)((val & 0x00ff000000000000) >> 48);
	tmp[2] = (uint8_t)((val & 0x0000ff0000000000) >> 40);
	tmp[3] = (uint8_t)((val & 0x000000ff00000000) >> 32);
	tmp[4] = (uint8_t)((val & 0x00000000ff000000) >> 24);
	tmp[5] = (uint8_t)((val & 0x0000000000ff0000) >> 16);
	tmp[6] = (uint8_t)((val & 0x000000000000ff00) >> 8);
	tmp[7] = (uint8_t)((val & 0x00000000000000ff) >> 0);

	return ret;
}



uint32_t mvhd_checksum(const uint8_t* buffer, size_t size, size_t checksum_offset)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < size; i++)
		if (i < checksum_offset || i >= checksum_offset + 4)
			sum += buffer[i];
	return ~sum;
}
int64_t mvhd_ftello64(FILE* f)
{
#ifdef _WIN32
	return _ftelli64(f);
#else
	return ftello(f);
#endif
}
int mvhd_fseeko64(FILE* f, int64_t offset, int origin)
{
#ifdef _WIN32
	return _fseeki64(f, offset, origin);
#else
	return fseeko(f, offset, origin);
#endif
}
int mvhd_sync_file(FILE* f)
{
	if (fflush(f) != 0)
		return -1;
#ifdef _WIN32
	return _commit(_fileno(f));
#else
	return fsync(fileno(f));
#endif
}
int mvhd_fail(MVHDMeta* v, int error)
{
	if (!v->error)
		v->error = error;
	return -1;
}
int mvhd_read_at(MVHDMeta* v, uint64_t offset, void* buffer, size_t size)
{
	if (v->error)
		return -1;
	if (offset > INT64_MAX || offset > v->file_size || size > v->file_size - offset)
		return mvhd_fail(v, MVHD_ERR_METADATA);
	if (mvhd_fseeko64(v->f, (int64_t)offset, SEEK_SET) != 0 ||
		fread(buffer, 1, size, v->f) != size)
		return mvhd_fail(v, MVHD_ERR_FILE);
	return 0;
}
int mvhd_write_at(MVHDMeta* v, uint64_t offset, const void* buffer, size_t size)
{
	if (v->error)
		return -1;
	if (v->readonly)
		return mvhd_fail(v, MVHD_ERR_READONLY);
	if (offset > INT64_MAX || size > INT64_MAX - offset)
		return mvhd_fail(v, MVHD_ERR_INVALID_SIZE);
	if (mvhd_fseeko64(v->f, (int64_t)offset, SEEK_SET) != 0 ||
		fwrite(buffer, 1, size, v->f) != size)
		return mvhd_fail(v, MVHD_ERR_FILE);
	if (offset + size > v->file_size)
		v->file_size = offset + size;
	return 0;
}
int mvhd_flush(MVHDMeta* v)
{
	if (!v || v->error)
		return -1;
	if (!v->readonly && mvhd_sync_file(v->f) != 0)
		return mvhd_fail(v, MVHD_ERR_FILE);
	return 0;
}
int mvhd_get_error(const MVHDMeta* v) { return v->error; }
uint64_t mvhd_get_current_size(MVHDMeta* v) { return v->footer.curr_sz; }
MVHDType mvhd_get_type(MVHDMeta* v) { return (MVHDType)v->footer.disk_type; }
const char* mvhd_strerr(int error)
{
	switch (error)
	{
		case 0:
			return "success";
		case MVHD_ERR_MEM:
			return "memory allocation failed";
		case MVHD_ERR_FILE:
			return "VHD storage I/O or flush failed";
		case MVHD_ERR_NOT_VHD:
			return "not a VHD image (VHDX is unsupported)";
		case MVHD_ERR_TYPE:
			return "unsupported VHD type (differencing VHD is unsupported)";
		case MVHD_ERR_FOOTER_CHECKSUM:
			return "invalid VHD footer checksum";
		case MVHD_ERR_SPARSE_CHECKSUM:
			return "invalid VHD header checksum";
		case MVHD_ERR_INVALID_SIZE:
			return "invalid VHD logical capacity";
		case MVHD_ERR_INVALID_BLOCK_SIZE:
			return "unsupported VHD allocation block size";
		case MVHD_ERR_INVALID_PARAMS:
			return "invalid VHD sector request";
		case MVHD_ERR_METADATA:
			return "invalid or overlapping VHD metadata/blocks";
		case MVHD_ERR_READONLY:
			return "VHD image is read-only";
		default:
			return "unknown VHD error";
	}
}
