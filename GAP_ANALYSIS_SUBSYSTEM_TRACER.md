# Gap Analysis: Subsystem Tracer Implementation

**Date**: 2025-11-06
**Analyst**: Deep code review following first principles analysis
**Severity**: 🔴 **CRITICAL** - Feature fundamentally broken

---

## Executive Summary

The subsystem tracer implementation appears complete with comprehensive detection logic, UI integration, and documentation. However, **first-principles analysis reveals a critical architectural gap**: the memory access tracking infrastructure is never populated, rendering 95% of the subsystem detection logic non-functional.

**Impact**: The user's primary use case (asset extraction via GIF/GS/DMA detection) **does not work**.

---

## 1. What Was Implemented ✅

### 1.1 Detection Logic (COMPLETE)
- ✅ `Subsystems.h/cpp`: 22 subsystem types with comprehensive PS2 memory map coverage
- ✅ `DetectFromMemoryAddress()`: Maps all EE and IOP hardware registers to subsystems
- ✅ `DetectFromSyscall()`: BIOS syscall detection via opcode 0x0C
- ✅ `GetDetailString()`: Human-readable descriptions with DMA channel names

### 1.2 Data Model (COMPLETE)
- ✅ `TraceEvent` extended with `subsystem` (u8) and `subsystem_detail` (string)
- ✅ Pre-existing `mem_r` and `mem_w` vectors for memory access tracking

### 1.3 Integration Points (COMPLETE)
- ✅ EE tracer hook (iR5900.cpp lines 1527-1595)
- ✅ IOP tracer hook (iR3000A.cpp lines 1290-1351)
- ✅ UI display with color coding (InstructionTraceView.cpp lines 374-417)

### 1.4 Documentation (COMPLETE)
- ✅ SUBSYSTEM_TRACER_COMPLETE.md
- ✅ TRACER_ENHANCEMENT_PLAN.md
- ✅ BUILD_REQUIREMENTS.md

---

## 2. Critical Gap Discovered 🔴

### 2.1 The Problem

**File**: `pcsx2/x86/ix86-32/iR5900.cpp`
**Lines**: 1560-1587 (subsystem detection logic)

```cpp
// Check memory accesses (writes first, then reads)
for (const auto& [addr, size] : ev.mem_w)  // ⚠️ ev.mem_w is ALWAYS EMPTY
{
    Subsystem::Type sub = Subsystem::DetectFromMemoryAddress(addr, true);
    if (sub != Subsystem::Type::None)
    {
        ev.subsystem = static_cast<u8>(sub);
        // ... set detail string
        break;
    }
}

// Check reads if no writes matched
if (ev.subsystem == static_cast<u8>(Subsystem::Type::None))
{
    for (const auto& [addr, size] : ev.mem_r)  // ⚠️ ev.mem_r is ALSO ALWAYS EMPTY
    {
        // ... detection logic (never executes)
    }
}
```

### 2.2 Root Cause Analysis

**Observation**: The vectors `ev.mem_r` and `ev.mem_w` are declared in `TraceEvent` but **never populated**.

**Evidence**:
```bash
# Search for ANY code that populates these vectors
grep -rn "\.mem_w\s*\." pcsx2/x86/ix86-32/iR5900.cpp
grep -rn "\.mem_r\s*\." pcsx2/x86/ix86-32/iR5900.cpp
grep -rn "push_back\|emplace\|insert" pcsx2/x86/ix86-32/iR5900.cpp | grep "mem_"

# Result: NO matches - vectors are NEVER written to
```

**Conclusion**: The detection logic is **architecturally isolated** from the memory access system.

### 2.3 Why This Happened

The tracer hook (`dynarecCheckBreakpoint`) executes **AFTER** instruction execution completes:

1. JIT compiled code runs instruction
2. Breakpoint check happens (if enabled)
3. Tracer hook invoked
4. At this point: PC, opcode, registers are available
5. **BUT**: Memory access information is **already lost**

The tracer hook has **no visibility** into:
- Which memory addresses were read/written
- Size of memory operations
- Order of memory accesses

---

## 3. Impact Assessment

### 3.1 What Works ✅

