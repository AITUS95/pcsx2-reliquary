// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/FileSystem.h"
#include "common/Pcsx2Defs.h"

#include <optional>
#include <string>

class Error;
struct MVHDMeta;

class VhdHddImage
{
public:
	VhdHddImage() = default;
	~VhdHddImage();
	VhdHddImage(const VhdHddImage&) = delete;
	VhdHddImage& operator=(const VhdHddImage&) = delete;

	bool Open(const std::string& path, Error* error = nullptr, bool readonly = false);
	bool Create(const std::string& path, u64 size, Error* error = nullptr);
	bool Close(Error* error = nullptr);
	u64 GetSize() const;
	bool ReadSectors(u64 sector, u32 count, void* dst, Error* error = nullptr);
	bool WriteSectors(u64 sector, u32 count, const void* src, Error* error = nullptr);
	bool Flush(Error* error = nullptr);

	static bool IsVhdFileName(const std::string& path);
	static std::optional<u64> GetLogicalSize(const std::string& path);

private:
	bool OpenShared(const std::string& path, Error* error, bool readonly, FileSystem::FileShareMode share_mode);
	bool CheckRequest(u64 sector, u32 count, Error* error) const;
	bool CheckResult(int result, Error* error) const;
	FileSystem::ManagedCFilePtr m_file;
	MVHDMeta* m_vhd = nullptr;
};
