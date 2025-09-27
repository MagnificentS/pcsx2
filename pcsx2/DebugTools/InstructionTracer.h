// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "DebugInterface.h"

#include "common/Pcsx2Types.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace InstructionTracer
{
        struct TraceEvent
        {
                BreakPointCpu cpu = BREAKPOINT_EE;
                u64 pc = 0;
                u32 opcode = 0;
                std::string disasm;
                u64 cycles = 0;
                u64 timestamp_ns = 0;

                using MemorySummary = std::vector<std::pair<u64, u8>>;

                MemorySummary mem_reads;
                MemorySummary mem_writes;
        };

        struct DumpBounds
        {
                std::optional<u64> limit;
        };

        void Enable(BreakPointCpu cpu, bool on);
        bool IsEnabled(BreakPointCpu cpu);
        void Record(BreakPointCpu cpu, const TraceEvent& ev);

        std::vector<TraceEvent> Drain(BreakPointCpu cpu, size_t n = 0);

        bool DumpToFile(BreakPointCpu cpu, const std::filesystem::path& path, const DumpBounds& bounds = {});
        size_t PendingCount(BreakPointCpu cpu);
        void Clear(BreakPointCpu cpu);

        u64 MonotonicTimestamp();

}
