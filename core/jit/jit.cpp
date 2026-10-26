// See jit.h.

#include "jit.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "a64_assembler.h"
#include "context.h"
#include "translator.h"

#if defined(__aarch64__) && defined(__linux__)
#include <sys/mman.h>
#define KAIROTOWNS_JIT_RUNS 1
#endif

namespace kairotowns::jit {

bool supported() {
#ifdef KAIROTOWNS_JIT_RUNS
    return true;
#else
    return false;
#endif
}

extern "C" uint32_t kairotowns_jit_materialize(Context *ctx, uint32_t kind, uint32_t base, uint32_t a, uint32_t b) {
    i486DXCommon &cpu = *ctx->cpu;
    cpu.state.EFLAGS = base;
    unsigned int value = a;
    const uint32_t bits = flag_bits(kind);
#define KAIROTOWNS_BY_SIZE(name) (bits == 8 ? cpu.name##Byte(value, b) : bits == 16 ? cpu.name##Word(value, b) : cpu.name##Dword(value, b))
    switch (flag_op(kind)) {
    case FLAGS_ADD: KAIROTOWNS_BY_SIZE(Add); break;
    case FLAGS_ADC: KAIROTOWNS_BY_SIZE(Adc); break;
    case FLAGS_SUB: KAIROTOWNS_BY_SIZE(Sub); break;
    case FLAGS_SBB: KAIROTOWNS_BY_SIZE(Sbb); break;
    case FLAGS_AND: KAIROTOWNS_BY_SIZE(And); break;
    case FLAGS_OR: KAIROTOWNS_BY_SIZE(Or); break;
    case FLAGS_XOR: KAIROTOWNS_BY_SIZE(Xor); break;
    case FLAGS_INC:
        if (bits == 8) cpu.IncrementByte(value);
        else if (bits == 16) cpu.IncrementWord(value);
        else cpu.IncrementDword(value);
        break;
    case FLAGS_DEC:
        if (bits == 8) cpu.DecrementByte(value);
        else if (bits == 16) cpu.DecrementWord(value);
        else cpu.DecrementDword(value);
        break;
    case FLAGS_ROL: KAIROTOWNS_BY_SIZE(Rol); break;
    case FLAGS_ROR: KAIROTOWNS_BY_SIZE(Ror); break;
    case FLAGS_SHL: KAIROTOWNS_BY_SIZE(Shl); break;
    case FLAGS_SHR: KAIROTOWNS_BY_SIZE(Shr); break;
    case FLAGS_SAR: KAIROTOWNS_BY_SIZE(Sar); break;
    default:
        throw std::logic_error("materialize: no flags kind " + std::to_string(kind));
    }
#undef KAIROTOWNS_BY_SIZE
    return cpu.state.EFLAGS;
}

// Watching what may write memory outside translated code (Jit::Mark).
static Jit::Mark mark_of(const Context *ctx, bool segments);
static void account(Context *ctx, const Jit::Mark &m);

extern "C" uint32_t kairotowns_jit_io(Context *ctx, uint32_t what, uint32_t port, uint32_t value, uint32_t clocks_before,
                                      uint32_t next_eip) {
    i486DXCommon &cpu = *ctx->cpu;
    InOut &io = *ctx->io;
    // Guest time as it is when the interpreter runs this instruction: devices read it.
    ctx->host->advance(ctx->host->owner, clocks_before - ctx->advanced);
    ctx->advanced = clocks_before;
    const uint64_t effects = *ctx->side_effects;
    const Jit::Mark mark = mark_of(ctx, false);
    const uint32_t bytes = (what >> 8) & 0xFF, length = what >> 16;
    if (what & 1) {
        if (bytes == 1) cpu.IOOut8(io, port, value & 0xFF);
        else if (bytes == 2) cpu.IOOut16(io, port, value & 0xFFFF);
        else cpu.IOOut32(io, port, value);
    } else if (bytes == 1) {
        const auto read = cpu.IOIn8(io, port);
        if (!cpu.state.exception) cpu.SetAL(read);
    } else if (bytes == 2) {
        const auto read = cpu.IOIn16(io, port);
        if (!cpu.state.exception) cpu.SetAX(read);
    } else {
        const auto read = cpu.IOIn32(io, port);
        if (!cpu.state.exception) cpu.SetEAX(read);
    }
    cpu.state.EIP += length;
    // A device may have remapped or written memory; one that wrote it also ends the run.
    ctx->refresh_tlb(ctx->jit);
    account(ctx, mark);
    // The run goes on only where nothing it relies on changed: the device did not move EIP (a
    // reset), write memory (code included), raise a fault, or give the interpreter's loop a
    // reason to stop.
    return cpu.state.EIP != next_eip || *ctx->side_effects != effects || cpu.state.exception ||
           ctx->host->must_stop(ctx->host->owner);
}

// The modes translated code depends on, as the CPU is now.
Mode mode_of(const i486DXCommon &cpu) {
    Mode mode;
    mode.operand_size = cpu.KairoCodeOperandSize();
    mode.address_size = cpu.KairoCodeAddressSize();
    mode.stack_address_size = cpu.GetStackAddressingSize();
    mode.paging = cpu.PagingEnabled();
    mode.cpu_mode = cpu.state.mode;
    return mode;
}

// Where a run that has taken clocks goes on for room more: what its clock limit moves by.
static int32_t go_on_for(Context *ctx, uint32_t clocks, int32_t room) {
    const int64_t moved = int64_t(clocks) + room - ctx->clock_limit;
    if (moved <= int64_t(kStopRun) || moved > INT32_MAX || int64_t(ctx->clock_limit) + moved > INT32_MAX)
        throw std::logic_error("a run's clock limit out of range");
    ctx->clock_limit += int32_t(moved);
    return int32_t(moved);
}

extern "C" int32_t kairotowns_jit_jumped_back(Context *ctx, uint32_t clocks_before, uint32_t instructions, uint32_t jump_eip) {
    i486DXCommon &cpu = *ctx->cpu;
    ctx->host->advance(ctx->host->owner, clocks_before - ctx->advanced);
    ctx->advanced = clocks_before;
    if (!ctx->host->jumped_back(ctx->host->owner, cpu.state.CS().value, jump_eip, instructions, clocks_before)) return 0;
    // Guest time moved: the run ends as the machine's loop ends one, and goes on as it starts one.
    const uint32_t eip = cpu.state.EIP;
    const uint16_t cs = cpu.state.CS().value;
    const int32_t room = ctx->host->go_on(ctx->host->owner, ctx->keep_hold_irq != 0);
    ctx->ended = 1;
    if (room < 0 || cpu.state.EIP != eip || cpu.state.CS().value != cs || cpu.state.exception) return kStopRun;
    ctx->ended = 0;
    return go_on_for(ctx, clocks_before, room);
}

extern "C" int32_t kairotowns_jit_poll(Context *ctx, uint32_t clocks, uint32_t instructions, uint32_t last_eip,
                                       uint32_t next_eip, uint32_t mode) {
    auto &cpu = *ctx->cpu;
    ctx->host->advance(ctx->host->owner, clocks - ctx->advanced);
    ctx->advanced = clocks;
    const uint16_t cs = cpu.state.CS().value;
    const uint32_t cs_base = cpu.state.CS().baseLinearAddr;
    const uint64_t effects = *ctx->side_effects;
    const int32_t room = ctx->host->poll(ctx->host->owner, instructions, clocks, last_eip, ctx->keep_hold_irq != 0);
    ctx->ended = 1;
    // Devices may have remapped memory. One that wrote it ends the run, and the next run starts
    // with the code generation stepped.
    ctx->refresh_tlb(ctx->jit);
    // The run goes on only where nothing it relies on changed: no interrupt was taken, nothing
    // wrote memory (its own code among it), and the CPU's modes are as they were.
    if (room < 0 || cpu.state.EIP != next_eip || cpu.state.CS().value != cs || cpu.state.CS().baseLinearAddr != cs_base ||
        mode_of(cpu).key() != mode || cpu.state.exception || cpu.state.halt || *ctx->side_effects != effects)
        return kStopRun;
    ctx->ended = 0;
    return go_on_for(ctx, clocks, room);
}

// Counts the instruction at CS:EIP by opcode, with KAIROTOWNS_JIT_STATS set.
static void count_interpreted(Context *ctx);

// What a Jit::Mark holds, as the CPU and memory are now; the segment registers only where what
// it watches may load one (not I/O).
static Jit::Mark mark_of(const Context *ctx, bool segments = true) {
    const auto &st = ctx->cpu->state;
    Jit::Mark m;
    m.writes = *ctx->writes;
    m.other_writes = *ctx->other_writes;
    m.has_segments = segments;
    if (segments) {
        for (int s = 0; s < 6; ++s) {
            const auto &seg = st.sreg[s];
            m.segments[s][0] = seg.baseLinearAddr;
            m.segments[s][1] = seg.minLimit;
            m.segments[s][2] = seg.maxLimit;
            m.segments[s][3] = seg.minLimitAdjust;
        }
    }
    return m;
}
// What changed since the mark, for blocks' checks: stores into main RAM count in their lines
// as they are made (Memory::kairoLines); one Memory could not place steps the code generation,
// and one into any other object the other generation; a segment register loaded steps the
// segment generation.
static void account(Context *ctx, const Jit::Mark &m) {
    const auto &st = ctx->cpu->state;
    if (*ctx->writes != m.writes) {
        ++ctx->code_generation;
        ++ctx->other_generation;
        ++ctx->steps_interpret;
    }
    if (*ctx->other_writes != m.other_writes) ++ctx->other_generation;
    if (!m.has_segments) return;
    for (int s = 0; s < 6; ++s) {
        const auto &seg = st.sreg[s];
        if (m.segments[s][0] != seg.baseLinearAddr || m.segments[s][1] != seg.minLimit ||
            m.segments[s][2] != seg.maxLimit || m.segments[s][3] != seg.minLimitAdjust) {
            ++ctx->segment_generation;
            return;
        }
    }
}

extern "C" uint32_t kairotowns_jit_interpret(Context *ctx, uint32_t clocks_before, uint32_t eip, uint32_t next_eip,
                                             uint32_t mode) {
    auto &cpu = static_cast<i486DXDefaultFidelity &>(*ctx->cpu);
    count_interpreted(ctx);
    ctx->host->advance(ctx->host->owner, clocks_before - ctx->advanced);
    ctx->advanced = clocks_before;
    const uint16_t cs = cpu.state.CS().value;
    const uint32_t cs_base = cpu.state.CS().baseLinearAddr;
    const Jit::Mark mark = mark_of(ctx);
    const uint32_t clocks = cpu.RunOneInstruction(*ctx->io_memory, *ctx->io);
    if (clocks >= 0x80000000u) throw std::logic_error("an instruction took more clocks than a run counts");
    // Page tables or memory slots may have changed.
    ctx->refresh_tlb(ctx->jit);
    account(ctx, mark);
    const bool stop = cpu.state.EIP != next_eip || cpu.state.CS().value != cs || cpu.state.CS().baseLinearAddr != cs_base ||
                      mode_of(cpu).key() != mode || cpu.state.exception || cpu.state.halt || cpu.state.holdIRQ ||
                      ctx->host->must_stop(ctx->host->owner);
    ctx->keep_hold_irq = cpu.state.holdIRQ ? 1 : 0;
    (void) eip;
    return clocks | (stop ? 0x80000000u : 0);
}

namespace {

// Translated code: a buffer the host can run, filled from the start; when it is full every
// translation is dropped and it starts over.
constexpr size_t kCodeBytes = 64u << 20;
constexpr size_t kTableSize = 1u << 13;

struct Key {
    const uint8_t *code;
    uint32_t eip, mode;
    bool operator==(const Key &o) const { return code == o.code && eip == o.eip && mode == o.mode; }
};
struct KeyHash {
    size_t operator()(const Key &k) const {
        return size_t((reinterpret_cast<uintptr_t>(k.code) ^ uint64_t(k.eip) << 32 ^ k.mode) * 0x9E3779B97F4A7C15ull >> 16);
    }
};

struct Block {
    const uint8_t *code = nullptr;
    uint32_t eip = 0, mode = 0;
    // The guest code the translation covers, compared with the code each time it runs.
    uint32_t bytes = 0;
    std::vector<uint8_t> original;
    const void *entry = nullptr;  // null when the first instruction is not translated
    // Translated reading the segment registers as it runs, not specialized on them: it met other
    // values than it was specialized on.
    bool generic = false;
};

} // namespace

struct Jit::Impl {
    i486DXCommon &cpu;
    Memory &mem;
    InOut &io;
    const MemoryAccess *main_ram;
    std::vector<unsigned char> &ram;
    Translator translator;
    Assembler assembler;
    Stats stats;
    // Instructions the interpreter ran inside runs, by opcode (KAIROTOWNS_JIT_STATS).
    const bool counting = std::getenv("KAIROTOWNS_JIT_STATS") != nullptr;
    std::unordered_map<uint32_t, uint64_t> interpreted_ops;

