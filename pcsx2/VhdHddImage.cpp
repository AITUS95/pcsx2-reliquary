// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "VhdHddImage.h"

#include "common/Error.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "minivhd.h"

#include <cerrno>
#include <climits>

VhdHddImage::~VhdHddImage()
{
	Close();
}

bool VhdHddImage::Open(const std::string& path, Error* error, bool readonly)
{
	return OpenShared(path, error, readonly, FileSystem::FileShareMode::DenyWrite);
}

bool VhdHddImage::OpenShared(const std::string& path, Error* error, bool readonly, FileSystem::FileShareMode share_mode)
{
	if (!Close(error))
		return false;
	m_file = FileSystem::OpenManagedSharedCFile(path.c_str(), readonly ? "rb" : "r+b",
		share_mode, error);
	if (!m_file)
		return false;

	int code = 0;
	m_vhd = mvhd_open_file(m_file.get(), readonly, &code);
	if (m_vhd)
		return true;
	Error::SetString(error, mvhd_strerr(code));
	m_file.reset();
	return false;
}

bool VhdHddImage::Create(const std::string& path, u64 size, Error* error)
{
	if (!Close(error))
		return false;

	m_file = FileSystem::OpenManagedCFile(path.c_str(), "w+bx", error);
	if (!m_file)
		return false;

	int code = 0;
	m_vhd = mvhd_create_file(m_file.get(), size, &code);
	if (m_vhd)
		return true;
	Error::SetString(error, mvhd_strerr(code));
	m_file.reset();
	FileSystem::DeleteFilePath(path.c_str());
	return false;
}

bool VhdHddImage::Close(Error* error)
{
	bool result = true;
	if (m_vhd)
	{
		result = Flush(error);
		mvhd_close(m_vhd);
		m_vhd = nullptr;
	}
	if (m_file && std::fclose(m_file.release()) != 0)
	{
		if (result)
			Error::SetErrno(error, "Closing VHD", errno);
		result = false;
	}
	return result;
}

u64 VhdHddImage::GetSize() const
{
	return m_vhd ? mvhd_get_current_size(m_vhd) : 0;
}

bool VhdHddImage::CheckRequest(u64 sector, u32 count, Error* error) const
{
	const u64 sectors = GetSize() / 512;
	if (!m_vhd || sector > UINT32_MAX || count > INT_MAX || sector > sectors || count > sectors - sector)
	{
		Error::SetStringView(error, "VHD sector request is outside the logical disk capacity.");
		return false;
	}
	return true;
}

bool VhdHddImage::CheckResult(int result, Error* error) const
{
	if (result == 0)
		return true;

	Error::SetString(error, m_vhd ? mvhd_strerr(mvhd_get_error(m_vhd)) : "VHD image is closed.");
	return false;
}

bool VhdHddImage::ReadSectors(u64 sector, u32 count, void* dst, Error* error)
{
	if (!CheckRequest(sector, count, error))
		return false;

	return CheckResult(mvhd_read_sectors(m_vhd, static_cast<u32>(sector), static_cast<int>(count), dst), error);
}

bool VhdHddImage::WriteSectors(u64 sector, u32 count, const void* src, Error* error)
{
	if (!CheckRequest(sector, count, error))
		return false;

	return CheckResult(mvhd_write_sectors(m_vhd, static_cast<u32>(sector), static_cast<int>(count), src), error);
}

bool VhdHddImage::Flush(Error* error)
{
	return CheckResult(m_vhd ? mvhd_flush(m_vhd) : -1, error);
}

bool VhdHddImage::IsVhdFileName(const std::string& path)
{
	const std::string_view extension = Path::GetExtension(path);
	return StringUtil::compareNoCase(extension, "vhd") || StringUtil::compareNoCase(extension, "vhdx");
}

std::optional<u64> VhdHddImage::GetLogicalSize(const std::string& path)
{
	VhdHddImage image;

	if (!image.OpenShared(path, nullptr, true, FileSystem::FileShareMode::DenyNone))
		return std::nullopt;
	return image.GetSize();
}