**BIOS Syscall Detection** (lines 1548-1558):
```cpp
const u32 v1_reg = cpuRegs.GPR.r[3].UD[0];
Subsystem::Type syscall_subsys = Subsystem::DetectFromSyscall(opcode, v1_reg);
if (syscall_subsys != Subsystem::Type::None)
{
    ev.subsystem = static_cast<u8>(syscall_subsys);
    // ... works correctly
}
```

**Why it works**: Syscall detection only needs opcode and register values, both available at hook time.

**Coverage**: ~5% of subsystem events (BIOS calls only)

### 3.2 What Doesn't Work ❌

**Memory-Based Detection** (lines 1560-1587):
- ❌ GS register writes (texture uploads)
- ❌ GIF tag writes (graphics data transfer)
- ❌ DMA channel configuration
- ❌ SPU2 register access (audio)
- ❌ CDVD register access (disk I/O)
- ❌ VIF0/VIF1 writes (VU program upload)
- ❌ All IOP hardware access
- ❌ All memory-mapped I/O

**Coverage**: ~95% of subsystem events

### 3.3 User Impact

**User's Request**: "Is it possible for the function trace to highlight plugin interactions so we can get a clearer picture of the emulation flow? eg if we want to extract assets"

**Asset Extraction Workflow** (from TRACER_ENHANCEMENT_PLAN.md):
1. Start tracing with GS/GIF filter ❌ **Doesn't work**
2. Identify texture upload patterns (DMA CH2) ❌ **Not detected**
3. Capture memory contents at upload addresses ❌ **Addresses unknown**
4. Reconstruct asset format ❌ **Can't proceed**

**Verdict**: The primary use case **does not work**.

---

## 4. Architectural Analysis

### 4.1 Existing PCSX2 Memory Infrastructure

**Memory Access Hooks** (found in codebase):

1. **vtlb System** (`pcsx2/vtlb.cpp`):
   - Low-level memory virtualization
   - Handles all memory reads/writes
   - Functions: `vtlb_memRead`, `vtlb_memWrite`

2. **Memory Checks** (`pcsx2/DebugTools/DebugInterface.h`):
   ```cpp
   class DebugInterface
   {
   public:
       virtual u8 read8(u32 address) = 0;
       virtual u16 read16(u32 address) = 0;
       virtual u32 read32(u32 address) = 0;
       virtual void write8(u32 address, u8 value) = 0;
       // ... etc
   };
   ```

3. **Breakpoint Memory Checks** (`pcsx2/DebugTools/BiosDebugData.cpp`):
   - `CBreakPoints::GetMemChecks()`
   - Can trigger on memory access ranges

### 4.2 Integration Gap

**Current Flow** (broken):
```
[JIT Instruction] → [Execute] → [Memory Access (lost)] → [Breakpoint Check] → [Tracer Hook]
                                        ↑
                                     PROBLEM: No connection
```

**Required Flow**:
```
[JIT Instruction] → [Execute] → [Memory Access] → [Tracer Capture] → [Breakpoint Check] → [Tracer Hook]
                                                         ↑
                                                   NEW: Capture layer
```

---

## 5. Remediation Options

### Option A: Instruction Decoder Integration ⭐ **RECOMMENDED**

**Approach**: Decode the opcode at hook time to determine memory operation details.

**Implementation**:
```cpp
// In dynarecCheckBreakpoint, before subsystem detection:

// Decode instruction to get memory access info
struct MemoryOperation {
    u64 addr;
    u8 size;
    bool is_write;
};

std::vector<MemoryOperation> DecodeMemoryAccess(u32 opcode, const R5900Regs& regs)
{
    // MIPS R5900 instruction formats:
    // Load:  LB/LH/LW/LD/LQ at rt, offset(rs)
    // Store: SB/SH/SW/SD/SQ rt, offset(rs)

    u32 op = opcode >> 26;  // Primary opcode
    u32 rs = (opcode >> 21) & 0x1F;
    u32 rt = (opcode >> 16) & 0x1F;
    s16 imm = opcode & 0xFFFF;

    std::vector<MemoryOperation> ops;

    switch (op) {
        case 0x20: // LB
        case 0x24: // LBU
            ops.push_back({regs.GPR.r[rs].UD[0] + imm, 1, false});
            break;
        case 0x21: // LH
        case 0x25: // LHU
            ops.push_back({regs.GPR.r[rs].UD[0] + imm, 2, false});
            break;
        case 0x23: // LW
        case 0x27: // LWU
            ops.push_back({regs.GPR.r[rs].UD[0] + imm, 4, false});
            break;
        case 0x28: // SB
            ops.push_back({regs.GPR.r[rs].UD[0] + imm, 1, true});
            break;
        case 0x29: // SH
            ops.push_back({regs.GPR.r[rs].UD[0] + imm, 2, true});
            break;
        case 0x2B: // SW
            ops.push_back({regs.GPR.r[rs].UD[0] + imm, 4, true});
            break;
        // ... handle all load/store opcodes
    }

    return ops;
}

// Then populate TraceEvent:
std::vector<MemoryOperation> mem_ops = DecodeMemoryAccess(opcode, cpuRegs);
for (const auto& op : mem_ops) {
    if (op.is_write)
        ev.mem_w.push_back({op.addr, op.size});
    else
        ev.mem_r.push_back({op.addr, op.size});
}
```

