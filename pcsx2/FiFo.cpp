// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"

#include "Gif.h"
#include "Gif_Unit.h"
#include "MTGS.h"
#include "Vif.h"
#include "Vif_Dma.h"
#include <vector>

bool vif1CpuFifoEnabled()
{
	// Finish accepted writes even if the compatibility option is changed.
	return EmuConfig.Gamefixes.VUCommunicationHack || vif1Regs.stat.ER0 || vif1CpuFifoPending();
}

static std::vector<u32> s_vif1_cpu_words;
// Input already staged beyond the counted FIFO remains owned until STC.
static std::vector<u32> s_vif1_error_stage;
static bool s_vif1_cpu_draining = false;
// One store may wait on the bus outside the 16-QW device FIFO. Both EE
// backends must yield after that store, before executing another instruction.
static mem128_t s_vif1_cpu_store = {};
static bool s_vif1_cpu_store_pending = false;

static u32 vif1CpuInputCapacity()
{
	// CPU input can still be accepted after a FLUSHE-stalled DMA has
	// exhausted its admission window. Keep that headroom separate from
	// the DMA limit; it does not establish the full overflow behavior.
	return vif1.waitforvu && (vif1.cmd & 0x7f) == 0x10 &&
	               !vif1Regs.stat.test(VIF1_STAT_FDR | VIF1_STAT_ER0 | VIF1_STAT_VSS | VIF1_STAT_VFS | VIF1_STAT_VIS) ?
	           76 :
	           64;
}

bool vif1CpuFifoBusBlocked()
{
	return s_vif1_cpu_store_pending;
}

const bool* vif1CpuFifoBusBlockedAddress()
{
	return &s_vif1_cpu_store_pending;
}

bool vif1CpuFifoActive()
{
	return s_vif1_cpu_draining;
}

bool vif1CpuFifoPending()
{
	return !s_vif1_error_stage.empty() || !s_vif1_cpu_words.empty();
}

static u32 vif1OwnedInputWords()
{
	return static_cast<u32>(s_vif1_error_stage.size() + s_vif1_cpu_words.size());
}

static void vif1StageErrorHead()
{
	// A loaded state retains the original flat, ordered input encoding.
	// Reconstruct the split lazily: HW registers load after VIF internals.
	const u32 count = std::min<u32>(8 - static_cast<u32>(s_vif1_error_stage.size()),
		static_cast<u32>(s_vif1_cpu_words.size()));
	s_vif1_error_stage.insert(s_vif1_error_stage.end(), s_vif1_cpu_words.begin(), s_vif1_cpu_words.begin() + count);
	s_vif1_cpu_words.erase(s_vif1_cpu_words.begin(), s_vif1_cpu_words.begin() + count);
}

u32 vif1InputFifoCount()
{
	if (vif1Regs.stat.ER0)
	{
		vif1StageErrorHead();
		return std::min<u32>(16, static_cast<u32>((s_vif1_cpu_words.size() + 3) / 4));
	}
	return std::min<u32>(16, (vif1OwnedInputWords() + 3) / 4);
}

static bool vif1ErrorDmaAdmission()
{
	return vif1Regs.stat.ER0 && !vif1Regs.stat.FDR && vif1.cmd == 0x6c && vif1.pass &&
	       vif1Regs.mode == 0 && vif1Regs.cycle.cl == 1 && vif1Regs.cycle.wl == 1 &&
	       vif1.dmamode == VIF_NORMAL_FROM_MEM_MODE && !vif1.irqoffset.enabled &&
	       dmacRegs.ctrl.MFD == NO_MFD && dmacRegs.ctrl.STD == NO_STD;
}

static void vif1AppendErrorInput(const u32* data, u32 words)
{
	const u32 staged = std::min<u32>(words, 8 - static_cast<u32>(s_vif1_error_stage.size()));
	s_vif1_error_stage.insert(s_vif1_error_stage.end(), data, data + staged);
	s_vif1_cpu_words.insert(s_vif1_cpu_words.end(), data + staged, data + words);
}

