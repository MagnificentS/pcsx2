// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "MipsDecoder.h"

namespace MipsDecoder
{

std::vector<MemoryAccess> DecodeMemoryOperations(u32 opcode, u64 pc, const u64 gpr[32])
{
	std::vector<MemoryAccess> result;

	// Extract instruction fields (MIPS instruction format)
	const u32 op = (opcode >> 26) & 0x3F;        // Primary opcode [31:26]
	const u32 rs = (opcode >> 21) & 0x1F;        // Source register [25:21]
	const u32 rt = (opcode >> 16) & 0x1F;        // Target register [20:16]
	const u32 func = opcode & 0x3F;              // Function code [5:0]
	const s16 imm = static_cast<s16>(opcode & 0xFFFF); // Immediate (sign-extended)

	// Base address from source register (rs)
	const u64 base_addr = gpr[rs];

	// Decode by primary opcode
	switch (op)
	{
		// === Load Instructions (I-type format) ===

		case 0x20: // LB - Load Byte (signed)
		case 0x24: // LBU - Load Byte Unsigned
			result.push_back({base_addr + imm, 1, false});
			break;

		case 0x21: // LH - Load Halfword (signed)
		case 0x25: // LHU - Load Halfword Unsigned
			result.push_back({base_addr + imm, 2, false});
			break;

		case 0x23: // LW - Load Word (signed)
		case 0x27: // LWU - Load Word Unsigned (R5900-specific)
			result.push_back({base_addr + imm, 4, false});
			break;

		case 0x37: // LD - Load Doubleword
			result.push_back({base_addr + imm, 8, false});
			break;

		case 0x1E: // LQ - Load Quadword (128-bit, R5900-specific)
			result.push_back({base_addr + imm, 16, false});
			break;

		// === Unaligned Load Instructions ===

		case 0x22: // LWL - Load Word Left
		case 0x26: // LWR - Load Word Right
			// Unaligned word access - report the aligned 4-byte region
			result.push_back({(base_addr + imm) & ~3ULL, 4, false});
			break;

		case 0x1A: // LDL - Load Doubleword Left
		case 0x1B: // LDR - Load Doubleword Right
			// Unaligned doubleword access - report the aligned 8-byte region
			result.push_back({(base_addr + imm) & ~7ULL, 8, false});
			break;

		// === Store Instructions (I-type format) ===

		case 0x28: // SB - Store Byte
			result.push_back({base_addr + imm, 1, true});
			break;

		case 0x29: // SH - Store Halfword
			result.push_back({base_addr + imm, 2, true});
			break;

		case 0x2B: // SW - Store Word
			result.push_back({base_addr + imm, 4, true});
			break;

		case 0x3F: // SD - Store Doubleword
			result.push_back({base_addr + imm, 8, true});
			break;

		case 0x1F: // SQ - Store Quadword (128-bit, R5900-specific)
			result.push_back({base_addr + imm, 16, true});
			break;

		// === Unaligned Store Instructions ===

		case 0x2A: // SWL - Store Word Left
		case 0x2E: // SWR - Store Word Right
			// Unaligned word access - report the aligned 4-byte region
			result.push_back({(base_addr + imm) & ~3ULL, 4, true});
			break;

		case 0x2C: // SDL - Store Doubleword Left
		case 0x2D: // SDR - Store Doubleword Right
			// Unaligned doubleword access - report the aligned 8-byte region
			result.push_back({(base_addr + imm) & ~7ULL, 8, true});
			break;

		// === Floating Point Unit (COP1) Load/Store ===

		case 0x31: // LWC1 - Load Word to Coprocessor 1 (FPU)
			result.push_back({base_addr + imm, 4, false});
			break;

		case 0x39: // SWC1 - Store Word from Coprocessor 1 (FPU)
			result.push_back({base_addr + imm, 4, true});
			break;

		case 0x35: // LDC1 - Load Doubleword to Coprocessor 1 (FPU)
			result.push_back({base_addr + imm, 8, false});
			break;

		case 0x3D: // SDC1 - Store Doubleword from Coprocessor 1 (FPU)
			result.push_back({base_addr + imm, 8, true});
			break;

		// === Vector Unit 0 (COP2/VU0) Load/Store ===

		case 0x36: // LQC2 - Load Quadword to Coprocessor 2 (VU0)
			result.push_back({base_addr + imm, 16, false});
			break;

		case 0x3E: // SQC2 - Store Quadword from Coprocessor 2 (VU0)
			result.push_back({base_addr + imm, 16, true});
			break;

		// === Cache/Sync Instructions (no memory data access) ===

		case 0x2F: // CACHE - Cache operation
			// CACHE instructions affect cache state but don't access memory data
			// in the traditional sense (they're control operations)
			break;

		case 0x00: // SPECIAL opcode group
			// Check function code for memory operations
			if (func == 0x0F) // SYNC - Synchronize shared memory
			{
				// SYNC is a barrier, not a memory access
			}
			// Other SPECIAL opcodes (ADD, SUB, etc.) don't access memory
			break;

		// === Prefetch (doesn't modify memory, just hint) ===

		case 0x33: // PREF - Prefetch
			// Prefetch hints don't actually access memory data for our purposes
			// (they're cache hints, not data transfers)
			break;

		// === All other opcodes ===

		default:
			// Non-memory instruction (arithmetic, branch, etc.)
			break;
	}

	return result;
}

std::vector<MemoryAccess> DecodeMemoryOperationsIOP(u32 opcode, u32 pc, const u32 gpr[32])
{
	std::vector<MemoryAccess> result;

	// IOP uses standard MIPS I/II instruction set (simpler than R5900)
	const u32 op = (opcode >> 26) & 0x3F;
	const u32 rs = (opcode >> 21) & 0x1F;
	const s16 imm = static_cast<s16>(opcode & 0xFFFF);

	// Base address from source register (32-bit for IOP)
	const u32 base_addr = gpr[rs];

	switch (op)
	{
		// === Load Instructions ===

		case 0x20: // LB - Load Byte (signed)
		case 0x24: // LBU - Load Byte Unsigned
			result.push_back({base_addr + imm, 1, false});
			break;

		case 0x21: // LH - Load Halfword (signed)
		case 0x25: // LHU - Load Halfword Unsigned
			result.push_back({base_addr + imm, 2, false});
			break;

		case 0x23: // LW - Load Word
			result.push_back({base_addr + imm, 4, false});
			break;

		// === Unaligned Load Instructions ===

		case 0x22: // LWL - Load Word Left
		case 0x26: // LWR - Load Word Right
			result.push_back({(base_addr + imm) & ~3U, 4, false});
			break;

		// === Store Instructions ===

		case 0x28: // SB - Store Byte
			result.push_back({base_addr + imm, 1, true});
			break;

		case 0x29: // SH - Store Halfword
			result.push_back({base_addr + imm, 2, true});
			break;

		case 0x2B: // SW - Store Word
			result.push_back({base_addr + imm, 4, true});
			break;

		// === Unaligned Store Instructions ===

		case 0x2A: // SWL - Store Word Left
		case 0x2E: // SWR - Store Word Right
			result.push_back({(base_addr + imm) & ~3U, 4, true});
			break;

		// === FPU Load/Store (COP1) ===

		case 0x31: // LWC1 - Load Word to FPU
			result.push_back({base_addr + imm, 4, false});
			break;

		case 0x39: // SWC1 - Store Word from FPU
			result.push_back({base_addr + imm, 4, true});
			break;

		// === Coprocessor 2 Load/Store (if used) ===

		case 0x32: // LWC2 - Load Word to Coprocessor 2
			result.push_back({base_addr + imm, 4, false});
			break;

		case 0x3A: // SWC2 - Store Word from Coprocessor 2
			result.push_back({base_addr + imm, 4, true});
			break;

		// === Cache/Sync Instructions ===

		case 0x2F: // CACHE - Cache operation
			// Cache operations don't access memory data
			break;

		// === All other opcodes ===

		default:
			// Non-memory instruction
			break;
	}

	return result;
}

} // namespace MipsDecoder
