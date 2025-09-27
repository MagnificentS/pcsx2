// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "MemoryScanner.h"

#include "Breakpoints.h"
#include "DebugInterface.h"
#include "InstructionTracer.h"

#include "common/FileSystem.h"
#include "common/Path.h"

#include "pcsx2/Host.h"
#include "pcsx2/VMManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
        template <typename T>
        T LoadValue(const u8* base)
        {
                T value;
                std::memcpy(&value, base, sizeof(T));
                return value;
        }

        bool ReadRange(DebugInterface& cpu, u64 begin, u64 end, std::vector<u8>& out)
        {
                if (end <= begin)
                        return false;

                const u64 size = end - begin;
                out.resize(size);

                for (u64 offset = 0; offset < size; ++offset)
                {
                        out[offset] = static_cast<u8>(cpu.read8(static_cast<u32>(begin + offset)));
                }

                return true;
        }

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
}

MemoryScanner& MemoryScanner::Get()
{
        static MemoryScanner instance;
        return instance;
}

MemoryScanner::ScanId MemoryScanner::AllocateId()
{
        return m_next_id.fetch_add(1, std::memory_order_relaxed);
}

bool MemoryScanner::SnapshotRange(const Query& query, std::vector<u8>& out_snapshot, bool run_on_cpu_thread) const
{
        if (run_on_cpu_thread)
        {
                bool ok = false;
                Host::RunOnCPUThread(
                        [&]() {
                                DebugInterface& cpu = DebugInterface::get(query.cpu);
                                ok = ReadRange(cpu, query.begin, query.end, out_snapshot);
                        },
                        true);
                return ok;
        }

        DebugInterface& cpu = DebugInterface::get(query.cpu);
        return ReadRange(cpu, query.begin, query.end, out_snapshot);
}

size_t MemoryScanner::ValueSize(ValueType type)
{
        switch (type)
        {
                case ValueType::U8:
                        return sizeof(u8);
                case ValueType::U16:
                        return sizeof(u16);
                case ValueType::U32:
                        return sizeof(u32);
                case ValueType::U64:
                        return sizeof(u64);
                case ValueType::F32:
                        return sizeof(float);
                case ValueType::F64:
                        return sizeof(double);
                default:
                        return sizeof(u8);
        }
}

