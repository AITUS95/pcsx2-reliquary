// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "HddImageOperations.h"
#include "VhdHddImage.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ScopedGuard.h"
#include "common/StringUtil.h"
#include "minivhd.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <random>
#include <vector>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
	constexpr u32 BUFFER_SIZE = 512 * 1024;

	bool Sync(FILE* file, Error* error)
	{
		if (std::fflush(file) != 0)
		{
			Error::SetErrno(error, "Flushing HDD image", errno);
			return false;
		}
#ifdef _WIN32
		const int result = _commit(_fileno(file));
#else
		const int result = fsync(fileno(file));
#endif
		if (result == 0)
			return true;
		Error::SetErrno(error, "Synchronizing HDD image", errno);
		return false;
	}

	bool Update(const HddImageOperations::Progress& progress, u64 completed, u64 total, Error* error)
	{
		if (!progress || progress(completed, total))
			return true;
		Error::SetStringView(error, "HDD operation canceled.");
		return false;
	}

	bool RawRead(FILE* file, u64 offset, void* data, u32 bytes, Error* error)
	{
		if (FileSystem::FSeek64(file, offset, SEEK_SET) == 0 && std::fread(data, 1, bytes, file) == bytes)
			return true;
		Error::SetStringView(error, "Failed to read every requested byte from the raw HDD image.");
		return false;
	}

	bool RawWrite(FILE* file, u64 offset, const void* data, u32 bytes, Error* error)
	{
		if (FileSystem::FSeek64(file, offset, SEEK_SET) == 0 && std::fwrite(data, 1, bytes, file) == bytes)
			return true;
		Error::SetStringView(error, "Failed to write every requested byte to the raw HDD image.");
		return false;
	}

	bool CloseFile(FileSystem::ManagedCFilePtr& file, Error* error)
	{
		if (!file || std::fclose(file.release()) == 0)
			return true;
		Error::SetErrno(error, "Closing HDD image", errno);
		return false;
	}

	class Image
	{
	public:
		bool Open(const std::string& path, bool vhd, Error* error)
		{
			if (vhd)
			{
				if (!m_vhd.Open(path, error, true))
					return false;
				m_size = m_vhd.GetSize();
				return true;
			}

			m_raw = FileSystem::OpenManagedSharedCFile(path.c_str(), "rb", FileSystem::FileShareMode::DenyWrite, error);
			if (!m_raw)
				return false;
			const s64 size = FileSystem::FSize64(m_raw.get());
			if (size < 0)
			{
				Error::SetErrno(error, "Reading HDD image size", errno);
				return false;
			}
			m_size = static_cast<u64>(size);
			return true;
		}

		bool Create(const std::string& path, u64 size, bool vhd, Error* error)
		{
			m_size = size;
			if (vhd)
				return m_vhd.Create(path, size, error);

			m_raw = FileSystem::OpenManagedCFile(path.c_str(), "w+bx", error);
			return static_cast<bool>(m_raw);
		}

		u64 GetSize() const { return m_size; }

		bool Read(u64 offset, void* data, u32 bytes, Error* error)
		{
			return m_raw ? RawRead(m_raw.get(), offset, data, bytes, error) :
			               m_vhd.ReadSectors(offset / 512, bytes / 512, data, error);
		}

		bool Write(u64 offset, const void* data, u32 bytes, Error* error)
		{
			return m_raw ? RawWrite(m_raw.get(), offset, data, bytes, error) :
			               m_vhd.WriteSectors(offset / 512, bytes / 512, data, error);
		}

		bool Close(Error* error = nullptr)
		{
			return m_raw ? CloseFile(m_raw, error) : m_vhd.Close(error);
		}

		bool Reopen(const std::string& path, Error* error)
		{
			const bool vhd = !m_raw;
			if (m_raw && !Sync(m_raw.get(), error))
				return false;
			return Close(error) && Open(path, vhd, error);
		}

	private:
		VhdHddImage m_vhd;
		FileSystem::ManagedCFilePtr m_raw;
		u64 m_size = 0;
	};

	std::string Sibling(const std::string& source, const char* suffix)
	{
		std::random_device random;
		return fmt::format("{}.{}-{:08x}{:08x}", source, suffix, random(), random());
	}

	template <typename T>
	bool ReadChunks(Image& source, u64 progress_offset, const HddImageOperations::Progress& progress, T process, Error* error)
	{
		const u64 size = source.GetSize();
		std::vector<u8> buffer(BUFFER_SIZE);
		for (u64 offset = 0; offset < size;)
		{
			if (!Update(progress, progress_offset + offset, size * 2, error))
				return false;
			const u32 bytes = static_cast<u32>(std::min<u64>(BUFFER_SIZE, size - offset));
			if (!source.Read(offset, buffer.data(), bytes, error) ||
				!process(offset, buffer.data(), bytes))
				return false;
			offset += bytes;
		}
		return true;
	}

	bool Copy(Image& source, Image& destination, const HddImageOperations::Progress& progress, Error* error)
	{
		return ReadChunks(source, 0, progress, [&](u64 offset, const u8* data, u32 bytes) { return destination.Write(offset, data, bytes, error); }, error);
	}

	bool Verify(Image& source, Image& destination, const HddImageOperations::Progress& progress, Error* error)
	{
		const u64 size = source.GetSize();
		std::vector<u8> destination_data(BUFFER_SIZE);
		if (!ReadChunks(source, size, progress, [&](u64 offset, const u8* data, u32 bytes) {
				if (!destination.Read(offset, destination_data.data(), bytes, error))
					return false;
				if (std::memcmp(data, destination_data.data(), bytes) == 0)
					return true;

				Error::SetStringFmt(error, "HDD verification failed at logical byte {}.", offset);
				return false; }, error))
			return false;

		return Update(progress, size * 2, size * 2, error);
	}

	bool Rebuild(Image& source, Image& destination, const std::string& destination_path,
		const HddImageOperations::Progress& progress, Error* error)
	{
		const u64 size = source.GetSize();
		if (!Copy(source, destination, progress, error))
			return false;
		// Reopen the persisted image before comparing logical sectors
		if (!destination.Reopen(destination_path, error))
			return false;
		if (destination.GetSize() != size)
		{
			Error::SetStringView(error, "Converted HDD capacity changed before verification.");
			return false;
		}
		return Verify(source, destination, progress, error);
	}

	bool Replace(const std::string& original, const std::string& temporary, Error* error)
	{
#ifdef _WIN32
		const std::string backup = Sibling(original, "backup");

		if (!ReplaceFileW(FileSystem::GetWin32Path(original).c_str(), FileSystem::GetWin32Path(temporary).c_str(),
				FileSystem::GetWin32Path(backup).c_str(), 0, nullptr, nullptr))
		{
			const DWORD replacement_error = GetLastError();
			Error::SetWin32(error, "Replacing compacted VHD", replacement_error);

			if (!FileSystem::FileExists(original.c_str()) && FileSystem::FileExists(backup.c_str()))
			{
				if (!MoveFileExW(FileSystem::GetWin32Path(backup).c_str(), FileSystem::GetWin32Path(original).c_str(), MOVEFILE_WRITE_THROUGH))
					Error::AddSuffix(error, " Automatic restoration failed; the original remains at the backup path.");
			}
			Error::AddSuffix(error, fmt::format(" Verified output: {}. Backup, if created: {}.", temporary, backup));
			return false;
		}
		FileSystem::DeleteFilePath(backup.c_str());
		return true;
#else
		return FileSystem::RenamePath(temporary.c_str(), original.c_str(), error);
#endif
	}

	bool CopyIdentity(const std::string& source, const std::string& destination, Error* error)
	{
		if (source == destination)
			return true;
		if (FileSystem::FileExists(destination.c_str()))
		{
			Error::SetStringView(error, "The destination already has an HDD identity file. Choose a different destination.");
			return false;
		}
		if (!FileSystem::FileExists(source.c_str()))
			return true;

		const s64 size = FileSystem::GetPathFileSize(source.c_str());
		if (size < 0 || size > 512)
		{
			Error::SetStringView(error, "The source HDD identity file is unreadable or larger than 512 bytes.");
			return false;
		}
		const auto identity = FileSystem::ReadBinaryFile(source.c_str());
		if (!identity)
		{
			Error::SetStringView(error, "Failed to read the source HDD identity file.");
			return false;
		}
		auto output = FileSystem::OpenManagedCFile(destination.c_str(), "w+bx", error);
		if (!output)
			return false;
		ScopedGuard cleanup([&]() {
			output.reset();
			FileSystem::DeleteFilePath(destination.c_str());
		});
		if (!RawWrite(output.get(), 0, identity->data(), static_cast<u32>(identity->size()), error) ||
			!Sync(output.get(), error) || !CloseFile(output, error))
			return false;
		cleanup.Cancel();
		return true;
	}
} // namespace

