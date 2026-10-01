// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "VUCommunication.h"

#include <gtest/gtest.h>

TEST(VUCommunication, EarlierActiveUnitAndTie)
{
	EXPECT_EQ(VUCommunication::SelectUnit(true, true, 100, 101), 0u);
	EXPECT_EQ(VUCommunication::SelectUnit(true, true, 100, 100), 0u);
	EXPECT_EQ(VUCommunication::SelectUnit(true, true, 101, 100), 1u);
	EXPECT_EQ(VUCommunication::SelectUnit(false, true, 0, 100), 1u);
	EXPECT_EQ(VUCommunication::SelectUnit(true, false, 100, 0), 0u);
}

TEST(VUCommunication, PendingVersionIsNotPublished)
{
	const int ready[] = {4, -1, -1, 0};
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 0), 3);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 1), 3);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 2), 3);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 3), 3);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 4), 0);
}

TEST(VUCommunication, SeveralOutstandingVersions)
{
	const int ready[] = {3, 4, 1, 2};
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 0), -1);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 1), 2);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 2), 3);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 3), 0);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 4), 1);
}

TEST(VUCommunication, AbsentInstancesRemainAbsent)
{
	const int ready[] = {-1, -1, -1, -1};
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 0), -1);
	EXPECT_EQ(VUCommunication::FindMatureFlagSlot(ready, 100), -1);
}

TEST(VUCommunication, StatusLookupMatchesPackedConversion)
{
	for (u32 raw = 0; raw <= 0xffff; raw++)
	{
		u32 original = (((raw & 0x7777) + 0x7777) | raw) & 0x8888;
		original = ((original * 0x249) >> 12) & 15;
		const u8 rotated = static_cast<u8>((original >> 2) | (original << 6));
		ASSERT_EQ(VUCommunication::NormalizeStatusLow(static_cast<u16>(raw)), rotated) << raw;
	}
}

TEST(VUCommunication, BatchStopsAtRequestAndPartner)
{
	EXPECT_EQ(VUCommunication::BatchCycles(0, 1, 100, 120, true, 105), 20u);
	EXPECT_EQ(VUCommunication::BatchCycles(1, 1, 100, 120, true, 105), 5u);
	EXPECT_EQ(VUCommunication::BatchCycles(1, 1, 100, 120, false, 0), 20u);
	EXPECT_EQ(VUCommunication::BatchCycles(0, 1, 100, 200, false, 0), 64u);
	// Stalls and control/delay groups are indivisible, not truncated pairs.
	EXPECT_EQ(VUCommunication::BatchCycles(1, 7, 100, 120, true, 102), 7u);
	EXPECT_EQ(VUCommunication::BatchCycles(0, 7, 100, 101, false, 0), 7u);
}