    uint8_t *buffer = nullptr;
    size_t used = 0, trampolines = 0, installed_bytes = 0;
    void (*enter)(Context *, const void *) = nullptr;
    void *leave = nullptr;
    // Every block since the last flush, and the last block found at each slot of a small table
    // in front of them.
    std::unordered_map<Key, std::unique_ptr<Block>, KeyHash> blocks;
    std::vector<Block *> table = std::vector<Block *>(kTableSize);
    // Where each block keeps the code generation its code check last saw; a deque keeps them
    // where they are as it grows.
    std::deque<Stamp> stamps;
    // Translated code's stores into main RAM by line (Context::code_lines).
    std::vector<uint64_t> code_lines;
    // What the dispatch in translated code finds (Translator::emit_dispatch).
    std::vector<DispatchEntry> dispatch_table = std::vector<DispatchEntry>(size_t(1) << kDispatchBits);
    void *dispatch = nullptr;
    // Translator::emit_stub's routines, by Translator::Stub.
    static constexpr size_t kStubs = size_t(Translator::Stub::TlbMissPaged) + 1;
    void *stubs[kStubs] = {};
    static size_t dispatch_slot(const uint8_t *code, uint32_t eip) {
        return size_t(((reinterpret_cast<uintptr_t>(code) ^ uint64_t(eip) << 20) * 0x9E3779B97F4A7C15ull) >> (64 - kDispatchBits));
    }
    // Translated code's TLB, its generation, and what its entries come from, as they were when
    // they were made.
    std::vector<TlbEntry> tlb = std::vector<TlbEntry>(kTlbEntries);
    uint32_t tlb_generation = 1;
    uint32_t tlb_page_counter = 0;
    uint64_t tlb_map_generation = ~0ull;
    bool tlb_paging = false;
    const unsigned char *tlb_ram = nullptr;

