// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <functional>
#include <string>

class Error;

namespace HddImageOperations
{
	// Return false to cancel. Progress spans copying and full logical verification.
	using Progress = std::function<bool(u64 completed, u64 total)>;

	// Callers must keep emulation shut down until the operation finishes.
	bool Convert(const std::string& source, const std::string& destination, bool to_vhd,
		const std::string& identity_path, const Progress& progress, Error* error);
	bool Compact(const std::string& source, const Progress& progress, Error* error);
} // namespace HddImageOperations