u32 vif1DeferErrorInput(const u32* data, u32 words, bool dma_input)
{
	vif1StageErrorHead();
	if (dma_input && vif1ErrorDmaAdmission() && !(words & 3))
	{
		u32 accepted = 0;
		// Check the request threshold between bursts, not after every QW.
		// Staged input is not FIFO space and must not consume its grant.
		while (accepted < words && vif1InputFifoCount() <= 8)
		{
			const u32 burst = std::min<u32>(32, words - accepted);
			pxAssert(s_vif1_cpu_words.size() + burst <= 64);
			vif1AppendErrorInput(data + accepted, burst);
			accepted += burst;
		}
		return accepted;
	}
	// Preserve the existing CPU/chain admission limit outside normal DMA.
	const u32 accepted = std::min(words, 64u - std::min<u32>(64, vif1OwnedInputWords()));
	vif1AppendErrorInput(data, accepted);
	return accepted;
}

u32 vif1DeferDmaInput(const u32* data, u32 words, u32 partial_qw)
{
	u32 accepted = std::min(words, 64u - std::min<u32>(64, static_cast<u32>(s_vif1_cpu_words.size())));
	// Retire complete DMA QW, including words decoded before FLUSHE stopped.
	// The remaining head fragment still occupies its input QW.
	const u32 remainder = (partial_qw + accepted) & 3;
	accepted = accepted >= remainder ? accepted - remainder : 0;
	s_vif1_cpu_words.insert(s_vif1_cpu_words.end(), data, data + accepted);
	return accepted;
}

bool vif1ErrorInputFull()
{
	if (vif1ErrorDmaAdmission())
		return vif1InputFifoCount() > 8;
	return vif1Regs.stat.ER0 && vif1OwnedInputWords() >= 64;
}

bool vif1FlushInputFull()
{
	return s_vif1_cpu_words.size() > 60;
}

void vif1CpuFifoReset()
{
	s_vif1_cpu_words.clear();
	s_vif1_error_stage.clear();
	s_vif1_cpu_store_pending = false;
	s_vif1_cpu_store = {};
}

bool vif1CpuFifoFreeze(SaveStateBase& state)
{
	if (state.GetVersion() < 1)
	{
		if (state.IsLoading())
			vif1CpuFifoReset();
		return state.IsOkay();
	}

	u32 count = vif1OwnedInputWords();
	state.Freeze(count);
	// The serialized input includes the bounded CPU headroom during FLUSHE.
	if (!state.IsOkay() || count > 76)
		return false;
	if (state.IsLoading())
	{
		s_vif1_error_stage.clear();
		s_vif1_cpu_words.resize(count);
		state.FreezeMem(s_vif1_cpu_words.data(), count * sizeof(u32));
	}
	else
	{
		// Keep the existing ordered-word representation, without new fields.
		state.FreezeMem(s_vif1_error_stage.data(), s_vif1_error_stage.size() * sizeof(u32));
		state.FreezeMem(s_vif1_cpu_words.data(), s_vif1_cpu_words.size() * sizeof(u32));
	}
	state.Freeze(s_vif1_cpu_store_pending);
	state.Freeze(s_vif1_cpu_store);
	// A held bus write can exist only while the device FIFO has no room
	// for another QW. Otherwise a loaded state could block EE forever with
	// no queued input available to release the write.
	if (s_vif1_cpu_store_pending && (!vif1CpuFifoEnabled() || count <= 60))
		return false;
	return state.IsOkay();
}

static void vif1UpdateCpuFifoStatus()
{
	if (vif1.cmd)
	{
		// A CPU fragment can exhaust the available input without completing DMA.
		const bool cpu_input_empty = s_vif1_cpu_draining ? s_vif1_cpu_words.empty() : !vif1ch.chcr.STR;
		const bool cpu_waiting_for_input = cpu_input_empty && vif1.tag.size &&
		                                   !vif1.waitforvu && !vif1Regs.stat.VGW;
		if ((vif1.done || cpu_waiting_for_input) && !vif1ch.qwc && !vif1.waitforvu && !vif1Regs.stat.VGW)
			vif1Regs.stat.VPS = VPS_WAITING;
	}
	else
		vif1Regs.stat.VPS = VPS_IDLE;

	if (gifRegs.stat.APATH == 2 && gifUnit.gifPath[1].isDone())
	{
		gifRegs.stat.APATH = 0;
		gifRegs.stat.OPH = 0;
		vif1Regs.stat.VGW = false;
		if (gifUnit.checkPaths(1, 0, 1))
			gifUnit.Execute(false, true);
	}
}