    // The context's fields that stay as they are while the machine does.
    void set_up_context() {
        auto &st = cpu.state;
        ctx.state = &st;
        ctx.access = mem.KairoAccessTable();
        ctx.main_ram = main_ram;
        set_ram();
        ctx.side_effects = &mem.sideEffects;
        ctx.writes = &mem.kairoWrites;
        ctx.other_writes = &mem.kairoOtherWrites;
        ctx.parity = i486DXCommon::ParityTable;
        ctx.page_cache = st.pageTableCache;
        ctx.stack_window = &st.SSESPWindow;
        ctx.code_window = &st.CSEIPWindow;
        ctx.cpu = &cpu;
        ctx.materialize = reinterpret_cast<void *>(&kairotowns_jit_materialize);
        ctx.leave = leave;
        ctx.dispatch = dispatch;
        ctx.dispatch_table = dispatch_table.data();
        ctx.io_helper = reinterpret_cast<void *>(&kairotowns_jit_io);
        ctx.io = &io;
        ctx.host = &host;
        ctx.tlb = tlb.data();
        ctx.interpret_helper = reinterpret_cast<void *>(&kairotowns_jit_interpret);
        ctx.jumped_back_helper = reinterpret_cast<void *>(&kairotowns_jit_jumped_back);
        ctx.exit_stub = stubs[size_t(Translator::Stub::Exit)];
        ctx.interpret_stub = stubs[size_t(Translator::Stub::Interpret)];
        ctx.jumped_back_stub = stubs[size_t(Translator::Stub::JumpedBack)];
        ctx.link_stub = stubs[size_t(Translator::Stub::Link)];
        ctx.compare_stub = stubs[size_t(Translator::Stub::Compare)];
        ctx.poll_stub = stubs[size_t(Translator::Stub::Poll)];
        ctx.poll_helper = reinterpret_cast<void *>(&kairotowns_jit_poll);
        ctx.find_helper = reinterpret_cast<void *>(&kairotowns_jit_find);
        ctx.tlb_miss_flat = stubs[size_t(Translator::Stub::TlbMissFlat)];
        ctx.tlb_miss_paged = stubs[size_t(Translator::Stub::TlbMissPaged)];
        ctx.io_memory = &mem;
        ctx.irq_waiting = irq_waiting;
        ctx.jit = this;
        ctx.refresh_tlb = [](void *jit) { static_cast<Impl *>(jit)->refresh_tlb(); };
    }