bool MemoryScanner::Evaluate(const Query& query, const u8* base) const
{
        switch (query.type)
        {
                case ValueType::U8:
                {
                        const u64 actual = LoadValue<u8>(base);
                        const u64 expected = query.value.integer & 0xFFu;
                        switch (query.comparison)
                        {
                                case Comparison::Equal:
                                        return actual == expected;
                                case Comparison::NotEqual:
                                        return actual != expected;
                                case Comparison::Greater:
                                        return actual > expected;
                                case Comparison::Less:
                                        return actual < expected;
                                case Comparison::Approximate:
                                        return std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= query.epsilon;
                        }
                        break;
                }
                case ValueType::U16:
                {
                        const u64 actual = LoadValue<u16>(base);
                        const u64 expected = query.value.integer & 0xFFFFu;
                        switch (query.comparison)
                        {
                                case Comparison::Equal:
                                        return actual == expected;
                                case Comparison::NotEqual:
                                        return actual != expected;
                                case Comparison::Greater:
                                        return actual > expected;
                                case Comparison::Less:
                                        return actual < expected;
                                case Comparison::Approximate:
                                        return std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= query.epsilon;
                        }
                        break;
                }
                case ValueType::U32:
                {
                        const u64 actual = LoadValue<u32>(base);
                        const u64 expected = static_cast<u32>(query.value.integer);
                        switch (query.comparison)
                        {
                                case Comparison::Equal:
                                        return actual == expected;
                                case Comparison::NotEqual:
                                        return actual != expected;
                                case Comparison::Greater:
                                        return actual > expected;
                                case Comparison::Less:
                                        return actual < expected;
                                case Comparison::Approximate:
                                        return std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= query.epsilon;
                        }
                        break;
                }
                case ValueType::U64:
                {
                        const u64 actual = LoadValue<u64>(base);
                        const u64 expected = query.value.integer;
                        switch (query.comparison)
                        {
                                case Comparison::Equal:
                                        return actual == expected;
                                case Comparison::NotEqual:
                                        return actual != expected;
                                case Comparison::Greater:
                                        return actual > expected;
                                case Comparison::Less:
                                        return actual < expected;
                                case Comparison::Approximate:
                                        return std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= query.epsilon;
                        }
                        break;
                }
                case ValueType::F32:
                {
                        const double actual = static_cast<double>(LoadValue<float>(base));
                        const double expected = static_cast<double>(query.value.floating);
                        switch (query.comparison)
                        {
                                case Comparison::Equal:
                                        return actual == expected;
                                case Comparison::NotEqual:
                                        return actual != expected;
                                case Comparison::Greater:
                                        return actual > expected;
                                case Comparison::Less:
                                        return actual < expected;
                                case Comparison::Approximate:
                                        return std::fabs(actual - expected) <= query.epsilon;
                        }
                        break;
                }
                case ValueType::F64:
                {
                        const double actual = LoadValue<double>(base);
                        const double expected = query.value.floating;
                        switch (query.comparison)
                        {
                                case Comparison::Equal:
                                        return actual == expected;
                                case Comparison::NotEqual:
                                        return actual != expected;
                                case Comparison::Greater:
                                        return actual > expected;
                                case Comparison::Less:
                                        return actual < expected;
                                case Comparison::Approximate:
                                        return std::fabs(actual - expected) <= query.epsilon;
                        }
                        break;
                }
        }

        return false;
}

void MemoryScanner::PerformScan(ScanJob& job, const Query& query)
{
        job.query = query;
        job.results.clear();
        if (query.end <= query.begin)
                return;

        if (!SnapshotRange(query, job.snapshot, true))
                return;

        const size_t element_size = ValueSize(query.type);
        if (element_size == 0)
                return;

        const u64 range_size = query.end - query.begin;
        for (u64 offset = 0; offset + element_size <= range_size; offset += element_size)
        {
                if (Evaluate(query, job.snapshot.data() + offset))
                {
                        Result result;
                        result.address = query.begin + offset;
                        result.bytes.assign(job.snapshot.data() + offset, job.snapshot.data() + offset + element_size);
                        job.results.push_back(std::move(result));
                }
        }
}

MemoryScanner::ScanId MemoryScanner::SubmitInitial(const Query& query)
{
        if (query.end <= query.begin)
                return kInvalidScanId;

        if (VMManager::GetState() != VMState::Paused)
                return kInvalidScanId;

        std::lock_guard<std::mutex> lock(m_mutex);
        ScanJob job;
        PerformScan(job, query);

        if (job.results.empty() && job.snapshot.empty())
                return kInvalidScanId;

        const ScanId id = AllocateId();
        m_jobs.emplace(id, std::move(job));
        return id;
}

MemoryScanner::ScanId MemoryScanner::SubmitRescan(ScanId id, const Query& query)
{
        if (id == kInvalidScanId)
                return kInvalidScanId;

        if (VMManager::GetState() != VMState::Paused)
                return kInvalidScanId;

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_jobs.find(id);
        if (it == m_jobs.end())
                return kInvalidScanId;

        PerformScan(it->second, query);
        return id;
}

void MemoryScanner::Cancel(ScanId id)
{
        if (id == kInvalidScanId)
                return;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.erase(id);
}

std::vector<MemoryScanner::Result> MemoryScanner::Results(ScanId id) const
{
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_jobs.find(id);
        if (it == m_jobs.end())
                return {};
        return it->second.results;
}