bool HddImageOperations::Convert(const std::string& source, const std::string& destination, bool to_vhd,
	const std::string& identity_path, const Progress& progress, Error* error)
{
	if (source == destination || FileSystem::FileExists(destination.c_str()))
	{
		Error::SetStringView(error, "Choose a new destination; conversion preserves the source image.");
		return false;
	}
	if (to_vhd && (VhdHddImage::IsVhdFileName(source) || StringUtil::compareNoCase(Path::GetExtension(source), "chd")))
	{
		Error::SetStringView(error, "Raw to VHD conversion requires a raw source image.");
		return false;
	}

	Image input, output;
	if (!input.Open(source, !to_vhd, error))
		return false;
	const u64 size = input.GetSize();
	if (size == 0 || size % 512 != 0 || size > MVHD_MAX_SIZE_IN_BYTES)
	{
		Error::SetStringView(error, "HDD capacity must be a nonzero multiple of 512 bytes within the VHD capacity limit.");
		return false;
	}
	if (!output.Create(destination, size, to_vhd, error))
		return false;
	ScopedGuard cleanup([&]() {
		output.Close();
		FileSystem::DeleteFilePath(destination.c_str());
	});
	if (!Rebuild(input, output, destination, progress, error) || !output.Close(error))
		return false;

	const std::string source_id = identity_path.empty() ? Path::ReplaceExtension(source, "hddid") : identity_path;
	const std::string destination_id = Path::ReplaceExtension(destination, "hddid");
	if (!CopyIdentity(source_id, destination_id, error))
		return false;
	cleanup.Cancel();
	return true;
}

bool HddImageOperations::Compact(const std::string& source, const Progress& progress, Error* error)
{
	Image input, output;
	if (!input.Open(source, true, error))
		return false;
	const std::string temporary = Sibling(source, "compact") + ".vhd";
	if (!output.Create(temporary, input.GetSize(), true, error))
		return false;
	ScopedGuard cleanup([&]() {
		output.Close();
		FileSystem::DeleteFilePath(temporary.c_str());
	});
	if (!Rebuild(input, output, temporary, progress, error) || !output.Close(error) || !input.Close(error))
		return false;
	if (!Update(progress, 1, 1, error))
		return false;
	cleanup.Cancel();
	if (Replace(source, temporary, error))
		return true;
	Error::AddSuffix(error, fmt::format(" Verified compacted image retained at: {}.", temporary));
	return false;
}