    // Main RAM where it is now, and the counts of stores into it by line.
    void set_ram() {
        static_assert(kCodeLineShift == Memory::KAIRO_LINE_SHIFT, "Memory and the JIT count stores by the same lines");
        ctx.ram = ram.data();
        code_lines.resize((ram.size() >> kCodeLineShift) + 1);
        ctx.code_lines = code_lines.data();
        ctx.code_line_count = ram.size() >> kCodeLineShift;
        translator.set_code_lines(ram.data(), ram.size(), code_lines.data());
        // Main RAM's object stores RAM[physical address] (TownsMainRAMAccess).
        mem.kairoRAM = ram.data();
        mem.kairoRAMBytes = ram.size();
        mem.kairoLines = code_lines.data();
    }

    // Drops the TLB when the page-table cache was invalidated, a slot of Memory changed hands,
    // paging was switched or main RAM moved.
    void refresh_tlb() {
        const auto &st = cpu.state;
        if (st.pageTableCacheValidCounter == tlb_page_counter && mem.kairoMapGeneration == tlb_map_generation &&
            cpu.PagingEnabled() == tlb_paging && ram.data() == tlb_ram)
            return;
        // A new generation drops every entry; when the generations run out, they are cleared.
        if (++tlb_generation == 1u << 12) {
            for (auto &entry : tlb) entry = TlbEntry();
            tlb_generation = 1;
        }
        ctx.tlb_tag = tlb_generation << 20;
        set_ram();
        tlb_page_counter = st.pageTableCacheValidCounter;
        tlb_map_generation = mem.kairoMapGeneration;
        tlb_paging = cpu.PagingEnabled();
        tlb_ram = ram.data();
        ++stats.tlb_flushes;
    }
    Context ctx;

