// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"
#include <vector>

namespace MipsDecoder
{

/// Result of decoding a memory operation from an instruction
struct MemoryAccess
{
	u64 address;        // Effective memory address accessed
	u8 size;            // Size in bytes (1, 2, 4, 8, 16)
	bool is_write;      // true = write/store, false = read/load
};

/// Decode MIPS R5900 (EE) instruction to extract memory access information.
/// This function analyzes the opcode and uses the current CPU register state
/// to determine what memory addresses (if any) are accessed by the instruction.
///
/// @param opcode The 32-bit MIPS instruction to decode
/// @param pc Program counter (for PC-relative addressing if needed)
/// @param gpr Pointer to 32 general purpose registers (64-bit each for R5900)
/// @return Vector of memory accesses (empty if instruction doesn't access memory)
///
/// @note Supports all R5900 load/store instructions including:
///       - Standard loads: LB/LBU/LH/LHU/LW/LWU/LD
///       - Quadword loads: LQ (128-bit, EE-specific)
///       - Unaligned loads: LWL/LWR/LDL/LDR
///       - Standard stores: SB/SH/SW/SD
///       - Quadword stores: SQ (128-bit, EE-specific)
///       - Unaligned stores: SWL/SWR/SDL/SDR
///       - FPU loads/stores: LWC1/LDC1/SWC1/SDC1
///       - VU0 loads/stores: LQC2/SQC2
std::vector<MemoryAccess> DecodeMemoryOperations(
	u32 opcode,
	u64 pc,
	const u64 gpr[32]
);

/// Decode MIPS R3000A (IOP) instruction to extract memory access information.
/// Similar to DecodeMemoryOperations but for the IOP's simpler MIPS I/II instruction set.
///
/// @param opcode The 32-bit MIPS instruction to decode
/// @param pc Program counter (for PC-relative addressing if needed)
/// @param gpr Pointer to 32 general purpose registers (32-bit each for R3000A)
/// @return Vector of memory accesses (empty if instruction doesn't access memory)
///
/// @note IOP uses standard MIPS I/II instruction set (subset of R5900)
///       - No quadword operations (LQ/SQ)
///       - 32-bit addresses and registers
///       - Otherwise similar load/store instruction set
std::vector<MemoryAccess> DecodeMemoryOperationsIOP(
	u32 opcode,
	u32 pc,
	const u32 gpr[32]
);

} // namespace MipsDecoder