void vif1CpuFifoDrain()
{
	if (!vif1CpuFifoEnabled() || !vif1CpuFifoPending())
		return;
	if (s_vif1_cpu_draining || vif1Regs.stat.FDR ||
		vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS | VIF1_STAT_ER0))
		return;
	// STC exposes the staged head to the decoder before the counted FIFO.
	s_vif1_cpu_words.insert(s_vif1_cpu_words.begin(), s_vif1_error_stage.begin(), s_vif1_error_stage.end());
	s_vif1_error_stage.clear();
	s_vif1_cpu_draining = true;
	// Pending CPU words precede a newly started DMA. Keep its word offset
	// separate from the already compacted CPU FIFO input.
	const tVIF_CTRL dma_offset = vif1.irqoffset;
	vif1.irqoffset = {};
	// VSS/VFS/VIS above block until STC. With those flags clear, both a
	// completed timing wait and a cancelled interrupt stall may resume.
	vif1.vifstalled.enabled = false;
	gifUnit.Execute(false, true);
	const size_t before = s_vif1_cpu_words.size();
	VIF1transfer(s_vif1_cpu_words.data(), static_cast<int>(before), true);
	const size_t consumed = before - vif1.vifpacketsize;
	s_vif1_cpu_words.erase(s_vif1_cpu_words.begin(), s_vif1_cpu_words.begin() + consumed);
	vif1.irqoffset = dma_offset;
	if (s_vif1_cpu_store_pending && s_vif1_cpu_words.size() <= vif1CpuInputCapacity() - 4)
	{
		s_vif1_cpu_words.insert(s_vif1_cpu_words.end(), s_vif1_cpu_store._u32, s_vif1_cpu_store._u32 + 4);
		s_vif1_cpu_store_pending = false;
	}
	if (vif1.irq && !vif1.cmd)
	{
		if (!vif1Regs.stat.ER1)
			vif1Regs.stat.INT = true;
		if (((vif1Regs.code >> 24) & 0x7f) != 0x07)
			vif1Regs.stat.VIS = true;
		hwIntcIrq(VIF1intc);
		--vif1.irq;
	}
	vif1Regs.stat.FQC = vif1InputFifoCount();
	vif1UpdateCpuFifoStatus();
	// DMA retries must not postpone an existing finish event: DMA is scanned
	// first, so replacing its deadline can otherwise starve FLUSHE indefinitely.
	if (!s_vif1_cpu_words.empty() && !(cpuRegs.interrupt & (1 << VIF_VU1_FINISH)))
	{
		CPU_SET_DMASTALL(VIF_VU1_FINISH, true);
		CPU_INT(VIF_VU1_FINISH, 128);
	}
	s_vif1_cpu_draining = false;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// Quick & dirty FIFO :D ////////////////////////
//////////////////////////////////////////////////////////////////////////

// Notes on FIFO implementation
//
// The FIFO consists of four separate pages of HW register memory, each mapped to a
// PS2 device.  They are listed as follows:
//
// 0x4000 - 0x5000 : VIF0  (all registers map to 0x4000)
// 0x5000 - 0x6000 : VIF1  (all registers map to 0x5000)
// 0x6000 - 0x7000 : GS    (all registers map to 0x6000)
// 0x7000 - 0x8000 : IPU   (registers map to 0x7000 and 0x7010, respectively)