    Host host;
    const unsigned char *irq_waiting = nullptr;

    Impl(i486DXCommon &cpu, Memory &mem, InOut &io, const MemoryAccess *main_ram, std::vector<unsigned char> &ram,
         const Host &host)
        : cpu(cpu), mem(mem), io(io), main_ram(main_ram), ram(ram), translator(cpu, mem), host(host) {
#ifdef KAIROTOWNS_JIT_RUNS
        void *map = mmap(nullptr, kCodeBytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED) throw std::runtime_error(std::string("JIT code buffer: mmap failed: ") + std::strerror(errno));
        buffer = static_cast<uint8_t *>(map);
        Translator::emit_enter(assembler, cpu);
        enter = reinterpret_cast<void (*)(Context *, const void *)>(install());
        Translator::emit_leave(assembler, cpu);
        leave = install();
        Translator::emit_dispatch(assembler, cpu);
        dispatch = install();
        for (size_t i = 0; i < kStubs; ++i) {
            translator.emit_stub(assembler, Translator::Stub(i));
            stubs[i] = install();
        }
        trampolines = used;
        set_up_context();
#else
        throw std::logic_error("the JIT runs only on AArch64");
#endif
    }
    ~Impl() {
        if (std::getenv("KAIROTOWNS_JIT_STATS")) {
            std::fprintf(stderr, "jit: %llu runs, %llu instructions, %llu TLB flushes, %llu translations in %.1f ms, %llu bytes, "
                                 "%llu blocks found in a run; runs ended by",
                         (unsigned long long) stats.runs, (unsigned long long) stats.instructions,
                         (unsigned long long) stats.tlb_flushes, (unsigned long long) stats.translations,
                         stats.translate_ns / 1e6, (unsigned long long) stats.code_bytes, (unsigned long long) stats.found);
            for (uint32_t i = 0; i < WHY_COUNT; ++i)
                std::fprintf(stderr, " %s %llu", exit_why_name(i), (unsigned long long) stats.exits[i]);
            // Where the shared code is, to read a profile of the code buffer by.
            std::fprintf(stderr, "\njit: code buffer at %p: enter +0 leave +%zx dispatch +%zx stubs", static_cast<void *>(buffer),
                         static_cast<size_t>(static_cast<uint8_t *>(leave) - buffer),
                         static_cast<size_t>(static_cast<uint8_t *>(dispatch) - buffer));
            for (size_t i = 0; i < kStubs; ++i)
                std::fprintf(stderr, " +%zx", static_cast<size_t>(static_cast<uint8_t *>(stubs[i]) - buffer));
            std::fprintf(stderr, ", blocks from +%zx\n", trampolines);
            std::fprintf(stderr, "jit: code checks %llu for the generation, %llu for lines written, %llu off main RAM; generation steps %llu by stores, %llu by interpreted instructions\n",
                         (unsigned long long) ctx.checks_generation, (unsigned long long) ctx.checks_lines,
                         (unsigned long long) ctx.checks_other,
                         (unsigned long long) ctx.steps_store, (unsigned long long) ctx.steps_interpret);
            std::vector<std::pair<uint32_t, uint64_t>> ops(interpreted_ops.begin(), interpreted_ops.end());
            std::sort(ops.begin(), ops.end(), [](const auto &x, const auto &y) { return x.second > y.second; });
            std::fprintf(stderr, "jit: interpreted in runs, by opcode (prefixed +10000):");
            for (size_t i = 0; i < ops.size() && i < 24; ++i)
                std::fprintf(stderr, " %x:%llu", ops[i].first, (unsigned long long) ops[i].second);
            std::fprintf(stderr, "\n");
        }
#ifdef KAIROTOWNS_JIT_RUNS
        if (buffer) munmap(buffer, kCodeBytes);
#endif
    }