**Pros**:
- ✅ Self-contained (no external dependencies)
- ✅ Works for all traced instructions
- ✅ Minimal performance impact (~50 ns per instruction)
- ✅ Accurate (uses actual CPU register state)

**Cons**:
- ⚠️ Requires MIPS R5900 opcode knowledge
- ⚠️ Must handle ~50 load/store instruction variants
- ⚠️ Complex instructions (LQ/SQ, indexed loads) need special handling

**Effort**: ~4 hours (create decoder, test all opcodes)

---

### Option B: Memory Hook Integration

**Approach**: Hook into vtlb memory access functions to capture addresses in real-time.

**Implementation**:
```cpp
// In vtlb.cpp or new MemoryTracer.cpp

thread_local std::vector<std::pair<u64, u8>> g_recent_mem_reads;
thread_local std::vector<std::pair<u64, u8>> g_recent_mem_writes;

// Hook vtlb functions:
u32 vtlb_memRead32(u32 addr)
{
    if (InstructionTracer::IsEnabled()) {
        g_recent_mem_reads.push_back({addr, 4});
    }
    // ... original code
}

void vtlb_memWrite32(u32 addr, u32 value)
{
    if (InstructionTracer::IsEnabled()) {
        g_recent_mem_writes.push_back({addr, 4});
    }
    // ... original code
}

// In tracer hook:
ev.mem_r = std::move(g_recent_mem_reads);
ev.mem_w = std::move(g_recent_mem_writes);
g_recent_mem_reads.clear();
g_recent_mem_writes.clear();
```

**Pros**:
- ✅ Captures actual memory accesses (not predictions)
- ✅ Works for all instruction types automatically
- ✅ Can capture indirect/complex memory patterns

**Cons**:
- ❌ Performance overhead on EVERY memory access (even when tracing disabled)
- ❌ Thread-local storage overhead
- ❌ Requires vtlb modifications (core system)
- ❌ May capture multiple instructions' accesses (timing issue)

**Effort**: ~3 hours (add hooks, test thread safety)

---

### Option C: Memory Check System Integration

**Approach**: Leverage existing `CBreakPoints::GetMemChecks()` infrastructure.

**Implementation**:
```cpp
// In tracer hook:

// Query recent memory check hits
auto mem_checks = CBreakPoints::GetRecentMemoryAccesses();  // New function needed
for (const auto& check : mem_checks) {
    if (check.is_write)
        ev.mem_w.push_back({check.addr, check.size});
    else
        ev.mem_r.push_back({check.addr, check.size});
}
```

**Pros**:
- ✅ Uses existing infrastructure
- ✅ No performance impact (checks already exist)

**Cons**:
- ❌ Only works for explicitly watched addresses
- ❌ Requires user to set memory breakpoints in advance
- ❌ Not suitable for general tracing (too limited)
- ❌ Defeats purpose of automatic subsystem detection

**Effort**: ~2 hours (expose memory check API)

---

## 6. Recommendation

### ⭐ Implement Option A: Instruction Decoder Integration

**Rationale**:
1. **Self-contained**: No dependencies on core emulator changes
2. **Performance**: Minimal overhead (~50 ns per traced event, zero when disabled)
3. **Accuracy**: Uses actual CPU state at execution time
4. **Completeness**: Works for all instructions
5. **Maintainability**: Isolated in tracer module

**Implementation Plan**:

#### Phase 1: MIPS R5900 Decoder (2 hours)
- Create `pcsx2/DebugTools/MipsDecoder.h`
- Implement decoder for common load/store instructions:
  - LB/LBU/LH/LHU/LW/LWU/LD (loads)
  - SB/SH/SW/SD (stores)
  - LQ/SQ (quadword loads/stores)
  - LWL/LWR/SWL/SWR (unaligned access)

#### Phase 2: Integrate into Tracer Hooks (1 hour)
- Add decoder calls in `iR5900.cpp` and `iR3000A.cpp`
- Populate `ev.mem_r` and `ev.mem_w` before subsystem detection
- Verify detection logic executes correctly

#### Phase 3: Testing (1 hour)
- Unit tests for decoder (all instruction types)
- Integration test: Trigger GS register write, verify detection
- Validate UI displays subsystem correctly

**Total Effort**: ~4 hours

---

## 7. Alternative: Document as Known Limitation

If immediate fix is not feasible, **document the limitation** clearly:

### Update SUBSYSTEM_TRACER_COMPLETE.md

Add section:

```markdown
## ⚠️ Known Limitations

### Memory-Based Detection Not Implemented

**Status**: Syscall detection works, memory-based detection does NOT work.

**What Works**:
- ✅ BIOS syscall detection (LoadExecPS2, etc.)

**What Doesn't Work**:
- ❌ GS/GIF register access detection
- ❌ DMA channel detection
- ❌ SPU2 register access
- ❌ CDVD register access
- ❌ All memory-mapped I/O detection

**Root Cause**: Memory access tracking (mem_r/mem_w) not implemented.

**Workaround**: Currently limited to syscall tracing only.

**Fix**: Requires instruction decoder integration (see GAP_ANALYSIS_SUBSYSTEM_TRACER.md).
```

---

## 8. Impact on Documentation

### Files Requiring Updates

1. **SUBSYSTEM_TRACER_COMPLETE.md**:
   - Add "Known Limitations" section
   - Remove claims about asset extraction workflow
   - Update "What Works" section to clarify syscall-only

2. **TRACER_ENHANCEMENT_PLAN.md**:
   - Mark Phase 1 (memory detection) as "NOT IMPLEMENTED"
   - Update status to "Phase 2 only (syscall detection)"

3. **INTEGRATION_SUMMARY.md**:
   - Clarify subsystem tracer is "partial implementation"
   - Update progress percentage

---

## 9. Risk Assessment

### If Shipped As-Is

**Risk Level**: 🟡 **MEDIUM** (not critical, but misleading)

**Risks**:
- ❌ Users expect asset extraction workflow (doesn't work)
- ❌ Documentation claims feature works (incorrect)
- ❌ Wastes user time trying non-functional feature
- ❌ Reduces trust in AGENT.md integration quality

**Mitigation**:
- Update documentation to clarify limitations
- Add warning in UI if subsystem detection likely inaccurate
- Provide roadmap for completion

### If Fixed with Option A

**Risk Level**: 🟢 **LOW**

**Benefits**:
- ✅ Feature works as documented
- ✅ User's asset extraction use case enabled
- ✅ Complete subsystem visibility
- ✅ Professional quality implementation

---

## 10. Summary

### Critical Findings

1. **Subsystem detection logic is correct** but receives no memory access data
2. **mem_r and mem_w vectors are never populated** (architectural gap)
3. **Only BIOS syscall detection works** (~5% coverage)
4. **User's primary use case (asset extraction) doesn't work**
5. **Documentation incorrectly claims feature is complete**

### Recommended Action

**Implement Option A** (Instruction Decoder Integration):
- 4 hours effort
- Self-contained solution
- Enables full subsystem detection
- Minimal performance impact
- Fulfills user's request

### Alternative Actions

1. **Document limitation** and defer fix to future PR
2. **Revert subsystem tracer** and re-plan with memory tracking first
3. **Implement Option B** if performance overhead acceptable

---

## 11. Decision Required

**Question for User/Maintainer**:

> The subsystem tracer appears complete but has a critical gap: memory access tracking is not implemented, breaking 95% of the detection logic. The user's primary use case (asset extraction) doesn't work.
>
> **Options**:
> 1. Fix now with instruction decoder (~4 hours)
> 2. Document as known limitation and defer
> 3. Revert and re-implement properly
>
> **Recommendation**: Option 1 (fix now) - the decoder is straightforward and completes the feature properly.

---

**End of Gap Analysis**
