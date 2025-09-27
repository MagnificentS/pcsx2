// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "DebugInterface.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

struct MemCheck;

class MemoryScanner
{
public:
        enum class ValueType
        {
                U8,
                U16,
                U32,
                U64,
                F32,
                F64
        };

        enum class Comparison
        {
                Equal,
                NotEqual,
                Greater,
                Less,
                Approximate
        };

        struct Value
        {
                u64 integer = 0;
                double floating = 0.0;
        };

        struct Query
        {
                BreakPointCpu cpu = BREAKPOINT_EE;
                u64 begin = 0;
                u64 end = 0;
                ValueType type = ValueType::U8;
                Comparison comparison = Comparison::Equal;
                Value value;
                double epsilon = 0.0;
        };

        struct Result
        {
                u64 address = 0;
                std::vector<u8> bytes;
        };

        struct DumpSpec
        {
                u64 start = 0;
                u32 size = 0;
                bool include_metadata = true;
        };

        using ScanId = u64;

        static constexpr ScanId kInvalidScanId = 0;

        static MemoryScanner& Get();

        ScanId SubmitInitial(const Query& query);
        ScanId SubmitRescan(ScanId id, const Query& query);
        void Cancel(ScanId id);
        std::vector<Result> Results(ScanId id) const;

        bool DumpOnChange(BreakPointCpu cpu, u64 addr, const std::filesystem::path& out_path, const DumpSpec& spec);
        void HandleMemCheckHit(const MemCheck& memcheck, u32 addr, bool write, int size, u32 pc);

private:
        struct ScanJob
        {
                Query query;
                std::vector<u8> snapshot;
                std::vector<Result> results;
        };

        struct Watch
        {
                BreakPointCpu cpu = BREAKPOINT_EE;
                DumpSpec spec;
                std::filesystem::path out_path;
        };

        MemoryScanner() = default;

        bool SnapshotRange(const Query& query, std::vector<u8>& out_snapshot, bool run_on_cpu_thread) const;
        void PerformScan(ScanJob& job, const Query& query);
        bool Evaluate(const Query& query, const u8* base) const;
        static size_t ValueSize(ValueType type);
        bool WriteDump(const Watch& watch) const;

        ScanId AllocateId();

        mutable std::mutex m_mutex;
        std::unordered_map<ScanId, ScanJob> m_jobs;
        std::vector<Watch> m_watches;
        std::atomic<ScanId> m_next_id{1};
};

namespace MemoryScannerHooks
{
        void OnMemCheckHit(const MemCheck& memcheck, u32 addr, bool write, int size, u32 pc);
}