    // Copies the assembled code into the buffer; null when it does not fit.
    // With KAIROTOWNS_JIT_DUMP set to a folder, every translation is written there as
    // <EIP>-<n>.bin (its host code, for llvm-objdump) and <EIP>-<n>.x86 (the guest code).
    const char *dump = std::getenv("KAIROTOWNS_JIT_DUMP");
    uint64_t dumped = 0;
    void dump_block(const void *host, size_t bytes, const uint8_t *guest, uint32_t guest_bytes, uint32_t eip) {
        const std::string name = std::string(dump) + "/" + std::to_string(eip) + "-" + std::to_string(dumped++);
        if (FILE *f = std::fopen((name + ".bin").c_str(), "wb")) {
            std::fwrite(host, 1, bytes, f);
            std::fclose(f);
        }
        if (FILE *f = std::fopen((name + ".x86").c_str(), "wb")) {
            std::fwrite(guest, 1, guest_bytes, f);
            std::fclose(f);
        }
        // Where each translation is in the code buffer, to read a profile of it by: host address,
        // bytes, file.
        if (FILE *f = std::fopen((std::string(dump) + "/map.txt").c_str(), "a")) {
            std::fprintf(f, "%llx %zu %s\n", (unsigned long long) reinterpret_cast<uintptr_t>(host), bytes, name.c_str());
            std::fclose(f);
        }
    }

    // entry: where the code is entered, in bytes from its start.
    void *install(size_t entry = 0) {
        const size_t bytes = assembler.size();
        if (used + bytes > kCodeBytes) {
            assembler.clear();
            return nullptr;
        }
        uint8_t *at = buffer + used;
        std::memcpy(at, assembler.code(), bytes);
        installed_bytes = bytes;
        stats.code_bytes += bytes;
        __builtin___clear_cache(reinterpret_cast<char *>(at), reinterpret_cast<char *>(at + bytes));
        used += (bytes + 15) & ~size_t(15);
        assembler.clear();
        return at + entry;
    }
    void flush() {
        blocks.clear();
        stamps.clear();
        for (auto &block : table) block = nullptr;
        for (auto &entry : dispatch_table) entry = DispatchEntry();
        used = trampolines;
        ++stats.flushes;
    }

    static size_t slot(const uint8_t *code, uint32_t eip) {
        const uint64_t h = (reinterpret_cast<uintptr_t>(code) ^ uint64_t(eip) << 20) * 0x9E3779B97F4A7C15ull;
        return size_t(h >> 48) & (kTableSize - 1);
    }