bool MemoryScanner::DumpOnChange(BreakPointCpu cpu, u64 addr, const std::filesystem::path& out_path, const DumpSpec& spec)
{
        if (spec.size == 0)
                return false;

        Watch watch;
        watch.cpu = cpu;
        watch.spec = spec;
        watch.spec.start = spec.start != 0 ? spec.start : addr;
        watch.out_path = out_path;

        bool inserted = false;
        {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto existing = std::find_if(m_watches.begin(), m_watches.end(), [&](const Watch& other) {
                        return other.cpu == watch.cpu && other.spec.start == watch.spec.start &&
                               other.spec.size == watch.spec.size && other.out_path == watch.out_path;
                });

                if (existing != m_watches.end())
                        *existing = watch;
                else
                {
                        m_watches.push_back(watch);
                        inserted = true;
                }
        }

        if (inserted)
        {
                const u32 start = static_cast<u32>(watch.spec.start);
                const u32 end = static_cast<u32>(watch.spec.start + spec.size);
                CBreakPoints::AddMemCheck(cpu, start, end, static_cast<MemCheckCondition>(MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE),
                        MEMCHECK_LOG);
        }
        return true;
}

bool MemoryScanner::WriteDump(const Watch& watch) const
{
        std::vector<u8> buffer;
        Query query;
        query.cpu = watch.cpu;
        query.begin = watch.spec.start;
        query.end = watch.spec.start + watch.spec.size;
        query.type = ValueType::U8;

        if (!SnapshotRange(query, buffer, false))
                return false;

        const std::string temp_path = TempPathFor(watch.out_path);
        auto file = FileSystem::OpenManagedCFile(temp_path.c_str(), "wb");
        if (!file)
                return false;

        if (watch.spec.include_metadata)
        {
                std::fprintf(
                        file.get(),
                        "{ \"version\":1, \"space\":\"%s\", \"start\":%u, \"size\":%u }\n--\n",
                        DebugInterface::cpuName(watch.cpu),
                        static_cast<u32>(watch.spec.start),
                        watch.spec.size);
        }

        if (!buffer.empty())
                std::fwrite(buffer.data(), 1, buffer.size(), file.get());

        file.reset();
        return FileSystem::RenamePath(temp_path.c_str(), ToNativePath(watch.out_path).c_str());
}

void MemoryScanner::HandleMemCheckHit(const MemCheck& memcheck, u32 addr, bool write, int size, u32 pc)
{
        if (InstructionTracer::IsEnabled(memcheck.cpu))
        {
                DebugInterface& iface = DebugInterface::get(memcheck.cpu);
                InstructionTracer::TraceEvent event;
                event.cpu = memcheck.cpu;
                event.pc = pc ? pc : iface.getPC();
                event.opcode = iface.read32(event.pc);
                event.disasm = iface.disasm(event.pc, true);
                event.cycles = iface.getCycles();
                event.timestamp_ns = InstructionTracer::MonotonicTimestamp();

                const u8 clamped_size = size > 0 ? static_cast<u8>(std::min(size, 255)) : 0;
                auto& summary = write ? event.mem_writes : event.mem_reads;
                summary.emplace_back(static_cast<u64>(addr), clamped_size);

                InstructionTracer::Record(memcheck.cpu, event);
        }

        std::vector<Watch> to_process;
        {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (const Watch& watch : m_watches)
                {
                        if (watch.cpu != memcheck.cpu)
                                continue;
                        if (addr < watch.spec.start)
                                continue;
                        if (addr >= (watch.spec.start + watch.spec.size))
                                continue;
                        to_process.push_back(watch);
                }
        }

        for (const Watch& watch : to_process)
                WriteDump(watch);
}

namespace MemoryScannerHooks
{
        void OnMemCheckHit(const MemCheck& memcheck, u32 addr, bool write, int size, u32 pc)
        {
                MemoryScanner::Get().HandleMemCheckHit(memcheck, addr, write, size, pc);
        }
}
