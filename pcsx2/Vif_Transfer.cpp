// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "Vif_Dma.h"
#include "Vif_Dynarec.h"

//------------------------------------------------------------------
// VifCode Transfer Interpreter (Vif0/Vif1)
//------------------------------------------------------------------

// Interprets packet
_vifT void vifTransferLoop(u32*& data, bool dma_input = false)
{
	vifStruct& vifX = GetVifX;

	u32& pSize = vifX.vifpacketsize;

	int ret = 0;

	vifXRegs.stat.VPS |= VPS_TRANSFERRING;
	vifXRegs.stat.ER1  = false;
	if constexpr (idx == 1)
	{
		if (vif1Regs.stat.ER0)
		{
			// DMA may finish while the decoder is stopped. Keep its input owned
			// until STC; changing the error mask alone does not resume execution.
			const u32 accepted = vif1DeferErrorInput(data, pSize, dma_input);
			data += accepted;
			pSize -= accepted;
			vif1Regs.stat.FQC = vif1InputFifoCount();
			return;
		}
	}
	//VIF_LOG("Starting VIF%d loop, pSize = %x, stalled = %x", idx, pSize, vifX.vifstalled.enabled );
	while (pSize > 0 && !vifX.vifstalled.enabled) {

		if(!vifX.cmd) { // Get new VifCode

			if(!vifXRegs.err.MII)
			{
				if(vifX.irq && !CHECK_VIF1STALLHACK)
					break;

				vifX.irq      |= data[0] >> 31;
			}

			vifXRegs.code = data[0];
			vifX.cmd	  = data[0] >> 24;


			VIF_LOG("New VifCMD %x tagsize %x irq %d", vifX.cmd, vifX.tag.size, vifX.irq);
			if (IsDevBuild && TraceLogging.EE.VIFcode.IsActive()) {
				// Pass 2 means "log it"
				vifCmdHandler[idx][vifX.cmd & 0x7f](2, data);
			}
		}

		ret = vifCmdHandler[idx][vifX.cmd & 0x7f](vifX.pass, data);
		data   += ret;
		pSize  -= ret;
		if constexpr (idx == 1)
		{
			if (vif1CpuFifoActive() && (ret == 0 || vif1.waitforvu || vif1Regs.stat.VGW))
				break;
		}
		if (vifX.vifstalled.enabled)
		{
			int current_STR = idx ? vif1ch.chcr.STR : vif0ch.chcr.STR;
			if (!current_STR)
				DevCon.Warning("Warning! VIF%d stalled during FIFO transfer!", idx);
		}
	}
}

_vifT static __fi bool vifTransfer(u32* data, int size, bool TTE, bool dma_input = false)
{
	vifStruct& vifX = GetVifX;

	// irqoffset necessary to add up the right qws, or else will spin (spiderman)
	int transferred = vifX.irqoffset.enabled ? vifX.irqoffset.value : 0;

	vifX.vifpacketsize = size;
	vifTransferLoop<idx>(data, dma_input);

	transferred += size - vifX.vifpacketsize;
	if constexpr (idx == 1)
	{
		// FLUSHE stops the decoder, not normal DMA input. Keep the command
		// and following words in order until VU1 finishes; do not decode ahead.
		if (dma_input && vif1.dmamode == VIF_NORMAL_FROM_MEM_MODE &&
			dmacRegs.ctrl.MFD != MFD_VIF1 && vif1.waitforvu && (vif1.cmd & 0x7f) == 0x10)
		{
			const u32 accepted = vif1DeferDmaInput(data, vifX.vifpacketsize, transferred & 3);
			vifX.vifpacketsize -= accepted;
			transferred += accepted;
		}
	}

	//Make this a minimum of 1 cycle so if it's the end of the packet it doesnt just fall through.
	//Metal Saga can do this, just to be safe :)
	if (!idx) g_vif0Cycles += std::max(1, (int)((transferred * BIAS) >> 2));
	else	  g_vif1Cycles += std::max(1, (int)((transferred * BIAS) >> 2));

	vifX.irqoffset.value = transferred % 4; // cannot lose the offset

	if (vifX.irq && vifX.cmd == 0) {
		VIF_LOG("Vif%d IRQ Triggering", idx);
		//Always needs to be set to return to the correct offset if there is data left.
		vifX.vifstalled.enabled = VifStallEnable(vifXch);
		vifX.vifstalled.value = VIF_IRQ_STALL;
	}

	if (!TTE) // *WARNING* - Tags CAN have interrupts! so lets just ignore the dma modifying stuffs (GT4)
	{
		transferred  = transferred >> 2;
		transferred = std::min((int)vifXch.qwc, transferred);
		vifXch.madr +=(transferred << 4);
		vifXch.qwc  -= transferred;

		hwDmacSrcTadrInc(vifXch);

		vifX.irqoffset.enabled = false;

		if(!vifXch.qwc)
			vifX.inprogress &= ~0x1;
		else if (vifX.irqoffset.value != 0)
			vifX.irqoffset.enabled = true;
	}
	else
	{
		if(vifX.irqoffset.value != 0){
			vifX.irqoffset.enabled = true;
		}else
			vifX.irqoffset.enabled = false;
	}

	vifExecQueue(idx);

	return !vifX.vifstalled.enabled;
}

// When TTE is set to 1, MADR and QWC are not updated as part of the transfer.
bool VIF0transfer(u32 *data, int size, bool TTE) {
	return vifTransfer<0>(data, size, TTE);
}
bool VIF1transfer(u32 *data, int size, bool TTE) {
	return vifTransfer<1>(data, size, TTE);
}

bool VIF1dmaTransfer(u32* data, int size)
{
	return vifTransfer<1>(data, size, false, true);
}
