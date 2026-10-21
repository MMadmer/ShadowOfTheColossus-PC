// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: the BMI1/LZCNT/MOVBE emulation of the CPU patches (gpu/sotc/cpu_patches.cpp) against the
// real instructions, on a CPU that has them: each form runs natively and emulated on the same
// random registers, memory and flags, and the registers, memory and defined flags must match.
// Then the patch passes themselves on a small module (TestPatchPasses).
//   ninja -C out/gpu cpu-emulation-test && out/gpu/cpu-emulation-test.exe
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <vector>
#include <xbyak/xbyak.h>
#include "cpu_patches.h"
#ifdef _WIN32
#include <windows.h>
#include "sotc_patches.h"
#endif

using namespace Xbyak::util;

namespace {

struct State {
    u64 rax, rbx, rcx, rdx, rsi;
    u64 flags;
    u64 memory; // [rsi]
    u64 stack;  // [rsp + 8]
};

constexpr u64 CF = 1, PF = 4, ZF = 0x40, SF = 0x80, OF = 0x800;
constexpr u64 Arithmetic = CF | PF | 0x10 | ZF | SF | OF;

using Emit = std::function<void(Xbyak::CodeGenerator&)>;

// void run(State*): loads the state (rsi points at State::memory), runs `body`, stores it back.
struct Runner : Xbyak::CodeGenerator {
    explicit Runner(const Emit& body) : Xbyak::CodeGenerator(4096) {
        const auto state = r12;
        push(rbx);
        push(rsi);
        push(rdi);
        push(r12);
        mov(state, rcx); // Windows x64: first argument
        sub(rsp, 64);
        mov(rax, ptr[state + offsetof(State, stack)]);
        mov(ptr[rsp + 8], rax);
        mov(rax, ptr[state + offsetof(State, rax)]);
        mov(rbx, ptr[state + offsetof(State, rbx)]);
        mov(rcx, ptr[state + offsetof(State, rcx)]);
        mov(rdx, ptr[state + offsetof(State, rdx)]);
        lea(rsi, ptr[state + offsetof(State, memory)]);
        push(qword[state + offsetof(State, flags)]);
        popfq();
        body(*this);
        pushfq();
        pop(qword[state + offsetof(State, flags)]);
        mov(ptr[state + offsetof(State, rax)], rax);
        mov(ptr[state + offsetof(State, rbx)], rbx);
        mov(ptr[state + offsetof(State, rcx)], rcx);
        mov(ptr[state + offsetof(State, rdx)], rdx);
        mov(ptr[state + offsetof(State, rsi)], rsi);
        mov(rax, ptr[rsp + 8]);
        mov(ptr[state + offsetof(State, stack)], rax);
        add(rsp, 64);
        pop(r12);
        pop(rdi);
        pop(rsi);
        pop(rbx);
        ret();
        ready();
    }
};

struct Case {
    const char* name;
    Emit native;
    u64 flags; // flags the instruction defines (or keeps)
};

u64 Value(std::mt19937_64& random) {
    switch (random() % 6) {
    case 0:
        return 0;
    case 1:
        return u64{1} << (random() % 64);
    case 2:
        return ~u64{0};
    case 3:
        return random() & 0xFFFFFFFF;
    default:
        return random();
    }
}

#ifdef _WIN32
using Function1 = u64 (*)(u64);
using Function2 = u64 (*)(u64, u64);

LONG CALLBACK IllegalInstruction(EXCEPTION_POINTERS* info) {
    return info->ExceptionRecord->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION &&
                   sotc_handle_illegal(info)
               ? EXCEPTION_CONTINUE_EXECUTION
               : EXCEPTION_CONTINUE_SEARCH;
}

/// The loader's illegal instruction path, as if the instruction at `code` had faulted.
bool DispatchIllegal(const void* code) {
    CONTEXT context{};
    context.Rip = reinterpret_cast<DWORD64>(code);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
    record.ExceptionAddress = const_cast<void*>(code);
    EXCEPTION_POINTERS pointers{&record, &context};
    return sotc_handle_illegal(&pointers) != 0;
}

/// The whole range [address, address + size) is read-execute.
bool IsReadExecute(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(address, &info, sizeof(info)) && info.Protect == PAGE_EXECUTE_READ &&
           static_cast<const u8*>(info.BaseAddress) + info.RegionSize >=
               static_cast<const u8*>(address) + size;
}

/// The patch passes with BMI emulation on, in a module laid out like the loader's (code, then
/// its trampolines, both read-execute after loading):
///  - a short TZCNT whose next instruction is a branch target, in a leaf whose red zone is live
///    there: reached through a relay jump, never a UD2 (the exception would clobber the red zone);
///  - the same where the red zone is not live (a call follows): a UD2 trap, the rest int3;
///  - an ANDN patched at run time from the illegal instruction path, which writes into the
///    read-execute code and trampoline area; an ANDN outside the module is left alone.
int TestPatchPasses() {
    constexpr size_t CodeSize = 0x10000;
    constexpr size_t TrampolineSize = 0x10000;
    constexpr size_t OutsideSize = 0x1000;
    auto* base = static_cast<u8*>(VirtualAlloc(nullptr, CodeSize + TrampolineSize + OutsideSize,
                                               MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!base) {
        std::printf("patch passes: no memory\n");
        return 1;
    }
    u8* trampolines = base + CodeSize;
    u8* outside = trampolines + TrampolineSize;

    Xbyak::CodeGenerator c(CodeSize, base);
    const u8* relay_function = c.getCurr();
    c.mov(ptr[rsp - 8], rcx); // the red zone holds the argument across the tzcnt
    c.xor_(r8d, r8d);
    const u8* relay_site = c.getCurr();
    c.tzcnt(eax, ecx);
    Xbyak::Label loop;
    c.L(loop);
    c.add(rax, ptr[rsp - 8]);
    c.inc(r8d);
    c.cmp(r8d, 2);
    c.jb(loop);
    c.ret();
    c.align(16);
    const u8* trap_function = c.getCurr();
    Xbyak::Label callee;
    c.tzcnt(eax, ecx);
    c.call(callee);
    c.ret();
    c.align(16);
    const u8* callee_function = c.getCurr();
    c.L(callee);
    c.ret();
    c.align(16);
    const size_t segment_size = c.getSize(); // the load-time passes stop here
    const u8* jit_function = c.getCurr();
    c.andn(rax, rcx, rdx);
    c.ret();
    std::memcpy(outside, jit_function, c.getCurr() - jit_function);

    const auto relay = reinterpret_cast<Function1>(const_cast<u8*>(relay_function));
    const auto trap = reinterpret_cast<Function1>(const_cast<u8*>(trap_function));
    const auto jit = reinterpret_cast<Function2>(const_cast<u8*>(jit_function));
    std::mt19937_64 random(54321);
    std::vector<std::array<u64, 5>> expected; // x, y, relay(x), trap(x), jit(x, y)
    for (int i = 0; i < 4096; ++i) {
        const u64 x = i == 0 ? 0 : Value(random);
        const u64 y = Value(random);
        expected.push_back({x, y, relay(x), trap(x), jit(x, y)});
    }

    int failures = 0;
    const auto check = [&failures](bool ok, const char* what) {
        std::printf("patch passes: %-58s %s\n", what, ok ? "ok" : "FAIL");
        failures += !ok;
    };
    Core::RegisterPatchModule(base, CodeSize, trampolines, TrampolineSize);
    const uintptr_t starts[] = {reinterpret_cast<uintptr_t>(relay_function),
                                reinterpret_cast<uintptr_t>(trap_function),
                                reinterpret_cast<uintptr_t>(callee_function)};
    Core::PatchShortSse4aInstructions(reinterpret_cast<u64>(base), segment_size, starts);
    check(relay_site[0] == 0xEB, "red zone live: short jump to a relay, no UD2");
    check(trap_function[0] == 0x0F && trap_function[1] == 0x0B && trap_function[2] == 0xCC &&
              trap_function[3] == 0xCC,
          "red zone not live: UD2 trap, the rest int3");

    DWORD old_protect = 0;
    VirtualProtect(base, CodeSize + TrampolineSize, PAGE_EXECUTE_READ, &old_protect);
    void* handler = AddVectoredExceptionHandler(1, IllegalInstruction);
    const bool jit_patched = DispatchIllegal(jit_function);
    check(jit_patched && jit_function[0] == 0xE9, "run-time patch into read-execute code");
    check(IsReadExecute(base, CodeSize) && IsReadExecute(trampolines, TrampolineSize),
          "code and trampolines read-execute again");
    check(!DispatchIllegal(outside) && outside[0] == 0xC4, "no patch outside the module");

    bool same = true;
    for (const auto& [x, y, relay_value, trap_value, jit_value] : expected) {
        same &= relay(x) == relay_value && trap(x) == trap_value && jit(x, y) == jit_value;
    }
    check(same, "patched code gives the native results");
    RemoveVectoredExceptionHandler(handler);
    return failures;
}
#endif

} // namespace

int main() {
#ifdef _WIN32
    // The patch passes emulate BMI1/LZCNT/MOVBE whatever the CPU (read once, before any use).
    _putenv_s("SOTC_EMULATE_BMI", "1");
#endif
    const std::vector<Case> cases = {
        {"tzcnt eax, ecx", [](auto& c) { c.tzcnt(eax, ecx); }, CF | ZF},
        {"tzcnt rax, rcx", [](auto& c) { c.tzcnt(rax, rcx); }, CF | ZF},
        {"tzcnt rcx, rcx", [](auto& c) { c.tzcnt(rcx, rcx); }, CF | ZF},
        {"tzcnt eax, [rsi]", [](auto& c) { c.tzcnt(eax, dword[rsi]); }, CF | ZF},
        {"tzcnt rax, [rsp+8]", [](auto& c) { c.tzcnt(rax, qword[rsp + 8]); }, CF | ZF},
        {"lzcnt eax, ecx", [](auto& c) { c.lzcnt(eax, ecx); }, CF | ZF},
        {"lzcnt rdx, rdx", [](auto& c) { c.lzcnt(rdx, rdx); }, CF | ZF},
        {"lzcnt rax, [rsp+8]", [](auto& c) { c.lzcnt(rax, qword[rsp + 8]); }, CF | ZF},
        {"andn eax, ebx, ecx", [](auto& c) { c.andn(eax, ebx, ecx); }, CF | ZF | SF | OF},
        {"andn rax, rbx, rcx", [](auto& c) { c.andn(rax, rbx, rcx); }, CF | ZF | SF | OF},
        {"andn rax, rax, rbx", [](auto& c) { c.andn(rax, rax, rbx); }, CF | ZF | SF | OF},
        {"andn rax, rbx, rax", [](auto& c) { c.andn(rax, rbx, rax); }, CF | ZF | SF | OF},
        {"andn rsi, rbx, [rsi]", [](auto& c) { c.andn(rsi, rbx, qword[rsi]); }, CF | ZF | SF | OF},
        {"andn eax, ebx, [rsp+8]", [](auto& c) { c.andn(eax, ebx, dword[rsp + 8]); },
         CF | ZF | SF | OF},
        {"bextr eax, ebx, edx", [](auto& c) { c.bextr(eax, ebx, edx); }, CF | ZF | OF},
        {"bextr rax, rbx, rdx", [](auto& c) { c.bextr(rax, rbx, rdx); }, CF | ZF | OF},
        {"bextr rcx, rbx, rdx", [](auto& c) { c.bextr(rcx, rbx, rdx); }, CF | ZF | OF},
        {"bextr rax, rcx, rdx", [](auto& c) { c.bextr(rax, rcx, rdx); }, CF | ZF | OF},
        {"bextr rax, rbx, rcx", [](auto& c) { c.bextr(rax, rbx, rcx); }, CF | ZF | OF},
        {"bextr eax, [rsi], edx", [](auto& c) { c.bextr(eax, dword[rsi], edx); }, CF | ZF | OF},
        {"bextr rcx, [rsp+8], rcx", [](auto& c) { c.bextr(rcx, qword[rsp + 8], rcx); },
         CF | ZF | OF},
        {"blsi eax, ecx", [](auto& c) { c.blsi(eax, ecx); }, CF | ZF | SF | OF},
        {"blsi rax, rax", [](auto& c) { c.blsi(rax, rax); }, CF | ZF | SF | OF},
        {"blsi rax, [rsp+8]", [](auto& c) { c.blsi(rax, qword[rsp + 8]); }, CF | ZF | SF | OF},
        {"blsmsk eax, ecx", [](auto& c) { c.blsmsk(eax, ecx); }, CF | ZF | SF | OF},
        {"blsmsk rax, rax", [](auto& c) { c.blsmsk(rax, rax); }, CF | ZF | SF | OF},
        {"blsmsk rax, [rsi]", [](auto& c) { c.blsmsk(rax, qword[rsi]); }, CF | ZF | SF | OF},
        {"blsr eax, ecx", [](auto& c) { c.blsr(eax, ecx); }, CF | ZF | SF | OF},
        {"blsr rcx, rcx", [](auto& c) { c.blsr(rcx, rcx); }, CF | ZF | SF | OF},
        {"blsr rax, [rsi]", [](auto& c) { c.blsr(rax, qword[rsi]); }, CF | ZF | SF | OF},
        {"movbe eax, [rsi]", [](auto& c) { c.movbe(eax, dword[rsi]); }, Arithmetic},
        {"movbe rax, [rsi]", [](auto& c) { c.movbe(rax, qword[rsi]); }, Arithmetic},
        {"movbe cx, [rsi]", [](auto& c) { c.movbe(cx, word[rsi]); }, Arithmetic},
        {"movbe rsi, [rsi]", [](auto& c) { c.movbe(rsi, qword[rsi]); }, Arithmetic},
        {"movbe rax, [rsp+8]", [](auto& c) { c.movbe(rax, qword[rsp + 8]); }, Arithmetic},
        {"movbe [rsi], ebx", [](auto& c) { c.movbe(dword[rsi], ebx); }, Arithmetic},
        {"movbe [rsi], rbx", [](auto& c) { c.movbe(qword[rsi], rbx); }, Arithmetic},
        {"movbe [rsi], dx", [](auto& c) { c.movbe(word[rsi], dx); }, Arithmetic},
        {"movbe [rsp+8], rcx", [](auto& c) { c.movbe(qword[rsp + 8], rcx); }, Arithmetic},
    };

    std::mt19937_64 random(12345);
    int failures = 0;
    for (const auto& test : cases) {
        Xbyak::CodeGenerator instruction(64);
        test.native(instruction);
        void* bytes = const_cast<u8*>(instruction.getCode());
        Runner native(test.native);
        bool emitted = true;
        Runner emulated([&](Xbyak::CodeGenerator& c) {
            emitted = Core::EmitCpuEmulationForTest(bytes, c);
        });
        if (!emitted) {
            std::printf("%-24s no emulation\n", test.name);
            ++failures;
            continue;
        }
        int mismatches = 0;
        for (int i = 0; i < 200000 && mismatches < 3; ++i) {
            State in{Value(random), Value(random), Value(random), Value(random), 0,
                     0x202 | (random() & Arithmetic), Value(random), Value(random)};
            if (random() % 2) { // BEXTR controls within or just past the operand
                in.rcx = (in.rcx & ~u64{0xFFFF}) | (random() % 72) | (random() % 72) << 8;
                in.rdx = (in.rdx & ~u64{0xFFFF}) | (random() % 72) | (random() % 72) << 8;
            }
            State a = in, b = in;
            native.getCode<void (*)(State*)>()(&a);
            emulated.getCode<void (*)(State*)>()(&b);
            for (State* s : {&a, &b}) { // rsi still pointing at its own State::memory
                if (s->rsi == reinterpret_cast<u64>(&s->memory)) {
                    s->rsi = 0;
                }
            }
            const bool same = a.rax == b.rax && a.rbx == b.rbx && a.rcx == b.rcx &&
                              a.rdx == b.rdx && a.rsi == b.rsi && a.memory == b.memory &&
                              a.stack == b.stack && (a.flags & test.flags) == (b.flags & test.flags);
            if (!same) {
                ++mismatches;
                std::printf("%-24s MISMATCH in rax=%llx rbx=%llx rcx=%llx rdx=%llx mem=%llx "
                            "stack=%llx flags=%llx\n  native   rax=%llx rbx=%llx rcx=%llx rdx=%llx "
                            "mem=%llx stack=%llx flags=%llx\n  emulated rax=%llx rbx=%llx "
                            "rcx=%llx rdx=%llx mem=%llx stack=%llx flags=%llx\n",
                            test.name, in.rax, in.rbx, in.rcx, in.rdx, in.memory, in.stack,
                            in.flags, a.rax, a.rbx, a.rcx, a.rdx, a.memory, a.stack,
                            a.flags & test.flags, b.rax, b.rbx, b.rcx, b.rdx, b.memory, b.stack,
                            b.flags & test.flags);
            }
        }
        std::printf("%-24s %s (%zu bytes emulated)\n", test.name, mismatches ? "FAIL" : "ok",
                    emulated.getSize());
        failures += mismatches != 0;
    }
#ifdef _WIN32
    failures += TestPatchPasses();
#endif
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