    // A new translation of the code; it may flush the code buffer, and every block with it, to
    // make room.
    Block translate(const uint8_t *code, uint32_t avail, uint32_t eip, const Mode &mode, bool generic) {
        // A block's translation checks the code at its entry, so only a refused block keeps its
        // code here.
        const auto started = std::chrono::steady_clock::now();
        struct Timed {
            Stats &stats;
            std::chrono::steady_clock::time_point started;
            ~Timed() {
                stats.translate_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count());
            }
        } timed{stats, started};
        Block block;
        block.code = code;
        block.eip = eip;
        block.mode = mode.key();
        block.generic = generic;
        Stamp *stamp = &stamps.emplace_back();
        Translator::Result result = translator.translate(assembler, code, avail, eip, mode, stamp, !generic);
        ++stats.translations;
        if (result.instructions == 0) {
            ++stats.refused;
            assembler.clear();
            // Its first instruction, at most MAX_INSTRUCTION_LENGTH bytes, decides that.
            block.bytes = std::min<uint32_t>(avail, i486DXCommon::MAX_INSTRUCTION_LENGTH);
        } else {
            block.bytes = result.bytes;
            block.entry = install(result.entry);
            if (!block.entry) {
                flush();
                block = Block();
                block.code = code;
                block.eip = eip;
                block.mode = mode.key();
                block.generic = generic;
                stamp = &stamps.emplace_back();
                result = translator.translate(assembler, code, avail, eip, mode, stamp, !generic);
                block.bytes = result.bytes;
                block.entry = install(result.entry);
                if (!block.entry) throw std::logic_error("a block does not fit an empty JIT code buffer");
            }
        }
        if (!block.entry) block.original.assign(code, code + block.bytes);
        else if (dump) dump_block(static_cast<const uint8_t *>(block.entry) - result.entry, installed_bytes, code, block.bytes, eip);
        return block;
    }

    // The block for the code at CS:EIP now, translated when the table has none for this code;
    // null where the interpreter runs the next instruction.
    // move_window: at the start of a run, where the interpreter's next step is that fetch.
    // generic: translate it reading the segment registers as it runs.
    Block *current(bool retranslate, bool move_window, bool generic = false) {
        auto &st = cpu.state;
        // The interpreter fetches through its code window, moving it first where the code is out
        // of it (FetchInstructionClass::FetchInstructionRef); that is done here as it does it.
        // Where that faults, the interpreter runs the instruction.
        const Mode mode = mode_of(cpu);
        const uint32_t lin = st.CS().baseLinearAddr + st.EIP;
        if (!st.CSEIPWindow.IsLinearAddressInRange(lin)) {
            if (!move_window) return nullptr;
            st.CSEIPWindow = static_cast<i486DXDefaultFidelity &>(cpu).GetConstMemoryWindow(mode.address_size, st.CS(), st.EIP, mem);
            if (st.exception || !st.CSEIPWindow.IsLinearAddressInRange(lin)) return nullptr;
        }
        const MemoryAccess::ConstPointer code = st.CSEIPWindow.GetReadAccessPointer(lin);
        if (code.ptr == nullptr || code.length == 0) return nullptr;
        Block *&cached = table[slot(code.ptr, st.EIP)];
        Block *block = cached;
        if (!block || block->code != code.ptr || block->eip != st.EIP || block->mode != mode.key()) {
            auto &owned = blocks[Key{code.ptr, st.EIP, mode.key()}];
            if (!owned) {
                owned = std::make_unique<Block>();
                retranslate = true;
            }
            block = owned.get();
        }
        // A translation checks its code at its entry; a refused one is checked here.
        if (retranslate || (!block->entry && std::memcmp(code.ptr, block->original.data(), block->bytes) != 0)) {
            const uint64_t flushes = stats.flushes;
            Block made = translate(code.ptr, code.length, st.EIP, mode, generic || block->generic);
            // A flush to make room dropped every block, this one's place too.
            if (stats.flushes != flushes) {
                auto placed = std::make_unique<Block>();
                block = placed.get();
                blocks[Key{code.ptr, st.EIP, mode.key()}] = std::move(placed);
            }
            *block = std::move(made);
        }
        table[slot(code.ptr, st.EIP)] = block;
        if (!block->entry) return nullptr;
        DispatchEntry &found = dispatch_table[dispatch_slot(code.ptr, st.EIP)];
        found.code = code.ptr;
        found.eip = st.EIP;
        found.mode = mode.key();
        found.entry = block->entry;
        return block;
    }

    // kairotowns_jit_find.
    const void *find() {
        const uint64_t flushes = stats.flushes;
        Block *block = current(false, true);
        if (!block || stats.flushes != flushes) return nullptr;
        ++stats.found;
        return block->entry;
    }