void ReadFIFO_VIF1(mem128_t* out)
{
	if (vif1Regs.stat.test(VIF1_STAT_INT | VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		DevCon.Warning("Reading from vif1 fifo when stalled");

	ZeroQWC(out); // Clear first in case no data gets written...
	pxAssertRel(vif1Regs.stat.FQC != 0, "FQC = 0 on VIF FIFO READ!");
	if (vif1Regs.stat.FDR)
	{
		if (vif1Regs.stat.FQC > vif1.GSLastDownloadSize)
		{
			DevCon.Warning("Warning! GS Download size < FIFO count!");
		}
		if (vif1Regs.stat.FQC > 0)
		{
			MTGS::InitAndReadFIFO(reinterpret_cast<u8*>(out), 1);
			vif1.GSLastDownloadSize--;
			GUNIT_LOG("ReadFIFO_VIF1");
			if (vif1.GSLastDownloadSize <= 16)
				gifRegs.stat.OPH = false;
			vif1Regs.stat.FQC = std::min((u32)16, vif1.GSLastDownloadSize);
		}
	}

	VIF_LOG("ReadFIFO/VIF1 -> 0x%08X.%08X.%08X.%08X", out->_u32[0], out->_u32[1], out->_u32[2], out->_u32[3]);
}

//////////////////////////////////////////////////////////////////////////
// WriteFIFO Pages
//
void WriteFIFO_VIF0(const mem128_t* value)
{
	VIF_LOG("WriteFIFO/VIF0 <- 0x%08X.%08X.%08X.%08X", value->_u32[0], value->_u32[1], value->_u32[2], value->_u32[3]);

	vif0ch.qwc += 1;
	if (vif0.irqoffset.value != 0 && vif0.vifstalled.enabled)
		DevCon.Warning("Offset on VIF0 FIFO start!");
	[[maybe_unused]] bool ret = VIF0transfer((u32*)value, 4);

	if (vif0.cmd)
	{
		if (vif0.done && vif0ch.qwc == 0)
			vif0Regs.stat.VPS = VPS_WAITING;
	}
	else
	{
		vif0Regs.stat.VPS = VPS_IDLE;
	}

	pxAssertMsg(ret, "vif stall code not implemented");
}

void WriteFIFO_VIF1(const mem128_t* value)
{
	VIF_LOG("WriteFIFO/VIF1 <- 0x%08X.%08X.%08X.%08X", value->_u32[0], value->_u32[1], value->_u32[2], value->_u32[3]);
	if (vif1CpuFifoEnabled())
	{
		pxAssertRel(!s_vif1_cpu_store_pending, "EE executed another store while the VIF1 bus write was blocked");
		if (vif1OwnedInputWords() > vif1CpuInputCapacity() - 4)
		{
			s_vif1_cpu_store = *value;
			s_vif1_cpu_store_pending = true;
			CPU_SET_DMASTALL(VIF_VU1_FINISH, true);
			CPU_INT(VIF_VU1_FINISH, 128);
			cpuSetEvent();
			return;
		}
		if (vif1Regs.stat.ER0)
		{
			[[maybe_unused]] const u32 accepted = vif1DeferErrorInput(value->_u32, 4, false);
			pxAssert(accepted == 4);
			// Input admission continues while ER0 stops the UNPACK decoder.
			vif1Regs.stat.VPS = VPS_TRANSFERRING;
		}
		else
			s_vif1_cpu_words.insert(s_vif1_cpu_words.end(), value->_u32, value->_u32 + 4);
		vif1Regs.stat.FQC = vif1InputFifoCount();
		vif1CpuFifoDrain();
		return;
	}

	if (vif1Regs.stat.FDR)
	{
		DevCon.Warning("writing to fifo when fdr is set!");
	}
	if (vif1Regs.stat.test(VIF1_STAT_INT | VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
	{
		DevCon.Warning("writing to vif1 fifo when stalled");
	}
	if (vif1.irqoffset.value != 0 && vif1.vifstalled.enabled)
	{
		DevCon.Warning("Offset on VIF1 FIFO start!");
	}

	[[maybe_unused]] bool ret = VIF1transfer((u32*)value, 4);
	vif1UpdateCpuFifoStatus();

	pxAssertMsg(ret, "vif stall code not implemented");
}

void WriteFIFO_GIF(const mem128_t* value)
{
	GUNIT_LOG("WriteFIFO_GIF()");
	if ((!gifUnit.CanDoPath3() || gif_fifo.fifoSize > 0))
	{
		//DevCon.Warning("GIF FIFO HW Write");
		gif_fifo.write_fifo((u32*)value, 1);
		gif_fifo.read_fifo();
	}
	else
	{
		gifUnit.TransferGSPacketData(GIF_TRANS_FIFO, (u8*)value, 16);
	}

	if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT)
		gifUnit.gifPath[GIF_PATH_3].state = GIF_PATH_IDLE;

	if (gifRegs.stat.APATH == 3)
	{
		gifRegs.stat.APATH = 0;
		gifRegs.stat.OPH = 0;

		if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_IDLE || gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT)
		{
			if (gifUnit.checkPaths(1, 1, 0))
				gifUnit.Execute(false, true);
		}
	}
}
