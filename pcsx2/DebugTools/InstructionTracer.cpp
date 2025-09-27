// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "InstructionTracer.h"

#include "common/Assertions.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace InstructionTracer
{
        namespace
        {
                using Clock = std::chrono::steady_clock;

                constexpr size_t kDefaultCapacity = 8192;

                constexpr size_t cpuIndex(BreakPointCpu cpu)
                {
                        switch (cpu)
                        {
                                case BREAKPOINT_EE:
                                        return 0;
                                case BREAKPOINT_IOP:
                                        return 1;
                                case BREAKPOINT_IOP_AND_EE:
                                        return 0;
                                default:
                                        return 0;
                        }
                }

                class RingBuffer
                {
                public:
                        explicit RingBuffer(size_t capacity)
                                : m_capacity(capacity)
                                , m_mask(capacity - 1)
                                , m_buffer(capacity)
                        {
                                pxAssertRel((capacity != 0) && ((capacity & (capacity - 1)) == 0), "capacity must be power of two");
                        }

                        void push(const TraceEvent& ev)
                        {
                                const size_t head = m_head.load(std::memory_order_relaxed);
                                size_t tail = m_tail.load(std::memory_order_acquire);
                                const size_t next = head + 1;

                                if ((next - tail) > m_capacity)
                                {
                                        tail = tail + 1;
                                        m_tail.store(tail, std::memory_order_release);
                                        ++m_dropped;
                                }

                                m_buffer[head & m_mask] = ev;
                                m_head.store(next, std::memory_order_release);
                        }

                        template <typename OutputIt>
                        size_t drain(size_t n, OutputIt it)
                        {
                                size_t drained = 0;
                                size_t tail = m_tail.load(std::memory_order_relaxed);
                                const size_t head = m_head.load(std::memory_order_acquire);

                                while ((tail != head) && (drained < n))
                                {
                                        *it++ = m_buffer[tail & m_mask];
                                        ++tail;
                                        ++drained;
                                }

                                m_tail.store(tail, std::memory_order_release);
                                return drained;
                        }

                        size_t size() const
                        {
                                const size_t head = m_head.load(std::memory_order_acquire);
                                const size_t tail = m_tail.load(std::memory_order_acquire);
                                return head - tail;
                        }

                        void clear()
                        {
                                m_tail.store(m_head.load(std::memory_order_acquire), std::memory_order_release);
                        }

                        size_t dropped() const
                        {
                                return m_dropped.load(std::memory_order_relaxed);
                        }

                private:
                        const size_t m_capacity;
                        const size_t m_mask;
                        std::vector<TraceEvent> m_buffer;
                        std::atomic<size_t> m_head{0};
                        std::atomic<size_t> m_tail{0};
                        std::atomic<size_t> m_dropped{0};
                };

                struct CpuState
                {
                        std::atomic<bool> enabled{false};
                        RingBuffer ring{kDefaultCapacity};
                        std::mutex io_mutex;

                        CpuState() = default;
                        CpuState(const CpuState&) = delete;
                        CpuState& operator=(const CpuState&) = delete;
                };

                std::array<CpuState, 2> g_cpu_state = {CpuState{}, CpuState{}};

                std::string ToNativePath(const std::filesystem::path& path)
                {
                        return Path::ToNativePath(path.string());
                }

                std::string TempPathFor(const std::filesystem::path& path)
                {
                        std::filesystem::path temp = path;
                        temp += ".tmp";
                        return Path::ToNativePath(temp.string());
                }

                std::string Escape(std::string_view input)
                {
                        std::string out;
                        out.reserve(input.size() + 8);
                        for (char ch : input)
                        {
                                switch (ch)
                                {
                                        case '\\':
                                                out += "\\\\";
                                                break;
                                        case '"':
                                                out += "\\\"";
                                                break;
                                        case '\n':
                                                out += "\\n";
                                                break;
                                        case '\r':
                                                out += "\\r";
                                                break;
                                        case '\t':
                                                out += "\\t";
                                                break;
                                        default:
                                                out.push_back(ch);
                                                break;
                                }
                        }
                        return out;
                }

                std::string CpuName(BreakPointCpu cpu)
                {
                        return DebugInterface::cpuName(cpu);
                }

                std::string FormatAddress(u64 value)
                {
                        char buffer[32];
                        const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
                        if (ec != std::errc())
                                return "0x0";
                        std::string out = "0x";
                        out.append(buffer, ptr);
                        return out;
                }

                void WriteEventLine(std::FILE* fp, const TraceEvent& ev)
                {
                        const double timestamp = static_cast<double>(ev.timestamp_ns) / 1'000'000'000.0;
                        std::string disasm = Escape(ev.disasm);

                        std::fputs("{", fp);
                        std::fprintf(fp, "\"ts\":%.9f,", timestamp);
                        std::fprintf(fp, "\"cpu\":\"%s\",", CpuName(ev.cpu).c_str());
                        std::fprintf(fp, "\"pc\":\"%s\",", FormatAddress(ev.pc).c_str());
                        std::fprintf(fp, "\"opcode\":%u,", ev.opcode);
                        std::fprintf(fp, "\"cycles\":%llu,", static_cast<unsigned long long>(ev.cycles));
                        std::fprintf(fp, "\"op\":\"%s\"", disasm.c_str());

                        if (!ev.mem_reads.empty() || !ev.mem_writes.empty())
                        {
                                std::fputs(",\"mem\":{", fp);
                                if (!ev.mem_reads.empty())
                                {
                                        std::fputs("\"r\":[", fp);
                                        bool first = true;
                                        for (const auto& [addr, size] : ev.mem_reads)
                                        {
                                                if (!first)
                                                        std::fputc(',', fp);
                                                std::fprintf(fp, "[\"%s\",%u]", FormatAddress(addr).c_str(), size);
                                                first = false;
                                        }
                                        std::fputc(']', fp);
                                        if (!ev.mem_writes.empty())
                                                std::fputc(',', fp);
                                }
                                if (!ev.mem_writes.empty())
                                {
                                        std::fputs("\"w\":[", fp);
                                        bool first = true;
                                        for (const auto& [addr, size] : ev.mem_writes)
                                        {
                                                if (!first)
                                                        std::fputc(',', fp);
                                                std::fprintf(fp, "[\"%s\",%u]", FormatAddress(addr).c_str(), size);
                                                first = false;
                                        }
                                        std::fputc(']', fp);
                                }
                                std::fputc('}', fp);
                        }

                        std::fputs("}\n", fp);
                }
        }

        void Enable(BreakPointCpu cpu, bool on)
        {
                g_cpu_state[cpuIndex(cpu)].enabled.store(on, std::memory_order_release);
                if (!on)
                        Clear(cpu);
        }

        bool IsEnabled(BreakPointCpu cpu)
        {
                return g_cpu_state[cpuIndex(cpu)].enabled.load(std::memory_order_acquire);
        }

        void Record(BreakPointCpu cpu, const TraceEvent& ev)
        {
                const size_t index = cpuIndex(cpu);
                if (!g_cpu_state[index].enabled.load(std::memory_order_acquire))
                        return;

                g_cpu_state[index].ring.push(ev);
        }

        std::vector<TraceEvent> Drain(BreakPointCpu cpu, size_t n)
        {
                const size_t index = cpuIndex(cpu);
                size_t limit = n == 0 ? std::numeric_limits<size_t>::max() : n;
                std::vector<TraceEvent> buffer;
                buffer.reserve(std::min(limit, g_cpu_state[index].ring.size()));
                g_cpu_state[index].ring.drain(limit, std::back_inserter(buffer));
                return buffer;
        }

        bool DumpToFile(BreakPointCpu cpu, const std::filesystem::path& path, const DumpBounds& bounds)
        {
                const size_t index = cpuIndex(cpu);
                std::lock_guard<std::mutex> guard(g_cpu_state[index].io_mutex);

                const std::string temp_path = TempPathFor(path);
                auto file = FileSystem::OpenManagedCFile(temp_path.c_str(), "wb");
                if (!file)
                        return false;

                size_t limit = bounds.limit.value_or(std::numeric_limits<size_t>::max());
                std::vector<TraceEvent> buffer = Drain(cpu, limit);
                for (const TraceEvent& ev : buffer)
                        WriteEventLine(file.get(), ev);

                file.reset();
                return FileSystem::RenamePath(temp_path.c_str(), ToNativePath(path).c_str());
        }

        size_t PendingCount(BreakPointCpu cpu)
        {
                return g_cpu_state[cpuIndex(cpu)].ring.size();
        }

        void Clear(BreakPointCpu cpu)
        {
                g_cpu_state[cpuIndex(cpu)].ring.clear();
        }

        u64 MonotonicTimestamp()
        {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        }
}