    // Points the branch at site at entry.
    void patch(void *site, const void *entry) {
        auto *word = static_cast<uint32_t *>(site);
        const int64_t distance = (static_cast<const uint8_t *>(entry) - static_cast<const uint8_t *>(site)) / 4;
        if (distance < -(int64_t(1) << 25) || distance >= (int64_t(1) << 25)) throw std::logic_error("chain out of reach");
        *word = 0x14000000u | (uint32_t(distance) & 0x3FFFFFF);
        __builtin___clear_cache(reinterpret_cast<char *>(word), reinterpret_cast<char *>(word + 1));
        ++stats.links;
    }

    Run run(int32_t clock_limit) {
        auto &st = cpu.state;
        // The interpreter's own debugging hooks and call stack run per instruction.
        if (st.halt || st.exception || cpu.debuggerPtr != nullptr || cpu.enableCallStack) return {};
        Block *block = current(false, true);
        if (!block) return {};
        refresh_tlb();

        ctx.keep_hold_irq = 0;
        ctx.advanced = 0;
        ctx.ended = 0;
        // The interpreter clears it as each instruction starts (RunOneInstruction); at least one
        // starts now, translated or not.
        st.holdIRQ = false;
        ctx.clock_limit = clock_limit;
        ctx.chained_instructions = 0;
        ctx.came_from = nullptr;
        ctx.link_site = nullptr;
        enter(&ctx, block->entry);

        ++stats.runs;
        stats.instructions += ctx.instructions;
        ++stats.exits[ctx.why < WHY_COUNT ? ctx.why : 0];
        Run result;
        result.instructions = ctx.instructions;
        result.clocks = ctx.clocks;
        result.last_eip = ctx.last_eip;
        result.advanced = ctx.advanced;
        result.keep_hold_irq = ctx.keep_hold_irq != 0;
        result.interpret_next = ctx.reason == REASON_INTERPRET;
        result.ended = ctx.ended != 0;

        // The run stopped at CS:EIP; there the dispatcher translates the code again where it
        // changed, and links a block to its successor where the block asked for it. A flush of
        // the code buffer in between drops both branches.
        const uint64_t flushes = stats.flushes;
        if (ctx.reason == REASON_MISMATCH || ctx.reason == REASON_SEGMENTS) {
            ++stats.mismatches;
            void *came_from = ctx.came_from;
            Block *again = current(true, false, ctx.reason == REASON_SEGMENTS);
            if (again && came_from && stats.flushes == flushes) patch(came_from, again->entry);
        } else if (ctx.link_site) {
            void *site = ctx.link_site;
            Block *next = current(false, false);
            if (next && stats.flushes == flushes) patch(site, next->entry);
        }
        return result;
    }
};

static void count_interpreted(Context *ctx) {
    auto &impl = *static_cast<Jit::Impl *>(ctx->jit);
    if (!impl.counting) return;
    i486DXCommon::InstructionAndOperand decoded;
    MemoryAccess::ConstMemoryWindow window;
    impl.cpu.DebugFetchInstruction(window, decoded, impl.mem);
    ++impl.interpreted_ops[decoded.inst.RealOpCode() | (decoded.inst.instPrefix != 0 ? 0x10000u : 0u)];
}

extern "C" const void *kairotowns_jit_find(Context *ctx, uint32_t eip) {
    if (ctx->cpu->state.EIP != eip) throw std::logic_error("kairotowns_jit_find: EIP not stored");
    return static_cast<Jit::Impl *>(ctx->jit)->find();
}

Jit::Jit(i486DXCommon &cpu, Memory &mem, InOut &io, const MemoryAccess *main_ram, std::vector<unsigned char> &ram,
         const Host &host, const unsigned char *irq_waiting)
    : impl_(std::make_unique<Impl>(cpu, mem, io, main_ram, ram, host)) {
    impl_->irq_waiting = irq_waiting;
    impl_->ctx.irq_waiting = irq_waiting;
}
Jit::~Jit() = default;
Jit::Run Jit::run(int32_t clock_limit) { return impl_->run(clock_limit); }
Jit::Mark Jit::mark() const { return mark_of(&impl_->ctx); }
void Jit::after(const Mark &mark) { account(&impl_->ctx, mark); }
void Jit::changed() {
    ++impl_->ctx.code_generation;
    ++impl_->ctx.other_generation;
    ++impl_->ctx.segment_generation;
}
const Jit::Stats &Jit::stats() const { return impl_->stats; }

} // namespace kairotowns::jit
