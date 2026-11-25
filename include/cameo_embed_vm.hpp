#pragma once
/** @file cameo_embed_vm.hpp
 * @brief Freestanding, zero-heap, zero-exceptions, zero-RTTI C++20 static VM for Cameo.Embed.
 *
 * Designed for boot-static hard-real-time execution and live hardware debugging on NexRT
 * (jeffadickson/rtos) and bare-metal RV32IM / ARM Cortex-M3 targets:
 *   - Zero dynamic allocation (no malloc/free/new/delete)
 *   - Zero C++ exceptions, zero RTTI, zero runtime static constructors (100% .bss)
 *   - Integer-only by default (CAMEO_EMBED_ENABLE_FLOAT=0, zero libm/soft-float footprint)
 *   - First-class Ptr values with arithmetic, alignment checks, and width-checked access
 *   - Explicit axi.* MMIO / AXI4-Lite / AXI4 burst API backed by a board MemoryWindow policy
 *   - Built-in dbg.* hardware debugging helpers (bitfields, hexdump, log_hex, assert, trace)
 *   - Fixed static records and integer-indexed arrays (no dynamic hash dictionaries, no units)
 *   - CRC-32C (Castagnoli 0x82F63B78) CEMB v1 verification for Flash .rodata and UART hot-loading
 *   - Hard deterministic instruction step budget + external abort flag (including inside axi.poll32)
 */

#include <stddef.h>
#include <stdint.h>

#ifndef CAMEO_EMBED_ENABLE_FLOAT
#define CAMEO_EMBED_ENABLE_FLOAT 0
#endif

#ifndef CAMEO_EMBED_ENABLE_MATH
#define CAMEO_EMBED_ENABLE_MATH 0
#endif

#ifndef CAMEO_EMBED_COMPACT_32
#define CAMEO_EMBED_COMPACT_32 0
#endif

#if CAMEO_EMBED_ENABLE_MATH && !CAMEO_EMBED_ENABLE_FLOAT
#error "CAMEO_EMBED_ENABLE_MATH=1 requires CAMEO_EMBED_ENABLE_FLOAT=1"
#endif

#if CAMEO_EMBED_COMPACT_32 && CAMEO_EMBED_ENABLE_FLOAT
#error "CAMEO_EMBED_COMPACT_32=1 requires CAMEO_EMBED_ENABLE_FLOAT=0"
#endif

#if (!defined(__STDC_HOSTED__) || (__STDC_HOSTED__ != 0)) && defined(__has_include) && __has_include(<span>) && __has_include(<string_view>)
#include <span>
#include <string_view>
#define CAMEO_EMBED_HAS_STD_VIEWS 1
#else
#define CAMEO_EMBED_HAS_STD_VIEWS 0
#endif

#if CAMEO_EMBED_ENABLE_FLOAT
#include <bit>
#include <limits>
#endif

#if CAMEO_EMBED_ENABLE_MATH
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0)
extern "C" {
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan2(double, double);
double sinh(double);
double cosh(double);
double tanh(double);
double asinh(double);
double acosh(double);
double atanh(double);
double exp(double);
double pow(double, double);
double expm1(double);
double log(double);
double log2(double);
double log10(double);
double log1p(double);
double sqrt(double);
double cbrt(double);
double floor(double);
double ceil(double);
double fmod(double, double);
}
#else
#include <math.h>
#endif
#endif

namespace cameo::embed {

#if CAMEO_EMBED_HAS_STD_VIEWS
template<typename T>
using Span = std::span<T>;
using StringView = std::string_view;
#else
/// Minimal zero-dependency span fallback for freestanding toolchains without libstdc++ (e.g. bare-metal RV32IM).
template<typename T>
class Span {
    T* data_{nullptr};
    size_t size_{0};
public:
    constexpr Span() noexcept = default;
    constexpr Span(T* d, size_t n) noexcept : data_(d), size_(n) {}
    template<size_t N>
    constexpr Span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}
    template<size_t N>
    constexpr Span(const T (&arr)[N]) noexcept requires (requires(const T* p) { data_ = p; })
        : data_(arr), size_(N) {}
    [[nodiscard]] constexpr T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0u; }
    [[nodiscard]] constexpr T& operator[](size_t i) const noexcept { return data_[i]; }
};

/// Minimal zero-dependency string_view fallback for freestanding toolchains without libstdc++.
class StringView {
    const char* data_{nullptr};
    size_t size_{0};
public:
    constexpr StringView() noexcept = default;
    constexpr StringView(const char* d, size_t n) noexcept : data_(d), size_(n) {}
    constexpr StringView(const char* s) noexcept : data_(s), size_(0) {
        if (s != nullptr) {
            while (s[size_] != '\0') ++size_;
        }
    }
    [[nodiscard]] constexpr const char* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0u; }
    [[nodiscard]] constexpr char operator[](size_t i) const noexcept { return data_[i]; }
    [[nodiscard]] constexpr const char* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr const char* end() const noexcept { return data_ + size_; }
};
#endif

inline void memory_barrier() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __asm__ volatile("" ::: "memory");
#endif
}

inline constexpr uint32_t kMagic = 0x424D4543u; // "CEMB" LE
inline constexpr uint16_t kVersion = 1u;
inline constexpr size_t kHeaderSize = 80u;
inline constexpr size_t kProtoDescSize = 24u;
inline constexpr size_t kConstDescSize = 16u;
inline constexpr size_t kInstrSize = 8u;
inline constexpr size_t kUpvalDescSize = 4u;
inline constexpr size_t kGlobalDescSize = 4u;
inline constexpr size_t kHostImportDescSize = 8u;
inline constexpr size_t kStringDescSize = 12u;

/// Image capability flags matching Cameo.Embed.EmbedImageFlags.
namespace ImageFlags {
inline constexpr uint16_t kNone             = 0u;
inline constexpr uint16_t kHasFloat         = 1u << 0;
inline constexpr uint16_t kHasRecordsArrays = 1u << 1;
inline constexpr uint16_t kHasMath          = 1u << 2;
inline constexpr uint16_t kHasClosures      = 1u << 3;
inline constexpr uint16_t kHasRtosSyscalls  = 1u << 4;
inline constexpr uint16_t kHasPointers      = 1u << 5;
inline constexpr uint16_t kHasAxi           = 1u << 6;
inline constexpr uint16_t kHasMemWrite      = 1u << 7;
inline constexpr uint16_t kHasDbg           = 1u << 8;
} // namespace ImageFlags

/// Memory/MMIO access permission flags for board MemoryWindow policies.
namespace MemoryPerms {
inline constexpr uint8_t kNone  = 0u;
inline constexpr uint8_t kRead  = 1u << 0;
inline constexpr uint8_t kWrite = 1u << 1;
inline constexpr uint8_t kAxi   = 1u << 2;
inline constexpr uint8_t kRam   = 1u << 3;
inline constexpr uint8_t kRoAxi = kRead | kAxi;
inline constexpr uint8_t kRwAxi = kRead | kWrite | kAxi;
inline constexpr uint8_t kRoRam = kRead | kRam;
inline constexpr uint8_t kRwRam = kRead | kWrite | kRam;
} // namespace MemoryPerms

/// Castagnoli CRC-32C (0x82F63B78), matching rtos::watchdog::crc32c and Cameo.Embed.
[[nodiscard]] constexpr uint32_t crc32c(const uint8_t* data, size_t bytes, uint32_t seed = 0xFFFFFFFFu) noexcept {
    uint32_t crc = seed;
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0x82F63B78u) : (crc >> 1);
        }
    }
    return ~crc;
}

/// 32-bit FNV-1a hash matching EmbedBinaryFormat.Fnv1a32.
[[nodiscard]] constexpr uint32_t fnv1a32(const uint8_t* data, size_t len) noexcept {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

[[nodiscard]] constexpr uint32_t fnv1a32(StringView sv) noexcept {
    uint32_t h = 2166136261u;
    for (char c : sv) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

/// Typed status codes (no exceptions).
enum class Status : uint8_t {
    Ok = 0,
    InvalidImage = 1,
    UnsupportedVersion = 2,
    ChecksumMismatch = 3,
    InvalidBytecode = 4,
    CapacityExceeded = 5,
    NotLoaded = 6,
    StepLimitExceeded = 7,
    Aborted = 8,
    ValueStackOverflow = 9,
    CallStackOverflow = 10,
    RecordPoolExhausted = 11,
    RecordSlotPoolExhausted = 12,
    BoxPoolExhausted = 13,
    ClosurePoolExhausted = 14,
    DivisionByZero = 15,
    TypeError = 16,
    InvalidKey = 17,
    UnboundHostFunction = 18,
    HostError = 19,
    NotFound = 20,
    InvalidStep = 21,
    MathDisabled = 22,
    FloatDisabled = 23,
    MemoryFault = 24,
    AssertionFailed = 25,
    WriteDenied = 26,
    Breakpoint = 27,
    Stepped = 28,
    WatchpointHit = 29,
};

/// Opcodes matching Cameo.Embed.EmbedOpCode bit-for-bit.
enum class Op : uint8_t {
    Move = 0,
    LoadK = 1,
    LoadNil = 2,
    LoadBool = 3,
    LoadInt = 4,
    GetUpval = 5,
    SetUpval = 6,
    NewBox = 7,
    GetBox = 8,
    SetBox = 9,
    GetGlobalSlot = 10,
    SetGlobalSlot = 11,
    LoadHostFn = 12,
    LoadNamespace = 13,
    GetTable = 14,
    GetField = 15,
    SetTable = 16,
    SetField = 17,
    NewTable = 18,
    Self = 19,
    Add = 20,
    Sub = 21,
    Mul = 22,
    Div = 23,
    Mod = 24,
    Pow = 25,
    IDiv = 26,
    BAnd = 27,
    BOr = 28,
    BXor = 29,
    Shl = 30,
    Shr = 31,
    Unm = 32,
    Not = 33,
    Len = 34,
    BNot = 35,
    Jmp = 36,
    JmpIf = 37,
    JmpIfNot = 38,
    Eq = 39,
    Lt = 40,
    Le = 41,
    Call = 42,
    TailCall = 43,
    Return = 44,
    ForPrep = 45,
    ForLoop = 46,
    SetList = 47,
    Closure = 48,
    IsKind = 49,
    MathFn = 50,
    LoadBuiltin = 51,
};

enum class ConstKind : uint8_t {
    Nil = 0,
    Bool = 1,
    Int = 2,
    Float = 3,
    String = 4,
};

enum class NamespaceId : uint16_t {
    Math = 0,
    Rtos = 1,
    Axi = 2,
    Dbg = 3,
    Ptr = 4,
};

enum class StandardHostId : uint16_t {
    Ticks = 0,
    Sleep = 1,
    Yield = 2,
    Log = 3,
    LogStr = 4,
    WatchdogCheckin = 5,
    QueueSend = 6,
    QueueRecv = 7,
    Notify = 8,
    GpioRead = 9,
    GpioWrite = 10,
    TaskCount = 11,
    TaskState = 12,
    TaskPriority = 13,
    TaskStackBytes = 14,
    TaskGuardOk = 15,
    TaskCycles = 16,
    QueueDepth = 17,
    DbgLogHex = 18,
    DbgHexdump = 19,
    DbgTrace = 20,
    DbgBreakpoint = 21,
    DmaState = 22,
    DmaTransferred = 23,
    DmaCapacity = 24,
    DmaGeneration = 25,
    HwSchedStatus = 26,
    HwSchedReadyMask = 27,
    HwSchedFaultMask = 28,
    HwSchedSeuCount = 29,
    HwSchedDispatches = 30,
    CustomBase = 32,
};

enum class BuiltinId : uint16_t {
    // Unary transcendental math (0..22 match Op::MathFn)
    Sin = 0, Cos = 1, Tan = 2, Asin = 3, Acos = 4, Atan = 5,
    Sinh = 6, Cosh = 7, Tanh = 8, Asinh = 9, Acosh = 10, Atanh = 11,
    Exp = 12, Exp2 = 13, Expm1 = 14,
    Log = 15, Log2 = 16, Log10 = 17, Log1p = 18,
    Sqrt = 19, Cbrt = 20, Deg = 21, Rad = 22,
    // Extra math.* library methods
    Abs = 100, Floor = 101, Ceil = 102, Min = 103, Max = 104,
    Fmod = 105, ToInteger = 106, Ult = 107,
    IsNan = 108, IsInf = 109, IsFinite = 110, IsClose = 111,
    Atan2 = 112,
    // First-class Ptr constructors, methods, and typed MMIO offset/bitfield overlays (200..229)
    PtrOf = 200,
    PtrNull = 201,
    PtrAddr = 202,
    PtrIsNull = 203,
    PtrIsAligned = 204,
    PtrAlignUp = 205,
    PtrAdd = 206,
    PtrSub = 207,
    PtrDiff = 208,
    PtrRead8 = 209,
    PtrRead16 = 210,
    PtrRead32 = 211,
    PtrRead64 = 212,
    PtrWrite8 = 213,
    PtrWrite16 = 214,
    PtrWrite32 = 215,
    PtrWrite64 = 216,
    PtrRead8At = 217,
    PtrRead16At = 218,
    PtrRead32At = 219,
    PtrRead64At = 220,
    PtrWrite8At = 221,
    PtrWrite16At = 222,
    PtrWrite32At = 223,
    PtrWrite64At = 224,
    PtrBits32At = 225,
    PtrRmwBits32At = 226,
    // Explicit axi.* MMIO / AXI4-Lite / AXI4 burst & watchpoint builtins (230..255)
    AxiRead8 = 230,
    AxiRead16 = 231,
    AxiRead32 = 232,
    AxiRead64 = 233,
    AxiWrite8 = 234,
    AxiWrite16 = 235,
    AxiWrite32 = 236,
    AxiWrite64 = 237,
    AxiRmw32 = 238,
    AxiPoll32 = 239,
    AxiBurstRead32 = 240,
    AxiBurstWrite32 = 241,
    AxiScratchGet32 = 242,
    AxiScratchSet32 = 243,
    AxiScratchLen = 244,
    AxiFence = 245,
    AxiWindowCheck = 246,
    AxiWatch = 247,
    AxiUnwatch = 248,
    AxiWatchHits = 249,
    AxiTraceEnable = 250,
    AxiTraceCount = 251,
    // Hardware debugging dbg.* builtins (260..280)
    DbgBits = 260,
    DbgSetBits = 261,
    DbgBit = 262,
    DbgPopcount = 263,
    DbgClz32 = 264,
    DbgCtz32 = 265,
    DbgBswap32 = 266,
    DbgAssert = 267,
    DbgExpectEq = 268,
    DbgHexdumpBuiltin = 269,
    DbgLogHexBuiltin = 270,
};

enum class ValueKind : uint8_t {
    Nil = 0,
    Bool = 1,
    Int = 2,
    Float = 3,
    String = 4,
    Record = 5,
    Closure = 6,
    HostFn = 7,
    Namespace = 8,
    BuiltinFn = 9,
    Box = 10,
    Ptr = 11,
};

/// Trivially copyable tagged VM value (16 bytes default; 8 bytes when CAMEO_EMBED_COMPACT_32=1).
struct Value {
    ValueKind kind{ValueKind::Nil};
    uint8_t pad0_{0};
    uint16_t aux_{0};
#if CAMEO_EMBED_COMPACT_32
    uint32_t bits_{0};
#else
    uint32_t aux2_{0};
    uint64_t bits_{0};
#endif

    [[nodiscard]] static constexpr Value nil() noexcept { return Value{}; }
    [[nodiscard]] static constexpr Value from_bool(bool b) noexcept {
        Value v; v.kind = ValueKind::Bool; v.bits_ = b ? 1u : 0u; return v;
    }
    [[nodiscard]] static constexpr Value from_int(int64_t i) noexcept {
        Value v; v.kind = ValueKind::Int;
#if CAMEO_EMBED_COMPACT_32
        v.bits_ = static_cast<uint32_t>(i);
#else
        v.bits_ = static_cast<uint64_t>(i);
#endif
        return v;
    }
#if CAMEO_EMBED_ENABLE_FLOAT
    [[nodiscard]] static constexpr Value from_float(double d) noexcept {
        Value v; v.kind = ValueKind::Float; v.bits_ = std::bit_cast<uint64_t>(d); return v;
    }
#endif
    [[nodiscard]] static constexpr Value from_string_id(uint32_t id) noexcept {
        Value v; v.kind = ValueKind::String; v.bits_ = id; return v;
    }
    [[nodiscard]] static constexpr Value from_record(uint16_t id) noexcept {
        Value v; v.kind = ValueKind::Record; v.bits_ = id; return v;
    }
    [[nodiscard]] static constexpr Value from_closure(uint16_t id) noexcept {
        Value v; v.kind = ValueKind::Closure; v.bits_ = id; return v;
    }
    [[nodiscard]] static constexpr Value from_host_fn(uint16_t id) noexcept {
        Value v; v.kind = ValueKind::HostFn; v.bits_ = id; return v;
    }
    [[nodiscard]] static constexpr Value from_namespace(NamespaceId ns) noexcept {
        Value v; v.kind = ValueKind::Namespace; v.bits_ = static_cast<uint16_t>(ns); return v;
    }
    [[nodiscard]] static constexpr Value from_builtin_fn(BuiltinId fn) noexcept {
        Value v; v.kind = ValueKind::BuiltinFn; v.bits_ = static_cast<uint16_t>(fn); return v;
    }
    [[nodiscard]] static constexpr Value from_box(uint16_t id) noexcept {
        Value v; v.kind = ValueKind::Box; v.bits_ = id; return v;
    }
    [[nodiscard]] static constexpr Value from_ptr(uint64_t addr) noexcept {
        Value v; v.kind = ValueKind::Ptr;
#if CAMEO_EMBED_COMPACT_32
        v.bits_ = static_cast<uint32_t>(addr);
#else
        v.bits_ = addr;
#endif
        return v;
    }

    [[nodiscard]] constexpr bool is_nil() const noexcept { return kind == ValueKind::Nil; }
    [[nodiscard]] constexpr bool is_bool() const noexcept { return kind == ValueKind::Bool; }
    [[nodiscard]] constexpr bool is_int() const noexcept { return kind == ValueKind::Int; }
    [[nodiscard]] constexpr bool is_float() const noexcept { return kind == ValueKind::Float; }
    [[nodiscard]] constexpr bool is_number() const noexcept { return kind == ValueKind::Int || kind == ValueKind::Float; }
    [[nodiscard]] constexpr bool is_string() const noexcept { return kind == ValueKind::String; }
    [[nodiscard]] constexpr bool is_record() const noexcept { return kind == ValueKind::Record; }
    [[nodiscard]] constexpr bool is_ptr() const noexcept { return kind == ValueKind::Ptr; }
    [[nodiscard]] constexpr bool is_truthy() const noexcept {
        return kind != ValueKind::Nil && !(kind == ValueKind::Bool && bits_ == 0u);
    }

    [[nodiscard]] constexpr bool as_bool() const noexcept { return is_truthy(); }
    [[nodiscard]] constexpr int64_t as_int() const noexcept {
#if CAMEO_EMBED_COMPACT_32
        return static_cast<int64_t>(static_cast<int32_t>(bits_));
#else
        return static_cast<int64_t>(bits_);
#endif
    }
    [[nodiscard]] constexpr uint64_t as_ptr() const noexcept {
        return static_cast<uint64_t>(bits_);
    }
#if CAMEO_EMBED_ENABLE_FLOAT
    [[nodiscard]] constexpr double as_float() const noexcept { return std::bit_cast<double>(bits_); }
    [[nodiscard]] constexpr double as_double() const noexcept {
        return kind == ValueKind::Int ? static_cast<double>(as_int()) : as_float();
    }
#endif
    [[nodiscard]] constexpr uint32_t as_string_id() const noexcept { return static_cast<uint32_t>(bits_); }
    [[nodiscard]] constexpr uint16_t as_handle() const noexcept { return static_cast<uint16_t>(bits_); }
};

#if CAMEO_EMBED_COMPACT_32
static_assert(sizeof(Value) == 8u, "Compact 32-bit Value must be 8 bytes");
#else
static_assert(sizeof(Value) == 16u, "Value must remain compact 16 bytes");
#endif

/// Board MMIO / RAM address window descriptor enforced before any Ptr or axi.* transaction.
struct MemoryWindow {
    uint64_t base{0};
    uint64_t size{0};
    uint8_t perms{MemoryPerms::kNone};
    uint8_t min_width{0u};     ///< Minimum bus access width in bytes (e.g. 4 for AXI4-Lite).
    uint8_t max_width{0u};     ///< Maximum bus access width in bytes (1, 2, 4, or 8).
    bool strict_align{false};  ///< Require natural alignment (addr % width == 0).
};

/// Flags controlling hardware watchpoint matching and break behavior.
namespace WatchFlags {
inline constexpr uint8_t kNone      = 0u;
inline constexpr uint8_t kRead      = 1u << 0;
inline constexpr uint8_t kWrite     = 1u << 1;
inline constexpr uint8_t kReadWrite = kRead | kWrite;
inline constexpr uint8_t kBreak     = 1u << 2; ///< Pause VM with Status::WatchpointHit when triggered.
} // namespace WatchFlags

/// Static watchpoint entry (all-zero default keeps StaticVm 100% in .bss).
struct Watchpoint {
    uint64_t base{0};
    uint64_t size{0};
    uint8_t flags{0};
    bool active{false};
};

/// Flags encoded in BusTraceEvent::flags.
namespace BusTraceFlags {
inline constexpr uint8_t kWrite         = 1u << 0;
inline constexpr uint8_t kWatchpointHit = 1u << 1;
inline constexpr uint8_t kFault         = 1u << 2;
} // namespace BusTraceFlags

/// Compact 24-byte AXI/MMIO bus transaction trace record (zero-heap, FST-exportable).
struct BusTraceEvent {
    uint32_t seq{0};
    uint32_t addr_lo{0};
    uint32_t addr_hi{0};
    uint32_t val_lo{0};
    uint32_t val_hi{0};
    uint8_t width{0};
    uint8_t flags{0};
    uint16_t watch_id{0};

    [[nodiscard]] constexpr uint64_t addr() const noexcept {
        return static_cast<uint64_t>(addr_lo) | (static_cast<uint64_t>(addr_hi) << 32u);
    }
    [[nodiscard]] constexpr uint64_t value() const noexcept {
        return static_cast<uint64_t>(val_lo) | (static_cast<uint64_t>(val_hi) << 32u);
    }
};

static_assert(sizeof(BusTraceEvent) == 24u, "BusTraceEvent must remain compact 24 bytes");

/// Optional callback invoked on every traced bus event or watchpoint hit.
using BusTraceCallback = void (*)(void* ctx, const BusTraceEvent& ev) noexcept;

/// Static PC breakpoint entry (all-zero default keeps StaticVm 100% in .bss).
struct BreakpointEntry {
    uint16_t proto_id{0};
    uint32_t pc{0};
    bool active{false};
};

/// Optional pluggable bus backend (for host simulation, Verilator AXI, or QEMU).
/// When read/write are null, direct volatile MMIO dereference with memory barriers is used.
struct BusBackend {
    void* ctx{nullptr};
    bool (*read)(void* ctx, uint64_t addr, uint8_t width, uint64_t& out) noexcept {nullptr};
    bool (*write)(void* ctx, uint64_t addr, uint8_t width, uint64_t val) noexcept {nullptr};
    void (*fence)(void* ctx) noexcept {nullptr};
};

/// Optional zero-heap text writer callback used by built-in dbg.hexdump and dbg.log_hex.
using DebugWriteFn = bool (*)(void* ctx, const char* data, size_t len) noexcept;

/// Watermark snapshot of mutable static pools for O(1) per-tick arena reset.
struct PoolWatermark {
    uint16_t boxes_used{0};
    uint16_t closures_used{0};
    uint16_t closure_upvals_used{0};
    uint16_t records_used{0};
    uint16_t record_slots_used{0};
};

/// Result of a VM invocation (value count + steps consumed + typed status).
struct ExecResult {
    Status status{Status::Ok};
    uint32_t steps_executed{0};
    size_t result_count{0};
    uint16_t fault_proto{0};
    uint32_t fault_pc{0};
};

/// Context passed to registered C++ host syscalls (zero heap, zero RTTI).
class HostCallContext {
public:
    HostCallContext(const Value* args, size_t arg_count, Value* out, size_t max_out,
                    const uint8_t* image, size_t image_size,
                    uint32_t strings_off, uint16_t string_count,
                    uint32_t str_data_off, uint32_t str_data_size) noexcept
        : args_(args), arg_count_(arg_count), out_(out), max_out_(max_out),
          image_(image), image_size_(image_size),
          strings_off_(strings_off), string_count_(string_count),
          str_data_off_(str_data_off), str_data_size_(str_data_size) {}

    [[nodiscard]] size_t arg_count() const noexcept { return arg_count_; }
    [[nodiscard]] Value arg(size_t index) const noexcept {
        return index < arg_count_ ? args_[index] : Value::nil();
    }
    [[nodiscard]] bool try_int(size_t index, int64_t& out) const noexcept {
        if (index >= arg_count_) return false;
        const Value& v = args_[index];
        if (v.kind == ValueKind::Int) { out = v.as_int(); return true; }
#if CAMEO_EMBED_ENABLE_FLOAT
        if (v.kind == ValueKind::Float) {
            double d = v.as_float();
            if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
                int64_t i = static_cast<int64_t>(d);
                if (static_cast<double>(i) == d) { out = i; return true; }
            }
        }
#endif
        return false;
    }
    [[nodiscard]] bool try_addr(size_t index, uint64_t& out) const noexcept {
        if (index >= arg_count_) return false;
        const Value& v = args_[index];
        if (v.kind == ValueKind::Ptr) { out = v.as_ptr(); return true; }
        if (v.kind == ValueKind::Int) { out = static_cast<uint64_t>(v.as_int()); return true; }
        return false;
    }
#if CAMEO_EMBED_ENABLE_FLOAT
    [[nodiscard]] bool try_double(size_t index, double& out) const noexcept {
        if (index >= arg_count_) return false;
        const Value& v = args_[index];
        if (!v.is_number()) return false;
        out = v.as_double();
        return true;
    }
#endif
    [[nodiscard]] bool try_string(size_t index, StringView& out) const noexcept {
        if (index >= arg_count_ || args_[index].kind != ValueKind::String) return false;
        uint32_t sid = args_[index].as_string_id();
        if (sid >= string_count_) return false;
        const size_t desc = static_cast<size_t>(strings_off_) + static_cast<size_t>(sid) * kStringDescSize;
        if (desc + kStringDescSize > image_size_) return false;
        const uint32_t off = read_u32(image_ + desc);
        const uint32_t len = read_u32(image_ + desc + 4u);
        if (off > str_data_size_ || len > str_data_size_ - off) return false;
        out = StringView(reinterpret_cast<const char*>(image_ + str_data_off_ + off), len);
        return true;
    }

    bool push_result(Value v) noexcept {
        if (result_count_ >= max_out_) return false;
        out_[result_count_++] = v;
        return true;
    }
    [[nodiscard]] size_t result_count() const noexcept { return result_count_; }

private:
    static uint32_t read_u32(const uint8_t* p) noexcept {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    const Value* args_;
    size_t arg_count_;
    Value* out_;
    size_t max_out_;
    size_t result_count_{0};
    const uint8_t* image_;
    size_t image_size_;
    uint32_t strings_off_;
    uint16_t string_count_;
    uint32_t str_data_off_;
    uint32_t str_data_size_;
};

using HostCallback = Status (*)(void* user_ctx, HostCallContext& ctx) noexcept;

namespace detail {

[[nodiscard]] constexpr uint16_t read_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
[[nodiscard]] constexpr int16_t read_i16(const uint8_t* p) noexcept {
    return static_cast<int16_t>(read_u16(p));
}
[[nodiscard]] constexpr uint32_t read_u32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
[[nodiscard]] constexpr uint64_t read_u64(const uint8_t* p) noexcept {
    return static_cast<uint64_t>(read_u32(p)) | (static_cast<uint64_t>(read_u32(p + 4)) << 32);
}

#if CAMEO_EMBED_ENABLE_FLOAT
inline constexpr double kTwoPow63 = 9223372036854775808.0;

[[nodiscard]] constexpr bool is_nan(double d) noexcept { return d != d; }
[[nodiscard]] constexpr bool is_inf(double d) noexcept {
    return !is_nan(d) && (d == std::numeric_limits<double>::infinity() ||
                          d == -std::numeric_limits<double>::infinity());
}
[[nodiscard]] constexpr bool is_finite(double d) noexcept { return !is_nan(d) && !is_inf(d); }

[[nodiscard]] inline double floor_d(double d) noexcept {
#if CAMEO_EMBED_ENABLE_MATH
    return ::floor(d);
#else
    if (!is_finite(d) || d >= kTwoPow63 || d < -kTwoPow63) return d;
    int64_t i = static_cast<int64_t>(d);
    double fi = static_cast<double>(i);
    return (d < fi) ? (fi - 1.0) : fi;
#endif
}

[[nodiscard]] inline double ceil_d(double d) noexcept {
#if CAMEO_EMBED_ENABLE_MATH
    return ::ceil(d);
#else
    if (!is_finite(d) || d >= kTwoPow63 || d < -kTwoPow63) return d;
    int64_t i = static_cast<int64_t>(d);
    double fi = static_cast<double>(i);
    return (d > fi) ? (fi + 1.0) : fi;
#endif
}

[[nodiscard]] inline double fmod_lua(double a, double b) noexcept {
#if CAMEO_EMBED_ENABLE_MATH
    double r = ::fmod(a, b);
#else
    if (b == 0.0 || !is_finite(a) || is_nan(b)) return std::numeric_limits<double>::quiet_NaN();
    if (is_inf(b)) return a;
    double q = a / b;
    double t = (q >= 0.0) ? floor_d(q) : ceil_d(q);
    double r = a - t * b;
#endif
    if (r > 0.0 ? b < 0.0 : (r < 0.0 && b > 0.0)) r += b;
    return r;
}

[[nodiscard]] constexpr bool float_to_int(double d, int64_t& out) noexcept {
    if (d >= -kTwoPow63 && d < kTwoPow63) {
        int64_t i = static_cast<int64_t>(d);
        if (static_cast<double>(i) == d) {
            out = i;
            return true;
        }
    }
    out = 0;
    return false;
}

[[nodiscard]] constexpr bool fits_double(int64_t i) noexcept {
    return i >= -(int64_t(1) << 53) && i <= (int64_t(1) << 53);
}

[[nodiscard]] constexpr bool int_float_eq(int64_t i, double f) noexcept {
    int64_t j = 0;
    return float_to_int(f, j) && i == j;
}

[[nodiscard]] inline bool int_lt_float(int64_t i, double f) noexcept {
    if (fits_double(i)) return static_cast<double>(i) < f;
    if (is_nan(f)) return false;
    if (f >= kTwoPow63) return true;
    if (f > -kTwoPow63) return i < static_cast<int64_t>(ceil_d(f));
    return false;
}

[[nodiscard]] inline bool int_le_float(int64_t i, double f) noexcept {
    if (fits_double(i)) return static_cast<double>(i) <= f;
    if (is_nan(f)) return false;
    if (f >= kTwoPow63) return true;
    if (f >= -kTwoPow63) return i <= static_cast<int64_t>(floor_d(f));
    return false;
}

[[nodiscard]] inline bool float_lt_int(double f, int64_t i) noexcept {
    if (fits_double(i)) return f < static_cast<double>(i);
    if (is_nan(f)) return false;
    if (f >= kTwoPow63) return false;
    if (f >= -kTwoPow63) return static_cast<int64_t>(floor_d(f)) < i;
    return true;
}

[[nodiscard]] inline bool float_le_int(double f, int64_t i) noexcept {
    if (fits_double(i)) return f <= static_cast<double>(i);
    if (is_nan(f)) return false;
    if (f >= kTwoPow63) return false;
    if (f > -kTwoPow63) return static_cast<int64_t>(ceil_d(f)) <= i;
    return true;
}
#endif // CAMEO_EMBED_ENABLE_FLOAT

// Wrapping two's complement helpers (zero C++ signed-overflow UB).
[[nodiscard]] constexpr int64_t wrap_add(int64_t a, int64_t b) noexcept {
    return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
[[nodiscard]] constexpr int64_t wrap_sub(int64_t a, int64_t b) noexcept {
    return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
[[nodiscard]] constexpr int64_t wrap_mul(int64_t a, int64_t b) noexcept {
    return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}
[[nodiscard]] constexpr int64_t wrap_neg(int64_t a) noexcept {
    return static_cast<int64_t>(0u - static_cast<uint64_t>(a));
}

[[nodiscard]] constexpr bool int_floor_div(int64_t x, int64_t y, int64_t& out) noexcept {
    if (y == 0) return false;
    if (y == -1) { out = wrap_neg(x); return true; }
    int64_t q = x / y;
    if ((x % y != 0) && ((x ^ y) < 0)) q = wrap_sub(q, 1);
    out = q;
    return true;
}

[[nodiscard]] constexpr bool int_mod(int64_t x, int64_t y, int64_t& out) noexcept {
    if (y == 0) return false;
    if (y == -1) { out = 0; return true; }
    int64_t r = x % y;
    if (r != 0 && ((r ^ y) < 0)) r = wrap_add(r, y);
    out = r;
    return true;
}

[[nodiscard]] constexpr int64_t shift_left(int64_t x, int64_t n) noexcept {
    if (n <= -64 || n >= 64) return 0;
    const uint64_t ux = static_cast<uint64_t>(x);
    return n >= 0 ? static_cast<int64_t>(ux << static_cast<unsigned>(n))
                  : static_cast<int64_t>(ux >> static_cast<unsigned>(-n));
}

[[nodiscard]] constexpr bool eq_sv(StringView a, StringView b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

[[nodiscard]] constexpr int cmp_sv(StringView a, StringView b) noexcept {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        const auto ca = static_cast<uint8_t>(a[i]);
        const auto cb = static_cast<uint8_t>(b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

[[nodiscard]] constexpr int64_t popcount64(uint64_t v) noexcept {
    int64_t c = 0;
    while (v != 0u) {
        v &= (v - 1u);
        ++c;
    }
    return c;
}

[[nodiscard]] constexpr int64_t clz32(uint32_t v) noexcept {
    if (v == 0u) return 32;
    int64_t n = 0;
    for (uint32_t mask = 0x80000000u; (v & mask) == 0u; mask >>= 1u) ++n;
    return n;
}

[[nodiscard]] constexpr int64_t ctz32(uint32_t v) noexcept {
    if (v == 0u) return 32;
    int64_t n = 0;
    for (uint32_t mask = 1u; (v & mask) == 0u; mask <<= 1u) ++n;
    return n;
}

[[nodiscard]] constexpr uint32_t bswap32(uint32_t v) noexcept {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}

inline constexpr char kHexDigits[17] = "0123456789ABCDEF";

inline void write_hex_digits(char* out, uint64_t val, size_t width_nibbles) noexcept {
    for (size_t i = 0; i < width_nibbles; ++i) {
        const unsigned shift = static_cast<unsigned>((width_nibbles - 1u - i) * 4u);
        out[i] = kHexDigits[(val >> shift) & 0xFu];
    }
}

[[nodiscard]] constexpr int hex_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

struct ProtoHeader {
    uint32_t code_offset;
    uint32_t code_count;
    uint32_t const_offset;
    uint16_t const_count;
    uint16_t upval_offset;
    uint16_t upval_count;
    uint16_t name_str_id;
    uint8_t num_params;
    uint8_t max_stack;
    uint8_t flags;
};

struct Instr {
    Op op;
    int16_t a;
    int16_t b;
    int16_t c;
};

} // namespace detail

/**
 * @brief Zero-heap staging receiver for hot-loading CRC-32C-verified CEMB v1 packages
 *        over UART, console monitor, or memory buffer without reflashing firmware.
 */
template<size_t MaxPackageBytes = 4096>
class PackageReceiver {
    static_assert(MaxPackageBytes >= kHeaderSize && MaxPackageBytes <= 262144u);

    alignas(8) uint8_t buffer_[MaxPackageBytes]{};
    size_t length_{0};
    bool ready_{false};

public:
    constexpr PackageReceiver() noexcept = default;

    void reset() noexcept {
        length_ = 0u;
        ready_ = false;
    }

    /// Append raw binary bytes into the static staging buffer.
    [[nodiscard]] bool feed_bytes(const uint8_t* data, size_t len) noexcept {
        if (data == nullptr && len > 0u) return false;
        if (len > MaxPackageBytes - length_) return false;
        for (size_t i = 0; i < len; ++i) buffer_[length_++] = data[i];
        ready_ = false;
        return true;
    }

    /// Append ASCII hexadecimal text (ignores whitespace / CR / LF; requires even digit count).
    [[nodiscard]] bool feed_hex(StringView hex) noexcept {
        int hi = -1;
        for (char c : hex) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            const int nib = detail::hex_nibble(c);
            if (nib < 0) return false;
            if (hi < 0) {
                hi = nib;
            } else {
                if (length_ >= MaxPackageBytes) return false;
                buffer_[length_++] = static_cast<uint8_t>((hi << 4) | nib);
                hi = -1;
            }
        }
        ready_ = false;
        return hi < 0;
    }

    /// Verify CEMB header, exact package size, and Castagnoli CRC-32C before loading.
    [[nodiscard]] Status verify() noexcept {
        ready_ = false;
        if (length_ < kHeaderSize) return Status::InvalidImage;
        if (detail::read_u32(buffer_ + 0) != kMagic) return Status::InvalidImage;
        if (detail::read_u16(buffer_ + 4) != kVersion) return Status::UnsupportedVersion;
        const uint32_t total = detail::read_u32(buffer_ + 8);
        if (total != length_ || total < kHeaderSize) return Status::InvalidImage;
        const uint32_t expect_crc = detail::read_u32(buffer_ + 12);
        const uint32_t actual_crc = crc32c(buffer_ + 16, length_ - 16u);
        if (actual_crc != expect_crc) return Status::ChecksumMismatch;
        ready_ = true;
        return Status::Ok;
    }

    [[nodiscard]] bool is_ready() const noexcept { return ready_; }
    [[nodiscard]] size_t bytes_received() const noexcept { return length_; }
    [[nodiscard]] static constexpr size_t capacity() noexcept { return MaxPackageBytes; }
    [[nodiscard]] Span<const uint8_t> span() const noexcept {
        return {buffer_, ready_ ? length_ : 0u};
    }
};

/** @brief Fixed-capacity, zero-heap C++20 static VM for Cameo.Embed bytecode images. */
template<
    size_t StackSlots = 256,
    size_t MaxFrames = 32,
    size_t MaxGlobals = 32,
    size_t MaxBoxes = 64,
    size_t MaxClosures = 64,
    size_t MaxRecords = 32,
    size_t MaxRecordSlots = 256,
    size_t MaxHostFunctions = 64,
    size_t ScratchWords = 64,
    size_t MaxMemoryWindows = 16,
    size_t MaxWatchpoints = 8,
    size_t BusTraceCapacity = 64,
    size_t MaxBreakpoints = 8
>
class StaticVm {
    static_assert(StackSlots >= 32 && StackSlots <= 65535);
    static_assert(MaxFrames >= 2 && MaxFrames <= 255);
    static_assert(MaxGlobals >= 1 && MaxGlobals <= 4096);
    static_assert(MaxBoxes >= 1 && MaxBoxes <= 4096);
    static_assert(MaxClosures >= 1 && MaxClosures <= 4096);
    static_assert(MaxRecords >= 1 && MaxRecords <= 4096);
    static_assert(MaxRecordSlots >= 4 && MaxRecordSlots <= 32768);
    static_assert(MaxHostFunctions >= 32 && MaxHostFunctions <= 1024);
    static_assert(ScratchWords >= 4 && ScratchWords <= 4096);
    static_assert(MaxMemoryWindows >= 1 && MaxMemoryWindows <= 64);
    static_assert(MaxWatchpoints >= 1 && MaxWatchpoints <= 32);
    static_assert(BusTraceCapacity >= 4 && (BusTraceCapacity & (BusTraceCapacity - 1u)) == 0u,
                  "BusTraceCapacity must be a power of two >= 4");
    static_assert(MaxBreakpoints >= 1 && MaxBreakpoints <= 32);

    static constexpr size_t kMaxClosureUpvals = MaxClosures * 4u;

    struct Frame {
        uint16_t closure_id{0};
        uint16_t proto_id{0};
        uint16_t base{0};
        uint16_t ret_dest{0};
        int16_t want_results{0};
        uint32_t pc{0};
    };

    struct ClosureSlot {
        uint16_t proto_id{0};
        uint16_t upval_offset{0};
        uint16_t upval_count{0};
    };

    struct RecordSlot {
        uint16_t first_entry{0u}; // 1-based index into record_entries_ (0 = empty, keeps StaticVm in .bss)
        bool used{false};
    };

    struct RecordEntry {
        Value key{};
        Value val{};
        uint16_t next{0u}; // 1-based index into record_entries_ (0 = end of chain)
        bool used{false};
    };

    struct HostBinding {
        HostCallback fn{nullptr};
        void* ctx{nullptr};
    };

    // ROM / staged CEMB image view (zero copy)
    const uint8_t* image_{nullptr};
    size_t image_size_{0};
    bool loaded_{false};

    // Parsed CEMB header metadata
    uint16_t image_flags_{0};
    uint16_t proto_count_{0};
    uint16_t const_count_{0};
    uint32_t code_count_{0};
    uint16_t upval_desc_count_{0};
    uint16_t global_count_{0};
    uint16_t host_import_count_{0};
    uint16_t string_count_{0};
    uint32_t protos_off_{0};
    uint32_t consts_off_{0};
    uint32_t code_off_{0};
    uint32_t upvals_off_{0};
    uint32_t globals_off_{0};
    uint32_t hosts_off_{0};
    uint32_t strings_off_{0};
    uint32_t str_data_off_{0};
    uint32_t str_data_size_{0};
    uint32_t image_crc_{0};

    // Fixed static runtime storage (zero heap, 100% .bss)
    Value stack_[StackSlots]{};
    Frame frames_[MaxFrames]{};
    Value globals_[MaxGlobals]{};
    Value boxes_[MaxBoxes]{};
    ClosureSlot closures_[MaxClosures]{};
    uint16_t closure_upvals_[kMaxClosureUpvals]{};
    RecordSlot records_[MaxRecords]{};
    RecordEntry record_entries_[MaxRecordSlots]{};
    HostBinding hosts_[MaxHostFunctions]{};

    // Static AXI burst scratchpad & board MMIO window policy table
    uint32_t scratch_[ScratchWords]{};
    MemoryWindow windows_[MaxMemoryWindows]{};
    uint16_t window_count_{0};
    bool axi_read_only_{false};
    BusBackend bus_{};

    // Hardware watchpoints & AXI bus-trace ring (100% .bss)
    Watchpoint watchpoints_[MaxWatchpoints]{};
    uint32_t watch_hits_{0};
    bool watch_break_pending_{false};
    uint64_t last_watch_addr_{0};
    BusTraceEvent bus_trace_[BusTraceCapacity]{};
    uint32_t bus_trace_total_{0};
    bool bus_trace_enabled_{false};
    BusTraceCallback bus_trace_cb_{nullptr};
    void* bus_trace_cb_ctx_{nullptr};

    // On-target PC breakpoints & resumable single-stepping state (100% .bss)
    BreakpointEntry breakpoints_[MaxBreakpoints]{};
    size_t active_frame_count_{0};
    size_t active_top_{0};
    bool paused_{false};
    bool skip_bp_once_{false};

    // Optional built-in debug text sink (for dbg.hexdump / dbg.log_hex)
    DebugWriteFn debug_writer_{nullptr};
    void* debug_writer_ctx_{nullptr};

    uint16_t boxes_used_{0};
    uint16_t closures_used_{0};
    uint16_t closure_upvals_used_{0};
    uint16_t records_used_{0};
    uint16_t record_slots_used_{0};

    Value module_exports_{};
    const volatile bool* abort_flag_{nullptr};

    // Diagnostic latches populated on MemoryFault / AssertionFailed
    uint64_t last_fault_addr_{0};
    int64_t last_assert_code_{0};
    int64_t last_expect_actual_{0};
    int64_t last_expect_expected_{0};

public:
    constexpr StaticVm() noexcept = default;
    StaticVm(const StaticVm&) = delete;
    StaticVm& operator=(const StaticVm&) = delete;

    /// Attach an optional external abort flag (e.g., set by an RTOS watchdog or ISR).
    void set_abort_flag(const volatile bool* flag) noexcept { abort_flag_ = flag; }

    /// Attach an optional zero-heap text sink for dbg.hexdump and dbg.log_hex.
    void set_debug_writer(DebugWriteFn fn, void* ctx = nullptr) noexcept {
        debug_writer_ = fn;
        debug_writer_ctx_ = ctx;
    }

    /// Lock or unlock all AXI and Ptr writes at runtime (deny-writes for flight/read-only diagnostics).
    void set_axi_read_only(bool ro) noexcept { axi_read_only_ = ro; }
    [[nodiscard]] bool axi_read_only() const noexcept { return axi_read_only_; }

    /// Attach an optional bus backend (Verilator/host mock/QEMU). Null callbacks use native volatile MMIO.
    void set_bus_backend(BusBackend backend) noexcept { bus_ = backend; }

    /// Clear all registered board MMIO/RAM windows.
    void clear_memory_windows() noexcept { window_count_ = 0u; }

    /// Register a board MMIO/RAM address window enforced before every Ptr and axi.* access.
    [[nodiscard]] bool add_memory_window(MemoryWindow win) noexcept {
        if (window_count_ >= MaxMemoryWindows || win.size == 0u ||
            win.min_width == 0u || win.max_width < win.min_width || win.max_width > 8u)
            return false;
        windows_[window_count_++] = win;
        return true;
    }

    /// Check whether an access of width bytes at addr is permitted by the board window policy.
    [[nodiscard]] bool check_memory_access(uint64_t addr, uint8_t width, bool is_write) const noexcept {
        if (width != 1u && width != 2u && width != 4u && width != 8u) return false;
        if (is_write && axi_read_only_) return false;
        for (uint16_t i = 0; i < window_count_; ++i) {
            const MemoryWindow& w = windows_[i];
            if (addr < w.base) continue;
            const uint64_t off = addr - w.base;
            if (static_cast<uint64_t>(width) > w.size || off > w.size - static_cast<uint64_t>(width))
                continue;
            if (is_write && (w.perms & MemoryPerms::kWrite) == 0u) return false;
            if (!is_write && (w.perms & MemoryPerms::kRead) == 0u) return false;
            if (width < w.min_width || width > w.max_width) return false;
            if (w.strict_align && (addr & static_cast<uint64_t>(width - 1u)) != 0u) return false;
            return true;
        }
        return false;
    }

    /// Read a static scratchpad word (0..ScratchWords-1).
    [[nodiscard]] uint32_t scratch_word(size_t index) const noexcept {
        return index < ScratchWords ? scratch_[index] : 0u;
    }
    /// Write a static scratchpad word (0..ScratchWords-1).
    [[nodiscard]] bool set_scratch_word(size_t index, uint32_t val) noexcept {
        if (index >= ScratchWords) return false;
        scratch_[index] = val;
        return true;
    }

    /// Diagnostic latches from last MemoryFault / AssertionFailed.
    [[nodiscard]] uint64_t last_fault_addr() const noexcept { return last_fault_addr_; }
    [[nodiscard]] int64_t last_assert_code() const noexcept { return last_assert_code_; }
    [[nodiscard]] int64_t last_expect_actual() const noexcept { return last_expect_actual_; }
    [[nodiscard]] int64_t last_expect_expected() const noexcept { return last_expect_expected_; }

    /// Register a C++ host callback for a numeric host syscall ID.
    [[nodiscard]] bool register_host(uint16_t host_id, HostCallback fn, void* user_ctx = nullptr) noexcept {
        if (host_id >= MaxHostFunctions) return false;
        hosts_[host_id] = {fn, user_ctx};
        return true;
    }

    [[nodiscard]] bool register_host(StandardHostId id, HostCallback fn, void* user_ctx = nullptr) noexcept {
        return register_host(static_cast<uint16_t>(id), fn, user_ctx);
    }

    /// Verify CRC-32C, structure, capacities, feature gates, and bytecode safety of a CEMB v1 image.
    [[nodiscard]] Status load(Span<const uint8_t> image) noexcept {
        if (image.size() < kHeaderSize || image.data() == nullptr)
            return Status::InvalidImage;

        const uint8_t* p = image.data();
        const uint32_t magic = detail::read_u32(p + 0);
        if (magic != kMagic) return Status::InvalidImage;
        const uint16_t version = detail::read_u16(p + 4);
        if (version != kVersion) return Status::UnsupportedVersion;

        const uint16_t flags = detail::read_u16(p + 6);
#if !CAMEO_EMBED_ENABLE_FLOAT
        if ((flags & ImageFlags::kHasFloat) != 0u) return Status::FloatDisabled;
#endif
        if (axi_read_only_ && (flags & ImageFlags::kHasMemWrite) != 0u) {
            return Status::WriteDenied;
        }

        const uint32_t total_size = detail::read_u32(p + 8);
        if (total_size != image.size() || total_size < kHeaderSize)
            return Status::InvalidImage;

        const uint32_t expected_crc = detail::read_u32(p + 12);
        const uint32_t actual_crc = crc32c(p + 16, image.size() - 16u);
        if (actual_crc != expected_crc) return Status::ChecksumMismatch;

        const uint16_t p_count = detail::read_u16(p + 16);
        const uint16_t c_count = detail::read_u16(p + 18);
        const uint32_t i_count = detail::read_u32(p + 20);
        const uint16_t u_count = detail::read_u16(p + 24);
        const uint16_t g_count = detail::read_u16(p + 26);
        const uint16_t h_count = detail::read_u16(p + 28);
        const uint16_t s_count = detail::read_u16(p + 30);

        const uint16_t req_stack = detail::read_u16(p + 32);
        const uint16_t req_frames = detail::read_u16(p + 34);
        const uint16_t req_boxes = detail::read_u16(p + 36);
        const uint16_t req_closures = detail::read_u16(p + 38);
        const uint16_t req_records = detail::read_u16(p + 40);
        const uint16_t req_record_slots = detail::read_u16(p + 42);

        const uint32_t p_off = detail::read_u32(p + 44);
        const uint32_t c_off = detail::read_u32(p + 48);
        const uint32_t i_off = detail::read_u32(p + 52);
        const uint32_t u_off = detail::read_u32(p + 56);
        const uint32_t g_off = detail::read_u32(p + 60);
        const uint32_t h_off = detail::read_u32(p + 64);
        const uint32_t s_off = detail::read_u32(p + 68);
        const uint32_t sd_off = detail::read_u32(p + 72);
        const uint32_t sd_size = detail::read_u32(p + 76);

        if (p_count == 0u) return Status::InvalidImage;
        if (g_count > MaxGlobals ||
            req_stack > StackSlots ||
            req_frames > MaxFrames ||
            req_boxes > MaxBoxes ||
            req_closures > MaxClosures ||
            req_records > MaxRecords ||
            req_record_slots > MaxRecordSlots) {
            return Status::CapacityExceeded;
        }

        auto section_ok = [&](uint32_t off, size_t count, size_t elem_size) noexcept -> bool {
            if (off > total_size) return false;
            const size_t avail = total_size - off;
            return count <= avail / elem_size;
        };
        if (!section_ok(p_off, p_count, kProtoDescSize) ||
            !section_ok(c_off, c_count, kConstDescSize) ||
            !section_ok(i_off, i_count, kInstrSize) ||
            !section_ok(u_off, u_count, kUpvalDescSize) ||
            !section_ok(g_off, g_count, kGlobalDescSize) ||
            !section_ok(h_off, h_count, kHostImportDescSize) ||
            !section_ok(s_off, s_count, kStringDescSize) ||
            sd_off > total_size ||
            sd_size > total_size - sd_off) {
            return Status::InvalidImage;
        }

        // Validate string descriptors before committing state
        for (uint16_t si = 0; si < s_count; ++si) {
            const uint8_t* sd = p + s_off + static_cast<size_t>(si) * kStringDescSize;
            const uint32_t soff = detail::read_u32(sd + 0);
            const uint32_t slen = detail::read_u32(sd + 4);
            if (soff > sd_size || slen > sd_size - soff)
                return Status::InvalidBytecode;
        }

        // Validate constant kinds (reject float constants when float disabled)
#if !CAMEO_EMBED_ENABLE_FLOAT
        for (uint16_t ci = 0; ci < c_count; ++ci) {
            const uint8_t* cd = p + c_off + static_cast<size_t>(ci) * kConstDescSize;
            if (static_cast<ConstKind>(cd[0]) == ConstKind::Float)
                return Status::FloatDisabled;
        }
#endif

        // Commit metadata and validate every prototype structurally
        loaded_ = false;
        reset_runtime_pools();
        image_ = p;
        image_size_ = image.size();
        image_flags_ = flags;
        proto_count_ = p_count;
        const_count_ = c_count;
        code_count_ = i_count;
        upval_desc_count_ = u_count;
        global_count_ = g_count;
        host_import_count_ = h_count;
        string_count_ = s_count;
        protos_off_ = p_off;
        consts_off_ = c_off;
        code_off_ = i_off;
        upvals_off_ = u_off;
        globals_off_ = g_off;
        hosts_off_ = h_off;
        strings_off_ = s_off;
        str_data_off_ = sd_off;
        str_data_size_ = sd_size;
        image_crc_ = expected_crc;

        for (uint16_t pid = 0; pid < proto_count_; ++pid) {
            const detail::ProtoHeader ph = read_proto(pid);
            if (ph.max_stack < ph.num_params || ph.max_stack > StackSlots)
                return Status::InvalidBytecode;
            if (ph.code_offset > code_count_ || ph.code_count > code_count_ - ph.code_offset)
                return Status::InvalidBytecode;
            if (ph.const_offset > const_count_ || ph.const_count > const_count_ - ph.const_offset)
                return Status::InvalidBytecode;
            if (ph.upval_offset > upval_desc_count_ || ph.upval_count > upval_desc_count_ - ph.upval_offset)
                return Status::InvalidBytecode;

            const Status st = validate_proto_code(ph);
            if (st != Status::Ok) return st;
        }

        loaded_ = true;
        return Status::Ok;
    }

    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    [[nodiscard]] uint32_t image_crc32c() const noexcept { return image_crc_; }
    [[nodiscard]] uint16_t image_flags() const noexcept { return image_flags_; }
    [[nodiscard]] Value module_exports() const noexcept { return module_exports_; }

    /// Snapshot current pool usage (typically right after run_main()) for O(1) periodic reset.
    [[nodiscard]] PoolWatermark mark_pools() const noexcept {
        return {boxes_used_, closures_used_, closure_upvals_used_, records_used_, record_slots_used_};
    }

    /// Reclaim any temporary records/boxes/closures allocated after a saved PoolWatermark.
    void reset_pools_to(PoolWatermark mark) noexcept {
        if (mark.boxes_used < boxes_used_) {
            for (uint16_t i = mark.boxes_used; i < boxes_used_; ++i) boxes_[i] = Value::nil();
            boxes_used_ = mark.boxes_used;
        }
        if (mark.closures_used < closures_used_) closures_used_ = mark.closures_used;
        if (mark.closure_upvals_used < closure_upvals_used_) closure_upvals_used_ = mark.closure_upvals_used;
        if (mark.records_used < records_used_) {
            for (uint16_t i = mark.records_used; i < records_used_; ++i) {
                records_[i] = RecordSlot{};
            }
            records_used_ = mark.records_used;
        }
        if (mark.record_slots_used < record_slots_used_) {
            for (uint16_t t = 0; t < records_used_; ++t) {
                uint16_t* link = &records_[t].first_entry;
                while (*link != 0u) {
                    const uint16_t idx = static_cast<uint16_t>(*link - 1u);
                    if (idx >= mark.record_slots_used) {
                        *link = record_entries_[idx].next;
                    } else {
                        link = &record_entries_[idx].next;
                    }
                }
            }
            for (uint16_t i = mark.record_slots_used; i < record_slots_used_; ++i) {
                record_entries_[i] = RecordEntry{};
            }
            record_slots_used_ = mark.record_slots_used;
        }
    }

    /// Execute the top-level chunk (proto 0) to initialize globals/functions and capture exports.
    [[nodiscard]] ExecResult run_main(uint32_t step_budget = 100000u,
                                      Span<Value> out_results = {}) noexcept {
        if (!loaded_) return {Status::NotLoaded, 0, 0, 0, 0};

        uint16_t root_cl = 0;
        if (!alloc_closure(0u, root_cl)) return {Status::ClosurePoolExhausted, 0, 0, 0, 0};

        Value single_buf[1]{};
        Value* target = out_results.empty() ? single_buf : out_results.data();
        const size_t cap = out_results.empty() ? 1u : out_results.size();

        ExecResult res = invoke_closure(root_cl, {}, Span<Value>(target, cap), step_budget);
        if (res.status == Status::Ok && res.result_count > 0 && target[0].is_record()) {
            module_exports_ = target[0];
        }
        return res;
    }

    /// Look up a script global slot by name.
    [[nodiscard]] bool find_global(StringView name, uint16_t& slot_out) const noexcept {
        if (!loaded_) return false;
        const uint32_t h = fnv1a32(name);
        for (uint16_t i = 0; i < global_count_; ++i) {
            const uint8_t* gd = image_ + globals_off_ + static_cast<size_t>(i) * kGlobalDescSize;
            const uint16_t sid = detail::read_u16(gd + 0);
            if (string_hash(sid) == h && detail::eq_sv(string_view_of(sid), name)) {
                slot_out = detail::read_u16(gd + 2);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool get_global(StringView name, Value& out) const noexcept {
        uint16_t slot = 0;
        if (!find_global(name, slot) || slot >= MaxGlobals) return false;
        out = globals_[slot];
        return true;
    }

    [[nodiscard]] bool set_global(StringView name, Value val) noexcept {
        uint16_t slot = 0;
        if (!find_global(name, slot) || slot >= MaxGlobals) return false;
        globals_[slot] = val;
        return true;
    }

    /// Look up an exported value (from the chunk's returned export record, or fallback to globals).
    [[nodiscard]] bool get_export(StringView name, Value& out) const noexcept {
        if (!loaded_) return false;
        if (module_exports_.is_record()) {
            if (record_get_by_sv(module_exports_.as_handle(), name, out)) return true;
        }
        return get_global(name, out);
    }

    /// Call a callable value (closure or host function) with bounded steps.
    [[nodiscard]] ExecResult call_value(Value callee,
                                        Span<const Value> args,
                                        Span<Value> results,
                                        uint32_t step_budget = 100000u) noexcept {
        if (!loaded_) return {Status::NotLoaded, 0, 0, 0, 0};
        if (callee.kind == ValueKind::Closure) {
            return invoke_closure(callee.as_handle(), args, results, step_budget);
        }
        if (callee.kind == ValueKind::HostFn) {
            const uint16_t hid = callee.as_handle();
            if (hid >= MaxHostFunctions || hosts_[hid].fn == nullptr)
                return {Status::UnboundHostFunction, 0, 0, 0, 0};
            HostCallContext ctx(args.data(), args.size(), results.data(), results.size(),
                                image_, image_size_, strings_off_, string_count_, str_data_off_, str_data_size_);
            const Status st = hosts_[hid].fn(hosts_[hid].ctx, ctx);
            return {st, 1u, ctx.result_count(), 0, 0};
        }
        return {Status::TypeError, 0, 0, 0, 0};
    }

    /// Call an exported or global Cameo function by name (e.g., "diagnose_bus").
    [[nodiscard]] ExecResult call_export(StringView name,
                                         Span<const Value> args,
                                         Span<Value> results,
                                         uint32_t step_budget = 100000u) noexcept {
        Value fn{};
        if (!get_export(name, fn)) return {Status::NotFound, 0, 0, 0, 0};
        return call_value(fn, args, results, step_budget);
    }

    /// Read a field from a VM record value by string name (for inspecting returned structs in C++).
    [[nodiscard]] bool record_get_field(Value rec, StringView field, Value& out) const noexcept {
        if (!loaded_ || !rec.is_record()) return false;
        return record_get_by_sv(rec.as_handle(), field, out);
    }

    /// Read an integer-indexed element from a VM fixed array value (1-based, Cameo convention).
    [[nodiscard]] bool array_get_index(Value arr, int64_t idx, Value& out) const noexcept {
        if (!loaded_ || !arr.is_record()) return false;
        out = record_get(arr.as_handle(), Value::from_int(idx));
        return !out.is_nil();
    }

    /// Resolve a ROM string value to a zero-copy StringView (NUL-terminated in ROM).
    [[nodiscard]] StringView get_string(Value v) const noexcept {
        if (!loaded_ || !v.is_string()) return {};
        return string_view_of(v.as_string_id());
    }

    // ---------------- Hardware Watchpoints & AXI Bus-Trace Ring ----------------

    /// Enable or disable recording all AXI/Ptr transactions into the static BusTraceRing.
    void set_bus_trace_enabled(bool enabled) noexcept { bus_trace_enabled_ = enabled; }
    [[nodiscard]] bool bus_trace_enabled() const noexcept { return bus_trace_enabled_; }

    /// Attach an optional callback fired on every traced bus event or watchpoint hit.
    void set_bus_trace_callback(BusTraceCallback cb, void* ctx = nullptr) noexcept {
        bus_trace_cb_ = cb;
        bus_trace_cb_ctx_ = ctx;
    }

    /// Number of retained bus-trace events (0..BusTraceCapacity).
    [[nodiscard]] size_t bus_trace_count() const noexcept {
        return bus_trace_total_ < BusTraceCapacity ? static_cast<size_t>(bus_trace_total_) : BusTraceCapacity;
    }
    [[nodiscard]] uint32_t bus_trace_total() const noexcept { return bus_trace_total_; }

    /// Read a retained bus-trace event by oldest-first index (0 = oldest, count()-1 = newest).
    [[nodiscard]] bool read_bus_trace_oldest(size_t offset, BusTraceEvent& out) const noexcept {
        const size_t n = bus_trace_count();
        if (offset >= n) return false;
        const size_t oldest = (bus_trace_total_ >= BusTraceCapacity)
            ? (static_cast<size_t>(bus_trace_total_) & (BusTraceCapacity - 1u))
            : 0u;
        out = bus_trace_[(oldest + offset) & (BusTraceCapacity - 1u)];
        return true;
    }

    void clear_bus_trace() noexcept {
        bus_trace_total_ = 0u;
    }

    /// Install a hardware watchpoint over [base, base+size). Returns slot ID (0..MaxWatchpoints-1) or -1.
    [[nodiscard]] int add_watchpoint(uint64_t base, uint64_t size, uint8_t flags) noexcept {
        if (size == 0u || (flags & WatchFlags::kReadWrite) == 0u) return -1;
        for (size_t i = 0; i < MaxWatchpoints; ++i) {
            if (!watchpoints_[i].active) {
                watchpoints_[i] = {base, size, flags, true};
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    [[nodiscard]] bool remove_watchpoint(size_t id) noexcept {
        if (id >= MaxWatchpoints || !watchpoints_[id].active) return false;
        watchpoints_[id] = Watchpoint{};
        return true;
    }

    void clear_watchpoints() noexcept {
        for (size_t i = 0; i < MaxWatchpoints; ++i) watchpoints_[i] = Watchpoint{};
        watch_hits_ = 0u;
        watch_break_pending_ = false;
        last_watch_addr_ = 0u;
    }

    [[nodiscard]] uint32_t watch_hits() const noexcept { return watch_hits_; }
    [[nodiscard]] uint64_t last_watch_addr() const noexcept { return last_watch_addr_; }

    // ---------------- On-Target PC Breakpoints & Single-Stepping ----------------

    [[nodiscard]] bool set_breakpoint(uint16_t proto_id, uint32_t pc) noexcept {
        for (size_t i = 0; i < MaxBreakpoints; ++i) {
            if (breakpoints_[i].active && breakpoints_[i].proto_id == proto_id && breakpoints_[i].pc == pc)
                return true;
        }
        for (size_t i = 0; i < MaxBreakpoints; ++i) {
            if (!breakpoints_[i].active) {
                breakpoints_[i] = {proto_id, pc, true};
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool clear_breakpoint(uint16_t proto_id, uint32_t pc) noexcept {
        for (size_t i = 0; i < MaxBreakpoints; ++i) {
            if (breakpoints_[i].active && breakpoints_[i].proto_id == proto_id && breakpoints_[i].pc == pc) {
                breakpoints_[i] = BreakpointEntry{};
                return true;
            }
        }
        return false;
    }

    void clear_breakpoints() noexcept {
        for (size_t i = 0; i < MaxBreakpoints; ++i) breakpoints_[i] = BreakpointEntry{};
    }

    /// Prepare an exported function call without executing instructions (ready for step_one / continue_run).
    [[nodiscard]] Status start_export(StringView name, Span<const Value> args = {}) noexcept {
        if (!loaded_) return Status::NotLoaded;
        Value fn{};
        if (!get_export(name, fn)) return Status::NotFound;
        return start_value(fn, args);
    }

    /// Prepare a closure call without executing instructions (ready for step_one / continue_run).
    [[nodiscard]] Status start_value(Value callee, Span<const Value> args = {}) noexcept {
        if (!loaded_) return Status::NotLoaded;
        if (callee.kind != ValueKind::Closure) return Status::TypeError;
        return prepare_closure_frame(callee.as_handle(), args);
    }

    /// Execute a single VM instruction from the currently paused call frame.
    [[nodiscard]] ExecResult step_one(Span<Value> results = {}) noexcept {
        if (!loaded_) return {Status::NotLoaded, 0, 0, 0, 0};
        if (!paused_ || active_frame_count_ == 0u) return {Status::NotFound, 0, 0, 0, 0};
        return resume_frames(results, 1u, true);
    }

    /// Resume a paused call frame until return, PC breakpoint, watchpoint break, or step limit.
    [[nodiscard]] ExecResult continue_run(Span<Value> results = {}, uint32_t step_budget = 100000u) noexcept {
        if (!loaded_) return {Status::NotLoaded, 0, 0, 0, 0};
        if (!paused_ || active_frame_count_ == 0u) return {Status::NotFound, 0, 0, 0, 0};
        return resume_frames(results, step_budget, false);
    }

    [[nodiscard]] bool is_paused() const noexcept { return paused_ && active_frame_count_ > 0u; }
    [[nodiscard]] size_t call_depth() const noexcept { return active_frame_count_; }
    [[nodiscard]] uint16_t current_proto() const noexcept {
        return active_frame_count_ > 0u ? frames_[active_frame_count_ - 1u].proto_id : 0u;
    }
    [[nodiscard]] uint32_t current_pc() const noexcept {
        return active_frame_count_ > 0u ? frames_[active_frame_count_ - 1u].pc : 0u;
    }
    [[nodiscard]] uint16_t frame_proto(size_t depth) const noexcept {
        return depth < active_frame_count_ ? frames_[depth].proto_id : 0u;
    }
    [[nodiscard]] uint32_t frame_pc(size_t depth) const noexcept {
        return depth < active_frame_count_ ? frames_[depth].pc : 0u;
    }
    [[nodiscard]] uint16_t current_frame_max_stack() const noexcept {
        if (!loaded_ || active_frame_count_ == 0u) return 0u;
        return read_proto(frames_[active_frame_count_ - 1u].proto_id).max_stack;
    }
    [[nodiscard]] bool inspect_reg(size_t reg_idx, Value& out) const noexcept {
        if (!loaded_ || active_frame_count_ == 0u) return false;
        const Frame& fr = frames_[active_frame_count_ - 1u];
        const detail::ProtoHeader ph = read_proto(fr.proto_id);
        if (reg_idx >= ph.max_stack) return false;
        const size_t slot = static_cast<size_t>(fr.base) + reg_idx;
        if (slot >= StackSlots) return false;
        out = stack_[slot];
        return true;
    }

private:
    void reset_runtime_pools() noexcept {
        for (size_t i = 0; i < StackSlots; ++i) stack_[i] = Value::nil();
        for (size_t i = 0; i < MaxGlobals; ++i) globals_[i] = Value::nil();
        for (size_t i = 0; i < MaxBoxes; ++i) boxes_[i] = Value::nil();
        for (size_t i = 0; i < MaxRecords; ++i) records_[i] = RecordSlot{};
        for (size_t i = 0; i < MaxRecordSlots; ++i) record_entries_[i] = RecordEntry{};
        for (size_t i = 0; i < ScratchWords; ++i) scratch_[i] = 0u;
        boxes_used_ = 0;
        closures_used_ = 0;
        closure_upvals_used_ = 0;
        records_used_ = 0;
        record_slots_used_ = 0;
        module_exports_ = Value::nil();
        active_frame_count_ = 0;
        active_top_ = 0;
        paused_ = false;
        skip_bp_once_ = false;
        watch_break_pending_ = false;
        last_watch_addr_ = 0;
        last_fault_addr_ = 0;
        last_assert_code_ = 0;
        last_expect_actual_ = 0;
        last_expect_expected_ = 0;
    }

    [[nodiscard]] bool has_breakpoint_at(uint16_t proto_id, uint32_t pc) const noexcept {
        for (size_t i = 0; i < MaxBreakpoints; ++i) {
            if (breakpoints_[i].active && breakpoints_[i].proto_id == proto_id && breakpoints_[i].pc == pc)
                return true;
        }
        return false;
    }

    void record_bus_event(uint64_t addr, uint8_t width, uint64_t val, bool is_write, bool is_fault) noexcept {
        bool wp_hit = false;
        uint16_t wp_id = 0u;
        const uint8_t rw_bit = is_write ? WatchFlags::kWrite : WatchFlags::kRead;
        if (!is_fault) {
            for (size_t i = 0; i < MaxWatchpoints; ++i) {
                const Watchpoint& wp = watchpoints_[i];
                if (!wp.active || (wp.flags & rw_bit) == 0u) continue;
                const uint64_t a_end = (addr <= UINT64_MAX - width) ? (addr + width) : UINT64_MAX;
                const uint64_t w_end = (wp.base <= UINT64_MAX - wp.size) ? (wp.base + wp.size) : UINT64_MAX;
                if (addr < w_end && a_end > wp.base) {
                    wp_hit = true;
                    wp_id = static_cast<uint16_t>(i);
                    ++watch_hits_;
                    last_watch_addr_ = addr;
                    if ((wp.flags & WatchFlags::kBreak) != 0u) {
                        watch_break_pending_ = true;
                    }
                    break;
                }
            }
        }
        if (!bus_trace_enabled_ && !wp_hit && !is_fault) return;
        const uint32_t seq = bus_trace_total_++;
        uint8_t fl = 0u;
        if (is_write) fl |= BusTraceFlags::kWrite;
        if (wp_hit) fl |= BusTraceFlags::kWatchpointHit;
        if (is_fault) fl |= BusTraceFlags::kFault;
        const BusTraceEvent ev{
            seq,
            static_cast<uint32_t>(addr & 0xFFFFFFFFu),
            static_cast<uint32_t>(addr >> 32u),
            static_cast<uint32_t>(val & 0xFFFFFFFFu),
            static_cast<uint32_t>(val >> 32u),
            width,
            fl,
            wp_id,
        };
        bus_trace_[static_cast<size_t>(seq) & (BusTraceCapacity - 1u)] = ev;
        if (bus_trace_cb_ != nullptr) {
            bus_trace_cb_(bus_trace_cb_ctx_, ev);
        }
    }

    [[nodiscard]] bool bus_read(uint64_t addr, uint8_t width, uint64_t& out) noexcept {
        if (!check_memory_access(addr, width, false)) {
            last_fault_addr_ = addr;
            record_bus_event(addr, width, 0u, false, true);
            return false;
        }
        if (bus_.read != nullptr) {
            const bool ok = bus_.read(bus_.ctx, addr, width, out);
            if (!ok) {
                last_fault_addr_ = addr;
                record_bus_event(addr, width, 0u, false, true);
                return false;
            }
            record_bus_event(addr, width, out, false, false);
            return true;
        }
        if (sizeof(uintptr_t) < 8u && addr > static_cast<uint64_t>(UINTPTR_MAX)) {
            last_fault_addr_ = addr;
            record_bus_event(addr, width, 0u, false, true);
            return false;
        }
        const auto p = static_cast<uintptr_t>(addr);
        memory_barrier();
        switch (width) {
            case 1u: out = *reinterpret_cast<const volatile uint8_t*>(p); break;
            case 2u: out = *reinterpret_cast<const volatile uint16_t*>(p); break;
            case 4u: out = *reinterpret_cast<const volatile uint32_t*>(p); break;
            case 8u: out = *reinterpret_cast<const volatile uint64_t*>(p); break;
            default: last_fault_addr_ = addr; record_bus_event(addr, width, 0u, false, true); return false;
        }
        memory_barrier();
        record_bus_event(addr, width, out, false, false);
        return true;
    }

    [[nodiscard]] bool bus_write(uint64_t addr, uint8_t width, uint64_t val) noexcept {
        if (!check_memory_access(addr, width, true)) {
            last_fault_addr_ = addr;
            record_bus_event(addr, width, val, true, true);
            return false;
        }
        if (bus_.write != nullptr) {
            const bool ok = bus_.write(bus_.ctx, addr, width, val);
            if (!ok) {
                last_fault_addr_ = addr;
                record_bus_event(addr, width, val, true, true);
                return false;
            }
            record_bus_event(addr, width, val, true, false);
            return true;
        }
        if (sizeof(uintptr_t) < 8u && addr > static_cast<uint64_t>(UINTPTR_MAX)) {
            last_fault_addr_ = addr;
            record_bus_event(addr, width, val, true, true);
            return false;
        }
        const auto p = static_cast<uintptr_t>(addr);
        memory_barrier();
        switch (width) {
            case 1u: *reinterpret_cast<volatile uint8_t*>(p) = static_cast<uint8_t>(val); break;
            case 2u: *reinterpret_cast<volatile uint16_t*>(p) = static_cast<uint16_t>(val); break;
            case 4u: *reinterpret_cast<volatile uint32_t*>(p) = static_cast<uint32_t>(val); break;
            case 8u: *reinterpret_cast<volatile uint64_t*>(p) = val; break;
            default: last_fault_addr_ = addr; record_bus_event(addr, width, val, true, true); return false;
        }
        memory_barrier();
        record_bus_event(addr, width, val, true, false);
        return true;
    }

    void bus_fence() noexcept {
        if (bus_.fence != nullptr) {
            bus_.fence(bus_.ctx);
        }
        memory_barrier();
    }

    [[nodiscard]] detail::ProtoHeader read_proto(uint16_t pid) const noexcept {
        const uint8_t* pd = image_ + protos_off_ + static_cast<size_t>(pid) * kProtoDescSize;
        return {
            detail::read_u32(pd + 0),
            detail::read_u32(pd + 4),
            detail::read_u32(pd + 8),
            detail::read_u16(pd + 12),
            detail::read_u16(pd + 14),
            detail::read_u16(pd + 16),
            detail::read_u16(pd + 18),
            pd[20],
            pd[21],
            pd[22],
        };
    }

    [[nodiscard]] detail::Instr read_instr(uint32_t index) const noexcept {
        const uint8_t* p = image_ + code_off_ + static_cast<size_t>(index) * kInstrSize;
        return {
            static_cast<Op>(p[0]),
            detail::read_i16(p + 2),
            detail::read_i16(p + 4),
            detail::read_i16(p + 6),
        };
    }

    [[nodiscard]] Value read_const(uint32_t index) const noexcept {
        const uint8_t* p = image_ + consts_off_ + static_cast<size_t>(index) * kConstDescSize;
        const auto kind = static_cast<ConstKind>(p[0]);
        const uint64_t payload = detail::read_u64(p + 8);
        switch (kind) {
            case ConstKind::Nil: return Value::nil();
            case ConstKind::Bool: return Value::from_bool(payload != 0u);
            case ConstKind::Int: return Value::from_int(static_cast<int64_t>(payload));
#if CAMEO_EMBED_ENABLE_FLOAT
            case ConstKind::Float: {
                Value v; v.kind = ValueKind::Float; v.bits_ = payload; return v;
            }
#endif
            case ConstKind::String: return Value::from_string_id(static_cast<uint32_t>(payload));
            default: return Value::nil();
        }
    }

    [[nodiscard]] StringView string_view_of(uint32_t sid) const noexcept {
        if (sid >= string_count_) return {};
        const uint8_t* sd = image_ + strings_off_ + static_cast<size_t>(sid) * kStringDescSize;
        const uint32_t off = detail::read_u32(sd + 0);
        const uint32_t len = detail::read_u32(sd + 4);
        return StringView(reinterpret_cast<const char*>(image_ + str_data_off_ + off), len);
    }

    [[nodiscard]] uint32_t string_hash(uint32_t sid) const noexcept {
        if (sid >= string_count_) return 0u;
        const uint8_t* sd = image_ + strings_off_ + static_cast<size_t>(sid) * kStringDescSize;
        return detail::read_u32(sd + 8);
    }

    [[nodiscard]] Status validate_proto_code(const detail::ProtoHeader& ph) const noexcept {
        const int32_t max_reg = static_cast<int32_t>(ph.max_stack);
        const int32_t k_len = static_cast<int32_t>(ph.const_count);
        const int32_t up_len = static_cast<int32_t>(ph.upval_count);
        const int32_t code_len = static_cast<int32_t>(ph.code_count);

        auto valid_reg = [&](int32_t r) noexcept { return r >= 0 && r < max_reg; };
        auto valid_k = [&](int32_t k) noexcept { return k >= 0 && k < k_len; };
        auto valid_rk = [&](int32_t rk) noexcept { return rk >= 0 ? (rk < max_reg) : ((~rk) < k_len); };
        auto valid_jmp = [&](int32_t pc, int32_t off) noexcept {
            const int32_t dst = pc + 1 + off;
            return dst >= 0 && dst <= code_len;
        };

        for (int32_t pc = 0; pc < code_len; ++pc) {
            const detail::Instr i = read_instr(ph.code_offset + static_cast<uint32_t>(pc));
            switch (i.op) {
                case Op::Move: case Op::Unm: case Op::Not: case Op::Len: case Op::BNot:
                    if (!valid_reg(i.a) || !valid_reg(i.b)) return Status::InvalidBytecode;
                    break;
                case Op::LoadK:
                    if (!valid_reg(i.a) || !valid_k(i.b)) return Status::InvalidBytecode;
                    break;
                case Op::LoadNil:
                    if (i.a < 0 || i.b < 0 || i.a + i.b >= max_reg) return Status::InvalidBytecode;
                    break;
                case Op::LoadBool:
                    if (!valid_reg(i.a)) return Status::InvalidBytecode;
                    if (i.c != 0 && pc + 2 > code_len) return Status::InvalidBytecode;
                    break;
                case Op::LoadInt:
                    if (!valid_reg(i.a)) return Status::InvalidBytecode;
                    break;
                case Op::GetUpval:
                    if (!valid_reg(i.a) || i.b < 0 || i.b >= up_len) return Status::InvalidBytecode;
                    break;
                case Op::SetUpval:
                    if (!valid_rk(i.a) || i.b < 0 || i.b >= up_len) return Status::InvalidBytecode;
                    break;
                case Op::NewBox:
                    if (!valid_reg(i.a)) return Status::InvalidBytecode;
                    break;
                case Op::GetBox:
                    if (!valid_reg(i.a) || !valid_reg(i.b)) return Status::InvalidBytecode;
                    break;
                case Op::SetBox:
                    if (!valid_reg(i.a) || !valid_rk(i.b)) return Status::InvalidBytecode;
                    break;
                case Op::GetGlobalSlot:
                    if (!valid_reg(i.a) || i.b < 0 || i.b >= static_cast<int32_t>(global_count_)) return Status::InvalidBytecode;
                    break;
                case Op::SetGlobalSlot:
                    if (!valid_rk(i.a) || i.b < 0 || i.b >= static_cast<int32_t>(global_count_)) return Status::InvalidBytecode;
                    break;
                case Op::LoadHostFn:
                    if (!valid_reg(i.a) || i.b < 0 || i.b >= static_cast<int32_t>(MaxHostFunctions)) return Status::InvalidBytecode;
                    break;
                case Op::LoadNamespace:
                    if (!valid_reg(i.a) || i.b < 0 || i.b > 4) return Status::InvalidBytecode;
                    break;
                case Op::LoadBuiltin:
                    if (!valid_reg(i.a) || i.b < 0) return Status::InvalidBytecode;
                    break;
                case Op::GetTable:
                    if (!valid_reg(i.a) || !valid_reg(i.b) || !valid_rk(i.c)) return Status::InvalidBytecode;
                    break;
                case Op::GetField:
                    if (!valid_reg(i.a) || !valid_reg(i.b) || !valid_k(i.c)) return Status::InvalidBytecode;
                    break;
                case Op::SetTable:
                    if (!valid_reg(i.a) || !valid_rk(i.b) || !valid_rk(i.c)) return Status::InvalidBytecode;
                    break;
                case Op::SetField:
                    if (!valid_reg(i.a) || !valid_k(i.b) || !valid_rk(i.c)) return Status::InvalidBytecode;
                    break;
                case Op::NewTable:
                    if (!valid_reg(i.a) || i.b < 0 || i.c < 0) return Status::InvalidBytecode;
                    break;
                case Op::Self:
                    if (!valid_reg(i.a) || !valid_reg(i.a + 1) || !valid_reg(i.b) || !valid_k(i.c)) return Status::InvalidBytecode;
                    break;
                case Op::Div: case Op::Pow:
#if !CAMEO_EMBED_ENABLE_FLOAT
                    return Status::FloatDisabled;
#else
                    if (!valid_reg(i.a) || !valid_rk(i.b) || !valid_rk(i.c)) return Status::InvalidBytecode;
                    break;
#endif
                case Op::Add: case Op::Sub: case Op::Mul:
                case Op::Mod: case Op::IDiv:
                case Op::BAnd: case Op::BOr: case Op::BXor: case Op::Shl: case Op::Shr:
                    if (!valid_reg(i.a) || !valid_rk(i.b) || !valid_rk(i.c)) return Status::InvalidBytecode;
                    break;
                case Op::Jmp:
                    if (!valid_jmp(pc, i.b)) return Status::InvalidBytecode;
                    break;
                case Op::JmpIf: case Op::JmpIfNot:
                    if (!valid_reg(i.a) || !valid_jmp(pc, i.b)) return Status::InvalidBytecode;
                    break;
                case Op::Eq: case Op::Lt: case Op::Le:
                    if (!valid_rk(i.b) || !valid_rk(i.c) || pc + 1 >= code_len) return Status::InvalidBytecode;
                    break;
                case Op::Call:
                    if (!valid_reg(i.a) || i.b < 0 || i.c < 0) return Status::InvalidBytecode;
                    if (i.b > 0 && i.a + i.b - 1 >= max_reg) return Status::InvalidBytecode;
                    if (i.c > 1 && i.a + i.c - 2 >= max_reg) return Status::InvalidBytecode;
                    break;
                case Op::TailCall:
                    if (!valid_reg(i.a) || i.b < 0) return Status::InvalidBytecode;
                    if (i.b > 0 && i.a + i.b - 1 >= max_reg) return Status::InvalidBytecode;
                    break;
                case Op::Return:
                    if (i.a < 0 || i.b < 0) return Status::InvalidBytecode;
                    if (i.b > 1 && i.a + i.b - 2 >= max_reg) return Status::InvalidBytecode;
                    break;
                case Op::ForPrep: case Op::ForLoop:
                    if (!valid_reg(i.a) || !valid_reg(i.a + 3) || !valid_jmp(pc, i.b)) return Status::InvalidBytecode;
                    break;
                case Op::SetList:
                    if (!valid_reg(i.a) || i.b < 0 || i.c < 0) return Status::InvalidBytecode;
                    if (i.b > 0 && i.a + i.b >= max_reg) return Status::InvalidBytecode;
                    break;
                case Op::Closure:
                    if (!valid_reg(i.a) || i.b < 0 || i.b >= static_cast<int32_t>(proto_count_)) return Status::InvalidBytecode;
                    break;
                case Op::IsKind:
                    if (!valid_reg(i.a) || !valid_reg(i.b) || i.c < 0 || i.c > 9) return Status::InvalidBytecode;
                    break;
                case Op::MathFn:
#if !CAMEO_EMBED_ENABLE_FLOAT
                    return Status::FloatDisabled;
#else
                    if (!valid_reg(i.a) || !valid_reg(i.b) || i.c < 0 || i.c > 22) return Status::InvalidBytecode;
                    break;
#endif
                default:
                    return Status::InvalidBytecode;
            }
        }
        return Status::Ok;
    }

    [[nodiscard]] bool alloc_box(Value initial, uint16_t& id_out) noexcept {
        if (boxes_used_ >= MaxBoxes) return false;
        id_out = boxes_used_++;
        boxes_[id_out] = initial;
        return true;
    }

    [[nodiscard]] bool alloc_closure(uint16_t proto_id, uint16_t& id_out) noexcept {
        const detail::ProtoHeader ph = read_proto(proto_id);
        if (closures_used_ >= MaxClosures) return false;
        if (static_cast<size_t>(closure_upvals_used_) + ph.upval_count > kMaxClosureUpvals) return false;
        id_out = closures_used_++;
        closures_[id_out] = {proto_id, closure_upvals_used_, ph.upval_count};
        closure_upvals_used_ = static_cast<uint16_t>(closure_upvals_used_ + ph.upval_count);
        return true;
    }

    [[nodiscard]] bool alloc_record(uint16_t& id_out) noexcept {
        if (records_used_ >= MaxRecords) return false;
        id_out = records_used_++;
        records_[id_out] = {0u, true};
        return true;
    }

    [[nodiscard]] bool values_equal(Value a, Value b) const noexcept {
        if (a.kind == b.kind) {
            switch (a.kind) {
                case ValueKind::Nil: return true;
                case ValueKind::Bool:
                case ValueKind::Int:
                case ValueKind::Ptr:
                case ValueKind::Record:
                case ValueKind::Closure:
                case ValueKind::HostFn:
                case ValueKind::Namespace:
                case ValueKind::BuiltinFn:
                    return a.bits_ == b.bits_;
#if CAMEO_EMBED_ENABLE_FLOAT
                case ValueKind::Float:
                    return a.as_float() == b.as_float();
#endif
                case ValueKind::String: {
                    const uint32_t sa = a.as_string_id();
                    const uint32_t sb = b.as_string_id();
                    if (sa == sb) return true;
                    if (string_hash(sa) != string_hash(sb)) return false;
                    return detail::eq_sv(string_view_of(sa), string_view_of(sb));
                }
                default: return a.bits_ == b.bits_;
            }
        }
#if CAMEO_EMBED_ENABLE_FLOAT
        if (a.kind == ValueKind::Int && b.kind == ValueKind::Float)
            return detail::int_float_eq(a.as_int(), b.as_float());
        if (a.kind == ValueKind::Float && b.kind == ValueKind::Int)
            return detail::int_float_eq(b.as_int(), a.as_float());
#endif
        return false;
    }

    [[nodiscard]] Status values_lt(Value a, Value b, bool& out) const noexcept {
        if (a.kind == ValueKind::Int && b.kind == ValueKind::Int) {
            out = a.as_int() < b.as_int();
            return Status::Ok;
        }
        if (a.kind == ValueKind::Ptr && b.kind == ValueKind::Ptr) {
            out = a.as_ptr() < b.as_ptr();
            return Status::Ok;
        }
#if CAMEO_EMBED_ENABLE_FLOAT
        if (a.kind == ValueKind::Int && b.kind == ValueKind::Float) {
            out = detail::int_lt_float(a.as_int(), b.as_float());
            return Status::Ok;
        }
        if (a.kind == ValueKind::Float) {
            if (b.kind == ValueKind::Float) { out = a.as_float() < b.as_float(); return Status::Ok; }
            if (b.kind == ValueKind::Int) { out = detail::float_lt_int(a.as_float(), b.as_int()); return Status::Ok; }
        }
#endif
        if (a.kind == ValueKind::String && b.kind == ValueKind::String) {
            out = detail::cmp_sv(string_view_of(a.as_string_id()), string_view_of(b.as_string_id())) < 0;
            return Status::Ok;
        }
        return Status::TypeError;
    }

    [[nodiscard]] Status values_le(Value a, Value b, bool& out) const noexcept {
        if (a.kind == ValueKind::Int && b.kind == ValueKind::Int) {
            out = a.as_int() <= b.as_int();
            return Status::Ok;
        }
        if (a.kind == ValueKind::Ptr && b.kind == ValueKind::Ptr) {
            out = a.as_ptr() <= b.as_ptr();
            return Status::Ok;
        }
#if CAMEO_EMBED_ENABLE_FLOAT
        if (a.kind == ValueKind::Int && b.kind == ValueKind::Float) {
            out = detail::int_le_float(a.as_int(), b.as_float());
            return Status::Ok;
        }
        if (a.kind == ValueKind::Float) {
            if (b.kind == ValueKind::Float) { out = a.as_float() <= b.as_float(); return Status::Ok; }
            if (b.kind == ValueKind::Int) { out = detail::float_le_int(a.as_float(), b.as_int()); return Status::Ok; }
        }
#endif
        if (a.kind == ValueKind::String && b.kind == ValueKind::String) {
            out = detail::cmp_sv(string_view_of(a.as_string_id()), string_view_of(b.as_string_id())) <= 0;
            return Status::Ok;
        }
        return Status::TypeError;
    }

    [[nodiscard]] Value normalize_key(Value k) const noexcept {
#if CAMEO_EMBED_ENABLE_FLOAT
        if (k.kind == ValueKind::Float) {
            int64_t i = 0;
            if (detail::float_to_int(k.as_float(), i)) return Value::from_int(i);
        }
#endif
        return k;
    }

    [[nodiscard]] Value record_get(uint16_t rid, Value key) const noexcept {
        if (rid >= records_used_ || !records_[rid].used || key.is_nil()) return Value::nil();
        key = normalize_key(key);
        uint16_t cur = records_[rid].first_entry;
        while (cur != 0u) {
            const RecordEntry& e = record_entries_[cur - 1u];
            if (e.used && values_equal(e.key, key)) return e.val;
            cur = e.next;
        }
        return Value::nil();
    }

    [[nodiscard]] bool record_get_by_sv(uint16_t rid, StringView key, Value& out) const noexcept {
        if (rid >= records_used_ || !records_[rid].used) return false;
        const uint32_t h = fnv1a32(key);
        uint16_t cur = records_[rid].first_entry;
        while (cur != 0u) {
            const RecordEntry& e = record_entries_[cur - 1u];
            if (e.used && e.key.is_string()) {
                const uint32_t sid = e.key.as_string_id();
                if (string_hash(sid) == h && detail::eq_sv(string_view_of(sid), key)) {
                    out = e.val;
                    return true;
                }
            }
            cur = e.next;
        }
        return false;
    }

    [[nodiscard]] Status record_set(uint16_t rid, Value key, Value val) noexcept {
        if (rid >= records_used_ || !records_[rid].used) return Status::TypeError;
        if (key.is_nil()) return Status::InvalidKey;
#if CAMEO_EMBED_ENABLE_FLOAT
        if (key.is_float() && detail::is_nan(key.as_float())) return Status::InvalidKey;
#endif
        key = normalize_key(key);

        uint16_t* link = &records_[rid].first_entry;
        uint16_t cur = *link;
        while (cur != 0u) {
            RecordEntry& e = record_entries_[cur - 1u];
            if (e.used && values_equal(e.key, key)) {
                if (val.is_nil()) {
                    e.used = false;
                    *link = e.next;
                    e.next = 0u;
                } else {
                    e.val = val;
                }
                return Status::Ok;
            }
            link = &e.next;
            cur = e.next;
        }
        if (val.is_nil()) return Status::Ok;

        uint16_t slot = 0xFFFFu;
        if (record_slots_used_ < MaxRecordSlots) {
            slot = record_slots_used_++;
        } else {
            for (uint16_t i = 0; i < MaxRecordSlots; ++i) {
                if (!record_entries_[i].used) { slot = i; break; }
            }
            if (slot == 0xFFFFu) return Status::RecordSlotPoolExhausted;
        }
        record_entries_[slot] = {key, val, records_[rid].first_entry, true};
        records_[rid].first_entry = static_cast<uint16_t>(slot + 1u);
        return Status::Ok;
    }

    [[nodiscard]] int64_t array_len(uint16_t rid) const noexcept {
        if (rid >= records_used_ || !records_[rid].used) return 0;
        int64_t n = 0;
        while (!record_get(rid, Value::from_int(n + 1)).is_nil()) {
            ++n;
        }
        return n;
    }

    [[nodiscard]] static Value resolve_ptr_method(StringView field) noexcept {
        if (detail::eq_sv(field, "addr")) return Value::from_builtin_fn(BuiltinId::PtrAddr);
        if (detail::eq_sv(field, "is_null")) return Value::from_builtin_fn(BuiltinId::PtrIsNull);
        if (detail::eq_sv(field, "is_aligned")) return Value::from_builtin_fn(BuiltinId::PtrIsAligned);
        if (detail::eq_sv(field, "align_up")) return Value::from_builtin_fn(BuiltinId::PtrAlignUp);
        if (detail::eq_sv(field, "add")) return Value::from_builtin_fn(BuiltinId::PtrAdd);
        if (detail::eq_sv(field, "sub")) return Value::from_builtin_fn(BuiltinId::PtrSub);
        if (detail::eq_sv(field, "diff")) return Value::from_builtin_fn(BuiltinId::PtrDiff);
        if (detail::eq_sv(field, "read8")) return Value::from_builtin_fn(BuiltinId::PtrRead8);
        if (detail::eq_sv(field, "read16")) return Value::from_builtin_fn(BuiltinId::PtrRead16);
        if (detail::eq_sv(field, "read32")) return Value::from_builtin_fn(BuiltinId::PtrRead32);
        if (detail::eq_sv(field, "read64")) return Value::from_builtin_fn(BuiltinId::PtrRead64);
        if (detail::eq_sv(field, "write8")) return Value::from_builtin_fn(BuiltinId::PtrWrite8);
        if (detail::eq_sv(field, "write16")) return Value::from_builtin_fn(BuiltinId::PtrWrite16);
        if (detail::eq_sv(field, "write32")) return Value::from_builtin_fn(BuiltinId::PtrWrite32);
        if (detail::eq_sv(field, "write64")) return Value::from_builtin_fn(BuiltinId::PtrWrite64);
        if (detail::eq_sv(field, "read8_at")) return Value::from_builtin_fn(BuiltinId::PtrRead8At);
        if (detail::eq_sv(field, "read16_at")) return Value::from_builtin_fn(BuiltinId::PtrRead16At);
        if (detail::eq_sv(field, "read32_at")) return Value::from_builtin_fn(BuiltinId::PtrRead32At);
        if (detail::eq_sv(field, "read64_at")) return Value::from_builtin_fn(BuiltinId::PtrRead64At);
        if (detail::eq_sv(field, "write8_at")) return Value::from_builtin_fn(BuiltinId::PtrWrite8At);
        if (detail::eq_sv(field, "write16_at")) return Value::from_builtin_fn(BuiltinId::PtrWrite16At);
        if (detail::eq_sv(field, "write32_at")) return Value::from_builtin_fn(BuiltinId::PtrWrite32At);
        if (detail::eq_sv(field, "write64_at")) return Value::from_builtin_fn(BuiltinId::PtrWrite64At);
        if (detail::eq_sv(field, "bits32_at")) return Value::from_builtin_fn(BuiltinId::PtrBits32At);
        if (detail::eq_sv(field, "rmw_bits32_at")) return Value::from_builtin_fn(BuiltinId::PtrRmwBits32At);
        return Value::nil();
    }

    [[nodiscard]] Value resolve_namespace_field(NamespaceId ns, StringView field) const noexcept {
        if (ns == NamespaceId::Math) {
            if (detail::eq_sv(field, "maxinteger")) return Value::from_int(INT64_MAX);
            if (detail::eq_sv(field, "mininteger")) return Value::from_int(INT64_MIN);
            if (detail::eq_sv(field, "abs")) return Value::from_builtin_fn(BuiltinId::Abs);
            if (detail::eq_sv(field, "floor")) return Value::from_builtin_fn(BuiltinId::Floor);
            if (detail::eq_sv(field, "ceil")) return Value::from_builtin_fn(BuiltinId::Ceil);
            if (detail::eq_sv(field, "min")) return Value::from_builtin_fn(BuiltinId::Min);
            if (detail::eq_sv(field, "max")) return Value::from_builtin_fn(BuiltinId::Max);
            if (detail::eq_sv(field, "fmod")) return Value::from_builtin_fn(BuiltinId::Fmod);
            if (detail::eq_sv(field, "tointeger")) return Value::from_builtin_fn(BuiltinId::ToInteger);
            if (detail::eq_sv(field, "ult")) return Value::from_builtin_fn(BuiltinId::Ult);
#if CAMEO_EMBED_ENABLE_FLOAT
            if (detail::eq_sv(field, "pi")) return Value::from_float(3.14159265358979323846);
            if (detail::eq_sv(field, "huge")) return Value::from_float(std::numeric_limits<double>::infinity());
            if (detail::eq_sv(field, "isnan")) return Value::from_builtin_fn(BuiltinId::IsNan);
            if (detail::eq_sv(field, "isinf")) return Value::from_builtin_fn(BuiltinId::IsInf);
            if (detail::eq_sv(field, "isfinite")) return Value::from_builtin_fn(BuiltinId::IsFinite);
            if (detail::eq_sv(field, "isclose")) return Value::from_builtin_fn(BuiltinId::IsClose);
            if (detail::eq_sv(field, "sin")) return Value::from_builtin_fn(BuiltinId::Sin);
            if (detail::eq_sv(field, "cos")) return Value::from_builtin_fn(BuiltinId::Cos);
            if (detail::eq_sv(field, "tan")) return Value::from_builtin_fn(BuiltinId::Tan);
            if (detail::eq_sv(field, "asin")) return Value::from_builtin_fn(BuiltinId::Asin);
            if (detail::eq_sv(field, "acos")) return Value::from_builtin_fn(BuiltinId::Acos);
            if (detail::eq_sv(field, "atan")) return Value::from_builtin_fn(BuiltinId::Atan2);
            if (detail::eq_sv(field, "sinh")) return Value::from_builtin_fn(BuiltinId::Sinh);
            if (detail::eq_sv(field, "cosh")) return Value::from_builtin_fn(BuiltinId::Cosh);
            if (detail::eq_sv(field, "tanh")) return Value::from_builtin_fn(BuiltinId::Tanh);
            if (detail::eq_sv(field, "exp")) return Value::from_builtin_fn(BuiltinId::Exp);
            if (detail::eq_sv(field, "log")) return Value::from_builtin_fn(BuiltinId::Log);
            if (detail::eq_sv(field, "log2")) return Value::from_builtin_fn(BuiltinId::Log2);
            if (detail::eq_sv(field, "log10")) return Value::from_builtin_fn(BuiltinId::Log10);
            if (detail::eq_sv(field, "sqrt")) return Value::from_builtin_fn(BuiltinId::Sqrt);
            if (detail::eq_sv(field, "cbrt")) return Value::from_builtin_fn(BuiltinId::Cbrt);
            if (detail::eq_sv(field, "deg")) return Value::from_builtin_fn(BuiltinId::Deg);
            if (detail::eq_sv(field, "rad")) return Value::from_builtin_fn(BuiltinId::Rad);
#endif
            return Value::nil();
        }
        if (ns == NamespaceId::Ptr) {
            if (detail::eq_sv(field, "of")) return Value::from_builtin_fn(BuiltinId::PtrOf);
            if (detail::eq_sv(field, "null")) return Value::from_builtin_fn(BuiltinId::PtrNull);
            return Value::nil();
        }
        if (ns == NamespaceId::Axi) {
            if (detail::eq_sv(field, "read8")) return Value::from_builtin_fn(BuiltinId::AxiRead8);
            if (detail::eq_sv(field, "read16")) return Value::from_builtin_fn(BuiltinId::AxiRead16);
            if (detail::eq_sv(field, "read32")) return Value::from_builtin_fn(BuiltinId::AxiRead32);
            if (detail::eq_sv(field, "read64")) return Value::from_builtin_fn(BuiltinId::AxiRead64);
            if (detail::eq_sv(field, "write8")) return Value::from_builtin_fn(BuiltinId::AxiWrite8);
            if (detail::eq_sv(field, "write16")) return Value::from_builtin_fn(BuiltinId::AxiWrite16);
            if (detail::eq_sv(field, "write32")) return Value::from_builtin_fn(BuiltinId::AxiWrite32);
            if (detail::eq_sv(field, "write64")) return Value::from_builtin_fn(BuiltinId::AxiWrite64);
            if (detail::eq_sv(field, "rmw32")) return Value::from_builtin_fn(BuiltinId::AxiRmw32);
            if (detail::eq_sv(field, "poll32")) return Value::from_builtin_fn(BuiltinId::AxiPoll32);
            if (detail::eq_sv(field, "burst_read32")) return Value::from_builtin_fn(BuiltinId::AxiBurstRead32);
            if (detail::eq_sv(field, "burst_write32")) return Value::from_builtin_fn(BuiltinId::AxiBurstWrite32);
            if (detail::eq_sv(field, "scratch_get32")) return Value::from_builtin_fn(BuiltinId::AxiScratchGet32);
            if (detail::eq_sv(field, "scratch_set32")) return Value::from_builtin_fn(BuiltinId::AxiScratchSet32);
            if (detail::eq_sv(field, "scratch_len")) return Value::from_builtin_fn(BuiltinId::AxiScratchLen);
            if (detail::eq_sv(field, "fence")) return Value::from_builtin_fn(BuiltinId::AxiFence);
            if (detail::eq_sv(field, "window_check")) return Value::from_builtin_fn(BuiltinId::AxiWindowCheck);
            if (detail::eq_sv(field, "watch")) return Value::from_builtin_fn(BuiltinId::AxiWatch);
            if (detail::eq_sv(field, "unwatch")) return Value::from_builtin_fn(BuiltinId::AxiUnwatch);
            if (detail::eq_sv(field, "watch_hits")) return Value::from_builtin_fn(BuiltinId::AxiWatchHits);
            if (detail::eq_sv(field, "trace_enable")) return Value::from_builtin_fn(BuiltinId::AxiTraceEnable);
            if (detail::eq_sv(field, "trace_count")) return Value::from_builtin_fn(BuiltinId::AxiTraceCount);
            return Value::nil();
        }
        if (ns == NamespaceId::Dbg) {
            if (detail::eq_sv(field, "bits")) return Value::from_builtin_fn(BuiltinId::DbgBits);
            if (detail::eq_sv(field, "set_bits")) return Value::from_builtin_fn(BuiltinId::DbgSetBits);
            if (detail::eq_sv(field, "bit")) return Value::from_builtin_fn(BuiltinId::DbgBit);
            if (detail::eq_sv(field, "popcount")) return Value::from_builtin_fn(BuiltinId::DbgPopcount);
            if (detail::eq_sv(field, "clz32")) return Value::from_builtin_fn(BuiltinId::DbgClz32);
            if (detail::eq_sv(field, "ctz32")) return Value::from_builtin_fn(BuiltinId::DbgCtz32);
            if (detail::eq_sv(field, "bswap32")) return Value::from_builtin_fn(BuiltinId::DbgBswap32);
            if (detail::eq_sv(field, "assert")) return Value::from_builtin_fn(BuiltinId::DbgAssert);
            if (detail::eq_sv(field, "expect_eq")) return Value::from_builtin_fn(BuiltinId::DbgExpectEq);
            if (detail::eq_sv(field, "hexdump")) {
                const auto hid = static_cast<uint16_t>(StandardHostId::DbgHexdump);
                if (hid < MaxHostFunctions && hosts_[hid].fn != nullptr)
                    return Value::from_host_fn(hid);
                return Value::from_builtin_fn(BuiltinId::DbgHexdumpBuiltin);
            }
            if (detail::eq_sv(field, "log_hex")) {
                const auto hid = static_cast<uint16_t>(StandardHostId::DbgLogHex);
                if (hid < MaxHostFunctions && hosts_[hid].fn != nullptr)
                    return Value::from_host_fn(hid);
                return Value::from_builtin_fn(BuiltinId::DbgLogHexBuiltin);
            }
            if (detail::eq_sv(field, "trace")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::DbgTrace));
            if (detail::eq_sv(field, "breakpoint")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::DbgBreakpoint));
            return Value::nil();
        }
        if (ns == NamespaceId::Rtos) {
            if (detail::eq_sv(field, "ticks")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::Ticks));
            if (detail::eq_sv(field, "sleep")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::Sleep));
            if (detail::eq_sv(field, "yield")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::Yield));
            if (detail::eq_sv(field, "log")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::Log));
            if (detail::eq_sv(field, "log_str")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::LogStr));
            if (detail::eq_sv(field, "watchdog_checkin")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::WatchdogCheckin));
            if (detail::eq_sv(field, "queue_send")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::QueueSend));
            if (detail::eq_sv(field, "queue_recv")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::QueueRecv));
            if (detail::eq_sv(field, "notify")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::Notify));
            if (detail::eq_sv(field, "gpio_read")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::GpioRead));
            if (detail::eq_sv(field, "gpio_write")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::GpioWrite));
            if (detail::eq_sv(field, "task_count")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::TaskCount));
            if (detail::eq_sv(field, "task_state")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::TaskState));
            if (detail::eq_sv(field, "task_priority")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::TaskPriority));
            if (detail::eq_sv(field, "task_stack_bytes")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::TaskStackBytes));
            if (detail::eq_sv(field, "task_guard_ok")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::TaskGuardOk));
            if (detail::eq_sv(field, "task_cycles")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::TaskCycles));
            if (detail::eq_sv(field, "queue_depth")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::QueueDepth));
            if (detail::eq_sv(field, "dma_state")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::DmaState));
            if (detail::eq_sv(field, "dma_transferred")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::DmaTransferred));
            if (detail::eq_sv(field, "dma_capacity")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::DmaCapacity));
            if (detail::eq_sv(field, "dma_generation")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::DmaGeneration));
            if (detail::eq_sv(field, "hwsched_status")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::HwSchedStatus));
            if (detail::eq_sv(field, "hwsched_ready_mask")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::HwSchedReadyMask));
            if (detail::eq_sv(field, "hwsched_fault_mask")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::HwSchedFaultMask));
            if (detail::eq_sv(field, "hwsched_seu_count")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::HwSchedSeuCount));
            if (detail::eq_sv(field, "hwsched_dispatches")) return Value::from_host_fn(static_cast<uint16_t>(StandardHostId::HwSchedDispatches));
            return Value::nil();
        }
        return Value::nil();
    }

#if CAMEO_EMBED_ENABLE_FLOAT
    [[nodiscard]] static Status eval_math_unary(BuiltinId fn, double x, double& out) noexcept {
#if CAMEO_EMBED_ENABLE_MATH
        constexpr double kPi = 3.14159265358979323846;
        switch (fn) {
            case BuiltinId::Sin: out = ::sin(x); return Status::Ok;
            case BuiltinId::Cos: out = ::cos(x); return Status::Ok;
            case BuiltinId::Tan: out = ::tan(x); return Status::Ok;
            case BuiltinId::Asin: out = ::asin(x); return Status::Ok;
            case BuiltinId::Acos: out = ::acos(x); return Status::Ok;
            case BuiltinId::Atan: out = ::atan2(x, 1.0); return Status::Ok;
            case BuiltinId::Sinh: out = ::sinh(x); return Status::Ok;
            case BuiltinId::Cosh: out = ::cosh(x); return Status::Ok;
            case BuiltinId::Tanh: out = ::tanh(x); return Status::Ok;
            case BuiltinId::Asinh: out = ::asinh(x); return Status::Ok;
            case BuiltinId::Acosh: out = ::acosh(x); return Status::Ok;
            case BuiltinId::Atanh: out = ::atanh(x); return Status::Ok;
            case BuiltinId::Exp: out = ::exp(x); return Status::Ok;
            case BuiltinId::Exp2: out = ::pow(2.0, x); return Status::Ok;
            case BuiltinId::Expm1: out = ::expm1(x); return Status::Ok;
            case BuiltinId::Log: out = ::log(x); return Status::Ok;
            case BuiltinId::Log2: out = ::log2(x); return Status::Ok;
            case BuiltinId::Log10: out = ::log10(x); return Status::Ok;
            case BuiltinId::Log1p: out = ::log1p(x); return Status::Ok;
            case BuiltinId::Sqrt: out = ::sqrt(x); return Status::Ok;
            case BuiltinId::Cbrt: out = ::cbrt(x); return Status::Ok;
            case BuiltinId::Deg: out = x * (180.0 / kPi); return Status::Ok;
            case BuiltinId::Rad: out = x * (kPi / 180.0); return Status::Ok;
            default: return Status::TypeError;
        }
#else
        (void)fn; (void)x; out = 0.0;
        return Status::MathDisabled;
#endif
    }
#endif // CAMEO_EMBED_ENABLE_FLOAT

    [[nodiscard]] static bool extract_addr(const Value* args, size_t nargs, size_t idx, uint64_t& addr_out) noexcept {
        if (idx >= nargs) return false;
        if (args[idx].is_ptr()) { addr_out = args[idx].as_ptr(); return true; }
        if (args[idx].is_int()) { addr_out = static_cast<uint64_t>(args[idx].as_int()); return true; }
        return false;
    }

    [[nodiscard]] Status eval_builtin_call(BuiltinId fn, const Value* args, size_t nargs,
                                           Value* out, size_t max_out, size_t& got_out,
                                           uint32_t& steps, uint32_t step_budget) noexcept {
        got_out = 0u;
        auto emit1 = [&](Value v) noexcept {
            if (max_out >= 1u) { out[0] = v; got_out = 1u; }
        };
        auto emit2 = [&](Value v0, Value v1) noexcept {
            if (max_out >= 1u) { out[0] = v0; got_out = 1u; }
            if (max_out >= 2u) { out[1] = v1; got_out = 2u; }
        };

        switch (fn) {
            // ---------------- First-class Ptr ----------------
            case BuiltinId::PtrOf: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr)) return Status::TypeError;
                emit1(Value::from_ptr(addr));
                return Status::Ok;
            }
            case BuiltinId::PtrNull:
                emit1(Value::from_ptr(0u));
                return Status::Ok;
            case BuiltinId::PtrAddr:
                if (nargs < 1 || !args[0].is_ptr()) return Status::TypeError;
                emit1(Value::from_int(static_cast<int64_t>(args[0].as_ptr())));
                return Status::Ok;
            case BuiltinId::PtrIsNull:
                if (nargs < 1 || !args[0].is_ptr()) return Status::TypeError;
                emit1(Value::from_bool(args[0].as_ptr() == 0u));
                return Status::Ok;
            case BuiltinId::PtrIsAligned: {
                if (nargs < 2 || !args[0].is_ptr() || !args[1].is_int()) return Status::TypeError;
                const int64_t a = args[1].as_int();
                const uint64_t ua = static_cast<uint64_t>(a);
                const bool ok = a > 0 && (ua & (ua - 1u)) == 0u && (args[0].as_ptr() & (ua - 1u)) == 0u;
                emit1(Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::PtrAlignUp: {
                if (nargs < 2 || !args[0].is_ptr() || !args[1].is_int()) return Status::TypeError;
                const int64_t a = args[1].as_int();
                const uint64_t ua = static_cast<uint64_t>(a);
                if (a <= 0 || (ua & (ua - 1u)) != 0u) return Status::TypeError;
                const uint64_t mask = ua - 1u;
                emit1(Value::from_ptr((args[0].as_ptr() + mask) & ~mask));
                return Status::Ok;
            }
            case BuiltinId::PtrAdd:
                if (nargs < 2 || !args[0].is_ptr() || !args[1].is_int()) return Status::TypeError;
                emit1(Value::from_ptr(args[0].as_ptr() + static_cast<uint64_t>(args[1].as_int())));
                return Status::Ok;
            case BuiltinId::PtrSub:
                if (nargs < 2 || !args[0].is_ptr() || !args[1].is_int()) return Status::TypeError;
                emit1(Value::from_ptr(args[0].as_ptr() - static_cast<uint64_t>(args[1].as_int())));
                return Status::Ok;
            case BuiltinId::PtrDiff:
                if (nargs < 2 || !args[0].is_ptr() || !args[1].is_ptr()) return Status::TypeError;
                emit1(Value::from_int(static_cast<int64_t>(args[0].as_ptr() - args[1].as_ptr())));
                return Status::Ok;
            case BuiltinId::PtrRead8:
            case BuiltinId::PtrRead16:
            case BuiltinId::PtrRead32:
            case BuiltinId::PtrRead64: {
                if (nargs < 1 || !args[0].is_ptr()) return Status::TypeError;
                const uint8_t width = (fn == BuiltinId::PtrRead8) ? 1u
                                    : (fn == BuiltinId::PtrRead16) ? 2u
                                    : (fn == BuiltinId::PtrRead32) ? 4u : 8u;
                uint64_t val = 0;
                if (!bus_read(args[0].as_ptr(), width, val)) return Status::MemoryFault;
                emit1(Value::from_int(static_cast<int64_t>(val)));
                return Status::Ok;
            }
            case BuiltinId::PtrWrite8:
            case BuiltinId::PtrWrite16:
            case BuiltinId::PtrWrite32:
            case BuiltinId::PtrWrite64: {
                if (nargs < 2 || !args[0].is_ptr() || !args[1].is_int()) return Status::TypeError;
                const uint8_t width = (fn == BuiltinId::PtrWrite8) ? 1u
                                    : (fn == BuiltinId::PtrWrite16) ? 2u
                                    : (fn == BuiltinId::PtrWrite32) ? 4u : 8u;
                const bool ok = bus_write(args[0].as_ptr(), width, static_cast<uint64_t>(args[1].as_int()));
                emit1(Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::PtrRead8At:
            case BuiltinId::PtrRead16At:
            case BuiltinId::PtrRead32At:
            case BuiltinId::PtrRead64At: {
                if (nargs < 2 || !args[0].is_ptr() || !args[1].is_int()) return Status::TypeError;
                const uint8_t width = (fn == BuiltinId::PtrRead8At) ? 1u
                                    : (fn == BuiltinId::PtrRead16At) ? 2u
                                    : (fn == BuiltinId::PtrRead32At) ? 4u : 8u;
                const uint64_t addr = args[0].as_ptr() + static_cast<uint64_t>(args[1].as_int());
                uint64_t val = 0;
                if (!bus_read(addr, width, val)) return Status::MemoryFault;
                emit1(Value::from_int(static_cast<int64_t>(val)));
                return Status::Ok;
            }
            case BuiltinId::PtrWrite8At:
            case BuiltinId::PtrWrite16At:
            case BuiltinId::PtrWrite32At:
            case BuiltinId::PtrWrite64At: {
                if (nargs < 3 || !args[0].is_ptr() || !args[1].is_int() || !args[2].is_int()) return Status::TypeError;
                const uint8_t width = (fn == BuiltinId::PtrWrite8At) ? 1u
                                    : (fn == BuiltinId::PtrWrite16At) ? 2u
                                    : (fn == BuiltinId::PtrWrite32At) ? 4u : 8u;
                const uint64_t addr = args[0].as_ptr() + static_cast<uint64_t>(args[1].as_int());
                const bool ok = bus_write(addr, width, static_cast<uint64_t>(args[2].as_int()));
                emit1(Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::PtrBits32At: {
                if (nargs < 4 || !args[0].is_ptr() || !args[1].is_int() ||
                    !args[2].is_int() || !args[3].is_int()) return Status::TypeError;
                const uint64_t addr = args[0].as_ptr() + static_cast<uint64_t>(args[1].as_int());
                const int64_t lsb = args[2].as_int();
                const int64_t width = args[3].as_int();
                uint64_t raw = 0;
                if (!bus_read(addr, 4u, raw)) return Status::MemoryFault;
                if (lsb < 0 || lsb >= 32 || width <= 0) { emit1(Value::from_int(0)); return Status::Ok; }
                const uint32_t shifted = static_cast<uint32_t>(raw) >> static_cast<unsigned>(lsb);
                const uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((uint32_t(1u) << static_cast<unsigned>(width)) - 1u);
                emit1(Value::from_int(static_cast<int64_t>(shifted & mask)));
                return Status::Ok;
            }
            case BuiltinId::PtrRmwBits32At: {
                if (nargs < 5 || !args[0].is_ptr() || !args[1].is_int() ||
                    !args[2].is_int() || !args[3].is_int() || !args[4].is_int()) return Status::TypeError;
                const uint64_t addr = args[0].as_ptr() + static_cast<uint64_t>(args[1].as_int());
                const int64_t lsb = args[2].as_int();
                const int64_t width = args[3].as_int();
                const uint32_t fv = static_cast<uint32_t>(args[4].as_int());
                uint64_t raw = 0;
                if (!bus_read(addr, 4u, raw)) { emit2(Value::from_int(0), Value::from_bool(false)); return Status::Ok; }
                if (lsb < 0 || lsb >= 32 || width <= 0) {
                    emit2(Value::from_int(static_cast<int64_t>(raw)), Value::from_bool(false));
                    return Status::Ok;
                }
                const uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((uint32_t(1u) << static_cast<unsigned>(width)) - 1u);
                const uint32_t smask = mask << static_cast<unsigned>(lsb);
                const uint32_t next = (static_cast<uint32_t>(raw) & ~smask) | ((fv & mask) << static_cast<unsigned>(lsb));
                if (!bus_write(addr, 4u, next)) { emit2(Value::from_int(0), Value::from_bool(false)); return Status::Ok; }
                emit2(Value::from_int(static_cast<int64_t>(next)), Value::from_bool(true));
                return Status::Ok;
            }

            // ---------------- Explicit axi.* MMIO / bus API ----------------
            case BuiltinId::AxiRead8:
            case BuiltinId::AxiRead16:
            case BuiltinId::AxiRead32:
            case BuiltinId::AxiRead64: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr)) return Status::TypeError;
                const uint8_t width = (fn == BuiltinId::AxiRead8) ? 1u
                                    : (fn == BuiltinId::AxiRead16) ? 2u
                                    : (fn == BuiltinId::AxiRead32) ? 4u : 8u;
                uint64_t val = 0;
                const bool ok = bus_read(addr, width, val);
                emit2(Value::from_int(ok ? static_cast<int64_t>(val) : 0), Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::AxiWrite8:
            case BuiltinId::AxiWrite16:
            case BuiltinId::AxiWrite32:
            case BuiltinId::AxiWrite64: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 2 || !args[1].is_int()) return Status::TypeError;
                const uint8_t width = (fn == BuiltinId::AxiWrite8) ? 1u
                                    : (fn == BuiltinId::AxiWrite16) ? 2u
                                    : (fn == BuiltinId::AxiWrite32) ? 4u : 8u;
                const bool ok = bus_write(addr, width, static_cast<uint64_t>(args[1].as_int()));
                emit1(Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::AxiRmw32: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 3 || !args[1].is_int() || !args[2].is_int())
                    return Status::TypeError;
                const uint32_t mask = static_cast<uint32_t>(args[1].as_int());
                const uint32_t val = static_cast<uint32_t>(args[2].as_int());
                uint64_t cur = 0;
                if (!bus_read(addr, 4u, cur)) {
                    emit2(Value::from_int(0), Value::from_bool(false));
                    return Status::Ok;
                }
                const uint32_t next = (static_cast<uint32_t>(cur) & ~mask) | (val & mask);
                if (!bus_write(addr, 4u, next)) {
                    emit2(Value::from_int(0), Value::from_bool(false));
                    return Status::Ok;
                }
                emit2(Value::from_int(static_cast<int64_t>(next)), Value::from_bool(true));
                return Status::Ok;
            }
            case BuiltinId::AxiPoll32: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 4 ||
                    !args[1].is_int() || !args[2].is_int() || !args[3].is_int())
                    return Status::TypeError;
                const uint32_t mask = static_cast<uint32_t>(args[1].as_int());
                const uint32_t expect = static_cast<uint32_t>(args[2].as_int());
                int64_t max_polls = args[3].as_int();
                if (max_polls < 1) max_polls = 1;
                uint32_t last = 0u;
                for (int64_t iter = 0; iter < max_polls; ++iter) {
                    if (iter > 0) {
                        if (steps >= step_budget) return Status::StepLimitExceeded;
                        if (abort_flag_ != nullptr && *abort_flag_) return Status::Aborted;
                        ++steps;
                    }
                    uint64_t raw = 0;
                    if (!bus_read(addr, 4u, raw)) {
                        emit2(Value::from_int(0), Value::from_bool(false));
                        return Status::Ok;
                    }
                    last = static_cast<uint32_t>(raw);
                    if ((last & mask) == (expect & mask)) {
                        emit2(Value::from_int(static_cast<int64_t>(last)), Value::from_bool(true));
                        return Status::Ok;
                    }
                }
                emit2(Value::from_int(static_cast<int64_t>(last)), Value::from_bool(false));
                return Status::Ok;
            }
            case BuiltinId::AxiBurstRead32: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 2 || !args[1].is_int())
                    return Status::TypeError;
                const int64_t count = args[1].as_int();
                const int64_t off = (nargs >= 3 && args[2].is_int()) ? args[2].as_int() : 0;
                if (count < 0 || off < 0 || static_cast<uint64_t>(off) > ScratchWords ||
                    static_cast<uint64_t>(count) > ScratchWords - static_cast<uint64_t>(off)) {
                    emit2(Value::from_int(0), Value::from_bool(false));
                    return Status::Ok;
                }
                for (int64_t i = 0; i < count; ++i) {
                    uint64_t raw = 0;
                    if (!bus_read(addr + static_cast<uint64_t>(i) * 4u, 4u, raw)) {
                        emit2(Value::from_int(i), Value::from_bool(false));
                        return Status::Ok;
                    }
                    scratch_[static_cast<size_t>(off + i)] = static_cast<uint32_t>(raw);
                }
                emit2(Value::from_int(count), Value::from_bool(true));
                return Status::Ok;
            }
            case BuiltinId::AxiBurstWrite32: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 2 || !args[1].is_int())
                    return Status::TypeError;
                const int64_t count = args[1].as_int();
                const int64_t off = (nargs >= 3 && args[2].is_int()) ? args[2].as_int() : 0;
                if (count < 0 || off < 0 || static_cast<uint64_t>(off) > ScratchWords ||
                    static_cast<uint64_t>(count) > ScratchWords - static_cast<uint64_t>(off)) {
                    emit2(Value::from_int(0), Value::from_bool(false));
                    return Status::Ok;
                }
                for (int64_t i = 0; i < count; ++i) {
                    if (!bus_write(addr + static_cast<uint64_t>(i) * 4u, 4u,
                                   scratch_[static_cast<size_t>(off + i)])) {
                        emit2(Value::from_int(i), Value::from_bool(false));
                        return Status::Ok;
                    }
                }
                emit2(Value::from_int(count), Value::from_bool(true));
                return Status::Ok;
            }
            case BuiltinId::AxiScratchGet32: {
                if (nargs < 1 || !args[0].is_int()) return Status::TypeError;
                const int64_t idx = args[0].as_int();
                const uint32_t v = (idx >= 0 && static_cast<size_t>(idx) < ScratchWords)
                    ? scratch_[static_cast<size_t>(idx)] : 0u;
                emit1(Value::from_int(static_cast<int64_t>(v)));
                return Status::Ok;
            }
            case BuiltinId::AxiScratchSet32: {
                if (nargs < 2 || !args[0].is_int() || !args[1].is_int()) return Status::TypeError;
                const int64_t idx = args[0].as_int();
                if (idx < 0 || static_cast<size_t>(idx) >= ScratchWords) {
                    emit1(Value::from_bool(false));
                    return Status::Ok;
                }
                scratch_[static_cast<size_t>(idx)] = static_cast<uint32_t>(args[1].as_int());
                emit1(Value::from_bool(true));
                return Status::Ok;
            }
            case BuiltinId::AxiScratchLen:
                emit1(Value::from_int(static_cast<int64_t>(ScratchWords)));
                return Status::Ok;
            case BuiltinId::AxiFence:
                bus_fence();
                return Status::Ok;
            case BuiltinId::AxiWindowCheck: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 3 || !args[1].is_int())
                    return Status::TypeError;
                const int64_t sz = args[1].as_int();
                const bool wr = args[2].is_truthy();
                const bool ok = (sz >= 1 && sz <= 8) &&
                                check_memory_access(addr, static_cast<uint8_t>(sz), wr);
                emit1(Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::AxiWatch: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 3 || !args[1].is_int() || !args[2].is_int())
                    return Status::TypeError;
                const int64_t sz = args[1].as_int();
                const uint8_t fl = static_cast<uint8_t>(args[2].as_int() & 0x7);
                const int id = (sz > 0) ? add_watchpoint(addr, static_cast<uint64_t>(sz), fl) : -1;
                emit1(Value::from_int(id));
                return Status::Ok;
            }
            case BuiltinId::AxiUnwatch: {
                if (nargs < 1 || !args[0].is_int()) return Status::TypeError;
                const int64_t id = args[0].as_int();
                const bool ok = id >= 0 && remove_watchpoint(static_cast<size_t>(id));
                emit1(Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::AxiWatchHits:
                emit1(Value::from_int(static_cast<int64_t>(watch_hits_)));
                return Status::Ok;
            case BuiltinId::AxiTraceEnable: {
                if (nargs < 1) return Status::TypeError;
                set_bus_trace_enabled(args[0].is_truthy());
                emit1(Value::from_bool(bus_trace_enabled_));
                return Status::Ok;
            }
            case BuiltinId::AxiTraceCount:
                emit1(Value::from_int(static_cast<int64_t>(bus_trace_count())));
                return Status::Ok;

            // ---------------- Hardware debugging dbg.* ----------------
            case BuiltinId::DbgBits: {
                if (nargs < 3 || !args[0].is_int() || !args[1].is_int() || !args[2].is_int())
                    return Status::TypeError;
                const uint64_t val = static_cast<uint64_t>(args[0].as_int());
                const int64_t lsb = args[1].as_int();
                const int64_t width = args[2].as_int();
                if (lsb < 0 || lsb >= 64 || width <= 0) { emit1(Value::from_int(0)); return Status::Ok; }
                const uint64_t shifted = val >> static_cast<unsigned>(lsb);
                const uint64_t mask = (width >= 64) ? ~uint64_t(0u) : ((uint64_t(1u) << static_cast<unsigned>(width)) - 1u);
                emit1(Value::from_int(static_cast<int64_t>(shifted & mask)));
                return Status::Ok;
            }
            case BuiltinId::DbgSetBits: {
                if (nargs < 4 || !args[0].is_int() || !args[1].is_int() || !args[2].is_int() || !args[3].is_int())
                    return Status::TypeError;
                const uint64_t val = static_cast<uint64_t>(args[0].as_int());
                const int64_t lsb = args[1].as_int();
                const int64_t width = args[2].as_int();
                const uint64_t fv = static_cast<uint64_t>(args[3].as_int());
                if (lsb < 0 || lsb >= 64 || width <= 0) { emit1(args[0]); return Status::Ok; }
                const uint64_t mask = (width >= 64) ? ~uint64_t(0u) : ((uint64_t(1u) << static_cast<unsigned>(width)) - 1u);
                const uint64_t smask = mask << static_cast<unsigned>(lsb);
                const uint64_t res = (val & ~smask) | ((fv & mask) << static_cast<unsigned>(lsb));
                emit1(Value::from_int(static_cast<int64_t>(res)));
                return Status::Ok;
            }
            case BuiltinId::DbgBit: {
                if (nargs < 2 || !args[0].is_int() || !args[1].is_int()) return Status::TypeError;
                const int64_t idx = args[1].as_int();
                const bool set = idx >= 0 && idx < 64 &&
                                 ((static_cast<uint64_t>(args[0].as_int()) >> static_cast<unsigned>(idx)) & 1u) != 0u;
                emit1(Value::from_bool(set));
                return Status::Ok;
            }
            case BuiltinId::DbgPopcount:
                if (nargs < 1 || !args[0].is_int()) return Status::TypeError;
                emit1(Value::from_int(detail::popcount64(static_cast<uint64_t>(args[0].as_int()))));
                return Status::Ok;
            case BuiltinId::DbgClz32:
                if (nargs < 1 || !args[0].is_int()) return Status::TypeError;
                emit1(Value::from_int(detail::clz32(static_cast<uint32_t>(args[0].as_int()))));
                return Status::Ok;
            case BuiltinId::DbgCtz32:
                if (nargs < 1 || !args[0].is_int()) return Status::TypeError;
                emit1(Value::from_int(detail::ctz32(static_cast<uint32_t>(args[0].as_int()))));
                return Status::Ok;
            case BuiltinId::DbgBswap32:
                if (nargs < 1 || !args[0].is_int()) return Status::TypeError;
                emit1(Value::from_int(static_cast<int64_t>(detail::bswap32(static_cast<uint32_t>(args[0].as_int())))));
                return Status::Ok;
            case BuiltinId::DbgAssert: {
                if (nargs < 1) return Status::TypeError;
                const int64_t code = (nargs >= 2 && args[1].is_int()) ? args[1].as_int() : 0;
                if (!args[0].is_truthy()) {
                    last_assert_code_ = code;
                    return Status::AssertionFailed;
                }
                emit1(Value::from_bool(true));
                return Status::Ok;
            }
            case BuiltinId::DbgExpectEq: {
                if (nargs < 2 || !args[0].is_int() || !args[1].is_int()) return Status::TypeError;
                const int64_t actual = args[0].as_int();
                const int64_t expected = args[1].as_int();
                const int64_t code = (nargs >= 3 && args[2].is_int()) ? args[2].as_int() : 0;
                if (actual != expected) {
                    last_assert_code_ = code;
                    last_expect_actual_ = actual;
                    last_expect_expected_ = expected;
                    return Status::AssertionFailed;
                }
                emit1(Value::from_bool(true));
                return Status::Ok;
            }
            case BuiltinId::DbgLogHexBuiltin: {
                if (nargs < 2 || !args[0].is_string() || !args[1].is_int()) return Status::TypeError;
                bool ok = false;
                if (debug_writer_ != nullptr) {
                    const StringView tag = string_view_of(args[0].as_string_id());
                    const uint64_t val = static_cast<uint64_t>(args[1].as_int());
                    char line[96]{};
                    size_t pos = 0;
                    for (size_t i = 0; i < tag.size() && pos + 20u < sizeof(line); ++i) line[pos++] = tag[i];
                    line[pos++] = '=';
                    line[pos++] = '0';
                    line[pos++] = 'x';
                    const size_t nibbles = (val > 0xFFFFFFFFULL) ? 16u : 8u;
                    detail::write_hex_digits(line + pos, val, nibbles);
                    pos += nibbles;
                    line[pos++] = '\n';
                    ok = debug_writer_(debug_writer_ctx_, line, pos);
                }
                emit1(Value::from_bool(ok));
                return Status::Ok;
            }
            case BuiltinId::DbgHexdumpBuiltin: {
                uint64_t addr = 0;
                if (!extract_addr(args, nargs, 0, addr) || nargs < 2 || !args[1].is_int())
                    return Status::TypeError;
                int64_t len = args[1].as_int();
                if (len <= 0) { emit1(Value::from_int(0)); return Status::Ok; }
                if (len > 256) len = 256;
                int64_t dumped = 0;
                for (int64_t row = 0; row < len; row += 16) {
                    char line[80]{};
                    size_t pos = 0;
                    line[pos++] = '0';
                    line[pos++] = 'x';
                    detail::write_hex_digits(line + pos, addr + static_cast<uint64_t>(row), 8u);
                    pos += 8u;
                    line[pos++] = ':';
                    const int64_t row_end = (row + 16 < len) ? (row + 16) : len;
                    bool any = false;
                    for (int64_t i = row; i < row_end; ++i) {
                        uint64_t b = 0;
                        if (!bus_read(addr + static_cast<uint64_t>(i), 1u, b)) break;
                        line[pos++] = ' ';
                        detail::write_hex_digits(line + pos, b, 2u);
                        pos += 2u;
                        ++dumped;
                        any = true;
                    }
                    if (!any) break;
                    line[pos++] = '\n';
                    if (debug_writer_ != nullptr) {
                        (void)debug_writer_(debug_writer_ctx_, line, pos);
                    }
                }
                emit1(Value::from_int(dumped));
                return Status::Ok;
            }

            // ---------------- Pure integer + optional float math.* ----------------
            case BuiltinId::Abs: {
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                if (args[0].is_int()) {
                    const int64_t v = args[0].as_int();
                    emit1(Value::from_int(v < 0 ? detail::wrap_neg(v) : v));
                    return Status::Ok;
                }
#if CAMEO_EMBED_ENABLE_FLOAT
                const double d = args[0].as_float();
                emit1(Value::from_float(d < 0.0 ? -d : d));
                return Status::Ok;
#else
                return Status::FloatDisabled;
#endif
            }
            case BuiltinId::Floor: {
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                if (args[0].is_int()) { emit1(args[0]); return Status::Ok; }
#if CAMEO_EMBED_ENABLE_FLOAT
                const double f = detail::floor_d(args[0].as_float());
                int64_t iv = 0;
                emit1(detail::float_to_int(f, iv) ? Value::from_int(iv) : Value::from_float(f));
                return Status::Ok;
#else
                return Status::FloatDisabled;
#endif
            }
            case BuiltinId::Ceil: {
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                if (args[0].is_int()) { emit1(args[0]); return Status::Ok; }
#if CAMEO_EMBED_ENABLE_FLOAT
                const double c = detail::ceil_d(args[0].as_float());
                int64_t iv = 0;
                emit1(detail::float_to_int(c, iv) ? Value::from_int(iv) : Value::from_float(c));
                return Status::Ok;
#else
                return Status::FloatDisabled;
#endif
            }
            case BuiltinId::Min: {
                if (nargs < 1) return Status::TypeError;
                Value best = args[0];
                for (size_t i = 1; i < nargs; ++i) {
                    bool lt = false;
                    const Status st = values_lt(args[i], best, lt);
                    if (st != Status::Ok) return st;
                    if (lt) best = args[i];
                }
                emit1(best);
                return Status::Ok;
            }
            case BuiltinId::Max: {
                if (nargs < 1) return Status::TypeError;
                Value best = args[0];
                for (size_t i = 1; i < nargs; ++i) {
                    bool lt = false;
                    const Status st = values_lt(best, args[i], lt);
                    if (st != Status::Ok) return st;
                    if (lt) best = args[i];
                }
                emit1(best);
                return Status::Ok;
            }
            case BuiltinId::Fmod: {
                if (nargs < 2 || !args[0].is_number() || !args[1].is_number()) return Status::TypeError;
                if (args[0].is_int() && args[1].is_int()) {
                    const int64_t y = args[1].as_int();
                    if (y == 0) return Status::DivisionByZero;
                    emit1(Value::from_int(y == -1 ? 0 : (args[0].as_int() % y)));
                    return Status::Ok;
                }
#if CAMEO_EMBED_ENABLE_MATH
                emit1(Value::from_float(::fmod(args[0].as_double(), args[1].as_double())));
                return Status::Ok;
#else
                return Status::MathDisabled;
#endif
            }
            case BuiltinId::ToInteger: {
                if (nargs < 1) { emit1(Value::nil()); return Status::Ok; }
                if (args[0].is_int()) { emit1(args[0]); return Status::Ok; }
#if CAMEO_EMBED_ENABLE_FLOAT
                if (args[0].is_float()) {
                    int64_t iv = 0;
                    emit1(detail::float_to_int(args[0].as_float(), iv) ? Value::from_int(iv) : Value::nil());
                    return Status::Ok;
                }
#endif
                emit1(Value::nil());
                return Status::Ok;
            }
            case BuiltinId::Ult: {
                if (nargs < 2 || !args[0].is_int() || !args[1].is_int()) return Status::TypeError;
                emit1(Value::from_bool(static_cast<uint64_t>(args[0].as_int()) <
                                       static_cast<uint64_t>(args[1].as_int())));
                return Status::Ok;
            }
#if CAMEO_EMBED_ENABLE_FLOAT
            case BuiltinId::IsNan:
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                emit1(Value::from_bool(args[0].is_float() && detail::is_nan(args[0].as_float())));
                return Status::Ok;
            case BuiltinId::IsInf:
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                emit1(Value::from_bool(args[0].is_float() && detail::is_inf(args[0].as_float())));
                return Status::Ok;
            case BuiltinId::IsFinite:
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                emit1(Value::from_bool(args[0].is_int() || detail::is_finite(args[0].as_float())));
                return Status::Ok;
            case BuiltinId::IsClose: {
                if (nargs < 2 || !args[0].is_number() || !args[1].is_number()) return Status::TypeError;
                const double a = args[0].as_double();
                const double b = args[1].as_double();
                const double rel = (nargs >= 3 && args[2].is_number()) ? args[2].as_double() : 1e-9;
                const double abs_t = (nargs >= 4 && args[3].is_number()) ? args[3].as_double() : 0.0;
                if (a == b) { emit1(Value::from_bool(true)); return Status::Ok; }
                if (!detail::is_finite(a) || !detail::is_finite(b)) { emit1(Value::from_bool(false)); return Status::Ok; }
                const double diff = (a > b) ? (a - b) : (b - a);
                const double ma = a < 0 ? -a : a;
                const double mb = b < 0 ? -b : b;
                const double lim = (rel * (ma > mb ? ma : mb)) > abs_t ? (rel * (ma > mb ? ma : mb)) : abs_t;
                emit1(Value::from_bool(diff <= lim));
                return Status::Ok;
            }
            case BuiltinId::Atan2: {
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                const double y = args[0].as_double();
                const double x = (nargs >= 2 && args[1].is_number()) ? args[1].as_double() : 1.0;
#if CAMEO_EMBED_ENABLE_MATH
                emit1(Value::from_float(::atan2(y, x)));
                return Status::Ok;
#else
                (void)y; (void)x;
                return Status::MathDisabled;
#endif
            }
            default: {
                if (nargs < 1 || !args[0].is_number()) return Status::TypeError;
                double r = 0.0;
                const Status st = eval_math_unary(fn, args[0].as_double(), r);
                if (st != Status::Ok) return st;
                emit1(Value::from_float(r));
                return Status::Ok;
            }
#else
            default:
                return Status::FloatDisabled;
#endif
        }
    }

    [[nodiscard]] static bool for_limit_int(Value limit, int64_t step, int64_t start, int64_t& lim_out) noexcept {
        if (limit.kind == ValueKind::Int) {
            lim_out = limit.as_int();
        }
#if CAMEO_EMBED_ENABLE_FLOAT
        else if (limit.kind == ValueKind::Float) {
            const double fl = limit.as_float();
            if (detail::is_nan(fl)) return false;
            if (step > 0) {
                if (fl < -detail::kTwoPow63) return false;
                if (fl >= detail::kTwoPow63) lim_out = std::numeric_limits<int64_t>::max();
                else lim_out = static_cast<int64_t>(detail::floor_d(fl));
            } else {
                if (fl >= detail::kTwoPow63) return false;
                if (fl <= -detail::kTwoPow63) lim_out = std::numeric_limits<int64_t>::min();
                else lim_out = static_cast<int64_t>(detail::ceil_d(fl));
            }
        }
#endif
        else {
            return false;
        }
        return step > 0 ? (start <= lim_out) : (lim_out <= start);
    }

    [[nodiscard]] Status op_for_prep(size_t ra, bool& skip_out) noexcept {
        Value& init = stack_[ra];
        Value& limit = stack_[ra + 1];
        Value& step = stack_[ra + 2];
        if (init.kind == ValueKind::Int && step.kind == ValueKind::Int) {
            const int64_t st = step.as_int();
            if (st == 0) return Status::InvalidStep;
            const int64_t start = init.as_int();
            if (!limit.is_number()) return Status::TypeError;
            int64_t lim = 0;
            if (!for_limit_int(limit, st, start, lim)) {
                skip_out = true;
                return Status::Ok;
            }
            const uint64_t count = st > 0
                ? (static_cast<uint64_t>(lim) - static_cast<uint64_t>(start)) / static_cast<uint64_t>(st)
                : (static_cast<uint64_t>(start) - static_cast<uint64_t>(lim)) /
                  (static_cast<uint64_t>(-(st + 1)) + 1u);
            limit = Value::from_int(static_cast<int64_t>(count));
            stack_[ra + 3] = init;
            skip_out = false;
            return Status::Ok;
        }
#if CAMEO_EMBED_ENABLE_FLOAT
        if (!init.is_number() || !limit.is_number() || !step.is_number()) return Status::TypeError;
        const double finit = init.as_double();
        const double flim = limit.as_double();
        const double fstep = step.as_double();
        if (fstep == 0.0) return Status::InvalidStep;
        if (fstep > 0.0 ? (flim < finit) : (finit < flim)) {
            skip_out = true;
            return Status::Ok;
        }
        init = Value::from_float(finit);
        limit = Value::from_float(flim);
        step = Value::from_float(fstep);
        stack_[ra + 3] = init;
        skip_out = false;
        return Status::Ok;
#else
        return Status::TypeError;
#endif
    }

    [[nodiscard]] static bool eval_is_kind(Value v, int16_t test) noexcept {
        switch (test) {
            case 0: return v.kind == ValueKind::Nil;
            case 1: return v.kind == ValueKind::Bool;
            case 2: return v.kind == ValueKind::Int || v.kind == ValueKind::Float;
            case 3: return v.kind == ValueKind::Int;
            case 4: return v.kind == ValueKind::Float;
            case 5: return v.kind == ValueKind::String;
            case 6: return v.kind == ValueKind::Record || v.kind == ValueKind::Namespace;
            case 7: return v.kind == ValueKind::Closure || v.kind == ValueKind::HostFn || v.kind == ValueKind::BuiltinFn;
            case 8: return v.kind == ValueKind::Ptr; // userdata
            default: return false;
        }
    }

    [[nodiscard]] Status prepare_closure_frame(uint16_t entry_cl, Span<const Value> args) noexcept {
        if (entry_cl >= closures_used_) return Status::InvalidBytecode;
        const uint16_t entry_pid = closures_[entry_cl].proto_id;
        const detail::ProtoHeader entry_ph = read_proto(entry_pid);
        if (entry_ph.max_stack > StackSlots) return Status::ValueStackOverflow;

        for (size_t i = 0; i < entry_ph.max_stack; ++i) stack_[i] = Value::nil();
        const size_t copy_n = args.size() < entry_ph.num_params ? args.size() : entry_ph.num_params;
        for (size_t i = 0; i < copy_n; ++i) stack_[i] = args[i];

        active_frame_count_ = 1u;
        frames_[0] = {entry_cl, entry_pid, 0u, 0u, -1, 0u};
        active_top_ = entry_ph.max_stack;
        paused_ = true;
        skip_bp_once_ = false;
        watch_break_pending_ = false;
        return Status::Ok;
    }

    [[nodiscard]] ExecResult invoke_closure(uint16_t entry_cl,
                                            Span<const Value> args,
                                            Span<Value> out_results,
                                            uint32_t step_budget) noexcept {
        const Status pst = prepare_closure_frame(entry_cl, args);
        if (pst != Status::Ok) return {pst, 0, 0, 0, 0};
        return resume_frames(out_results, step_budget, false);
    }

    [[nodiscard]] ExecResult resume_frames(Span<Value> out_results,
                                           uint32_t step_budget,
                                           bool single_step) noexcept {
        size_t& frame_count = active_frame_count_;
        size_t& top = active_top_;
        uint32_t steps = 0u;
        size_t final_res_count = 0u;

        next_frame:
        while (frame_count > 0u) {
            Frame& fr = frames_[frame_count - 1u];
            const detail::ProtoHeader ph = read_proto(fr.proto_id);
            const ClosureSlot& cl = closures_[fr.closure_id];
            const size_t base = fr.base;

            auto rk = [&](int16_t operand) noexcept -> Value {
                return operand >= 0 ? stack_[base + static_cast<size_t>(operand)]
                                    : read_const(ph.const_offset + static_cast<uint32_t>(~operand));
            };

            for (;;) {
                if (watch_break_pending_) {
                    watch_break_pending_ = false;
                    paused_ = true;
                    return {Status::WatchpointHit, steps, 0, fr.proto_id, fr.pc};
                }
                if (single_step && steps >= 1u) {
                    paused_ = true;
                    return {Status::Stepped, steps, 0, fr.proto_id, fr.pc};
                }
                if (fr.pc >= ph.code_count) {
                    --frame_count;
                    if (frame_count == 0u) {
                        paused_ = false;
                        skip_bp_once_ = false;
                        final_res_count = 0u;
                        return {Status::Ok, steps, final_res_count, ph.name_str_id, 0u};
                    }
                    const int16_t want = fr.want_results;
                    if (want > 0) {
                        for (int16_t j = 0; j < want; ++j) stack_[fr.ret_dest + static_cast<size_t>(j)] = Value::nil();
                    } else if (want < 0) {
                        top = fr.ret_dest;
                    }
                    goto next_frame;
                }
                if (!single_step && has_breakpoint_at(fr.proto_id, fr.pc)) {
                    if (skip_bp_once_) {
                        skip_bp_once_ = false;
                    } else {
                        paused_ = true;
                        skip_bp_once_ = true;
                        return {Status::Breakpoint, steps, 0, fr.proto_id, fr.pc};
                    }
                } else {
                    skip_bp_once_ = false;
                }

                if (steps >= step_budget) {
                    paused_ = false;
                    frame_count = 0u;
                    return {Status::StepLimitExceeded, steps, 0, fr.proto_id, fr.pc};
                }
                if (abort_flag_ != nullptr && *abort_flag_) {
                    paused_ = false;
                    frame_count = 0u;
                    return {Status::Aborted, steps, 0, fr.proto_id, fr.pc};
                }
                ++steps;

                const uint32_t cur_pc = fr.pc;
                const detail::Instr i = read_instr(ph.code_offset + fr.pc++);

                auto fail = [&](Status st) noexcept -> ExecResult {
                    paused_ = false;
                    frame_count = 0u;
                    return {st, steps, 0, fr.proto_id, cur_pc};
                };

                switch (i.op) {
                    case Op::Move:
                        stack_[base + static_cast<size_t>(i.a)] = stack_[base + static_cast<size_t>(i.b)];
                        continue;
                    case Op::LoadK:
                        stack_[base + static_cast<size_t>(i.a)] = read_const(ph.const_offset + static_cast<uint32_t>(i.b));
                        continue;
                    case Op::LoadNil:
                        for (int16_t k = 0; k <= i.b; ++k)
                            stack_[base + static_cast<size_t>(i.a + k)] = Value::nil();
                        continue;
                    case Op::LoadBool:
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_bool(i.b != 0);
                        if (i.c != 0) ++fr.pc;
                        continue;
                    case Op::LoadInt:
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_int(static_cast<int64_t>(i.b));
                        continue;
                    case Op::GetUpval: {
                        const uint16_t box_id = closure_upvals_[cl.upval_offset + static_cast<size_t>(i.b)];
                        stack_[base + static_cast<size_t>(i.a)] = boxes_[box_id];
                        continue;
                    }
                    case Op::SetUpval: {
                        const uint16_t box_id = closure_upvals_[cl.upval_offset + static_cast<size_t>(i.b)];
                        boxes_[box_id] = rk(i.a);
                        continue;
                    }
                    case Op::NewBox: {
                        uint16_t bid = 0;
                        if (!alloc_box(stack_[base + static_cast<size_t>(i.a)], bid))
                            return fail(Status::BoxPoolExhausted);
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_box(bid);
                        continue;
                    }
                    case Op::GetBox: {
                        const Value bv = stack_[base + static_cast<size_t>(i.b)];
                        if (bv.kind != ValueKind::Box) return fail(Status::TypeError);
                        stack_[base + static_cast<size_t>(i.a)] = boxes_[bv.as_handle()];
                        continue;
                    }
                    case Op::SetBox: {
                        const Value bv = stack_[base + static_cast<size_t>(i.a)];
                        if (bv.kind != ValueKind::Box) return fail(Status::TypeError);
                        boxes_[bv.as_handle()] = rk(i.b);
                        continue;
                    }
                    case Op::GetGlobalSlot:
                        stack_[base + static_cast<size_t>(i.a)] = globals_[static_cast<size_t>(i.b)];
                        continue;
                    case Op::SetGlobalSlot:
                        globals_[static_cast<size_t>(i.b)] = rk(i.a);
                        continue;
                    case Op::LoadHostFn:
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_host_fn(static_cast<uint16_t>(i.b));
                        continue;
                    case Op::LoadNamespace:
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_namespace(static_cast<NamespaceId>(i.b));
                        continue;
                    case Op::LoadBuiltin:
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_builtin_fn(static_cast<BuiltinId>(i.b));
                        continue;
                    case Op::NewTable: {
                        uint16_t rid = 0;
                        if (!alloc_record(rid)) return fail(Status::RecordPoolExhausted);
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_record(rid);
                        continue;
                    }
                    case Op::GetTable: {
                        const Value t = stack_[base + static_cast<size_t>(i.b)];
                        const Value k = rk(i.c);
                        if (t.kind == ValueKind::Record) {
                            stack_[base + static_cast<size_t>(i.a)] = record_get(t.as_handle(), k);
                        } else if (t.kind == ValueKind::Namespace && k.is_string()) {
                            stack_[base + static_cast<size_t>(i.a)] = resolve_namespace_field(
                                static_cast<NamespaceId>(t.as_handle()), string_view_of(k.as_string_id()));
                        } else if (t.kind == ValueKind::Ptr && k.is_string()) {
                            stack_[base + static_cast<size_t>(i.a)] = resolve_ptr_method(string_view_of(k.as_string_id()));
                        } else {
                            return fail(Status::TypeError);
                        }
                        continue;
                    }
                    case Op::GetField: {
                        const Value t = stack_[base + static_cast<size_t>(i.b)];
                        const Value k = read_const(ph.const_offset + static_cast<uint32_t>(i.c));
                        if (t.kind == ValueKind::Record) {
                            stack_[base + static_cast<size_t>(i.a)] = record_get(t.as_handle(), k);
                        } else if (t.kind == ValueKind::Namespace && k.is_string()) {
                            stack_[base + static_cast<size_t>(i.a)] = resolve_namespace_field(
                                static_cast<NamespaceId>(t.as_handle()), string_view_of(k.as_string_id()));
                        } else if (t.kind == ValueKind::Ptr && k.is_string()) {
                            stack_[base + static_cast<size_t>(i.a)] = resolve_ptr_method(string_view_of(k.as_string_id()));
                        } else {
                            return fail(Status::TypeError);
                        }
                        continue;
                    }
                    case Op::SetTable: {
                        const Value t = stack_[base + static_cast<size_t>(i.a)];
                        if (t.kind != ValueKind::Record) return fail(Status::TypeError);
                        const Value k = rk(i.b);
                        if (!k.is_int()) return fail(Status::InvalidKey); // Fixed arrays only: no dynamic dict keys
                        const Status st = record_set(t.as_handle(), k, rk(i.c));
                        if (st != Status::Ok) return fail(st);
                        continue;
                    }
                    case Op::SetField: {
                        const Value t = stack_[base + static_cast<size_t>(i.a)];
                        if (t.kind != ValueKind::Record) return fail(Status::TypeError);
                        const Value k = read_const(ph.const_offset + static_cast<uint32_t>(i.b));
                        const Status st = record_set(t.as_handle(), k, rk(i.c));
                        if (st != Status::Ok) return fail(st);
                        continue;
                    }
                    case Op::Self: {
                        const Value obj = stack_[base + static_cast<size_t>(i.b)];
                        const Value k = read_const(ph.const_offset + static_cast<uint32_t>(i.c));
                        stack_[base + static_cast<size_t>(i.a) + 1u] = obj;
                        if (obj.kind == ValueKind::Record) {
                            stack_[base + static_cast<size_t>(i.a)] = record_get(obj.as_handle(), k);
                        } else if (obj.kind == ValueKind::Ptr && k.is_string()) {
                            stack_[base + static_cast<size_t>(i.a)] = resolve_ptr_method(string_view_of(k.as_string_id()));
                        } else if (obj.kind == ValueKind::Namespace && k.is_string()) {
                            stack_[base + static_cast<size_t>(i.a)] = resolve_namespace_field(
                                static_cast<NamespaceId>(obj.as_handle()), string_view_of(k.as_string_id()));
                        } else {
                            return fail(Status::TypeError);
                        }
                        continue;
                    }
                    case Op::SetList: {
                        const size_t ra = base + static_cast<size_t>(i.a);
                        const Value t = stack_[ra];
                        if (t.kind != ValueKind::Record) return fail(Status::TypeError);
                        const size_t n = (i.b != 0) ? static_cast<size_t>(i.b) : (top > ra + 1u ? top - ra - 1u : 0u);
                        for (size_t j = 1u; j <= n; ++j) {
                            const Status st = record_set(t.as_handle(),
                                Value::from_int(static_cast<int64_t>(i.c) + static_cast<int64_t>(j)),
                                stack_[ra + j]);
                            if (st != Status::Ok) return fail(st);
                        }
                        continue;
                    }
                    case Op::Add: {
                        const Value b = rk(i.b), c = rk(i.c);
                        if (b.is_int() && c.is_int())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(detail::wrap_add(b.as_int(), c.as_int()));
                        else if (b.is_ptr() && c.is_int())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_ptr(b.as_ptr() + static_cast<uint64_t>(c.as_int()));
                        else if (b.is_int() && c.is_ptr())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_ptr(static_cast<uint64_t>(b.as_int()) + c.as_ptr());
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (b.is_number() && c.is_number())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_float(b.as_double() + c.as_double());
#endif
                        else return fail(Status::TypeError);
                        continue;
                    }
                    case Op::Sub: {
                        const Value b = rk(i.b), c = rk(i.c);
                        if (b.is_int() && c.is_int())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(detail::wrap_sub(b.as_int(), c.as_int()));
                        else if (b.is_ptr() && c.is_int())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_ptr(b.as_ptr() - static_cast<uint64_t>(c.as_int()));
                        else if (b.is_ptr() && c.is_ptr())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(static_cast<int64_t>(b.as_ptr() - c.as_ptr()));
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (b.is_number() && c.is_number())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_float(b.as_double() - c.as_double());
#endif
                        else return fail(Status::TypeError);
                        continue;
                    }
                    case Op::Mul: {
                        const Value b = rk(i.b), c = rk(i.c);
                        if (b.is_int() && c.is_int())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(detail::wrap_mul(b.as_int(), c.as_int()));
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (b.is_number() && c.is_number())
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_float(b.as_double() * c.as_double());
#endif
                        else return fail(Status::TypeError);
                        continue;
                    }
                    case Op::Div: {
#if CAMEO_EMBED_ENABLE_FLOAT
                        const Value b = rk(i.b), c = rk(i.c);
                        if (!b.is_number() || !c.is_number()) return fail(Status::TypeError);
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_float(b.as_double() / c.as_double());
                        continue;
#else
                        return fail(Status::FloatDisabled);
#endif
                    }
                    case Op::IDiv: {
                        const Value b = rk(i.b), c = rk(i.c);
                        if (b.is_int() && c.is_int()) {
                            int64_t q = 0;
                            if (!detail::int_floor_div(b.as_int(), c.as_int(), q))
                                return fail(Status::DivisionByZero);
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(q);
                        }
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (b.is_number() && c.is_number()) {
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_float(detail::floor_d(b.as_double() / c.as_double()));
                        }
#endif
                        else return fail(Status::TypeError);
                        continue;
                    }
                    case Op::Mod: {
                        const Value b = rk(i.b), c = rk(i.c);
                        if (b.is_int() && c.is_int()) {
                            int64_t m = 0;
                            if (!detail::int_mod(b.as_int(), c.as_int(), m))
                                return fail(Status::DivisionByZero);
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(m);
                        }
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (b.is_number() && c.is_number()) {
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_float(detail::fmod_lua(b.as_double(), c.as_double()));
                        }
#endif
                        else return fail(Status::TypeError);
                        continue;
                    }
                    case Op::Pow: {
#if CAMEO_EMBED_ENABLE_MATH
                        const Value b = rk(i.b), c = rk(i.c);
                        if (!b.is_number() || !c.is_number()) return fail(Status::TypeError);
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_float(::pow(b.as_double(), c.as_double()));
                        continue;
#elif CAMEO_EMBED_ENABLE_FLOAT
                        return fail(Status::MathDisabled);
#else
                        return fail(Status::FloatDisabled);
#endif
                    }
                    case Op::BAnd: case Op::BOr: case Op::BXor: case Op::Shl: case Op::Shr: {
                        const Value b = rk(i.b), c = rk(i.c);
                        int64_t x = 0, y = 0;
                        if (b.is_int()) x = b.as_int();
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (!b.is_float() || !detail::float_to_int(b.as_float(), x)) return fail(Status::TypeError);
#else
                        else return fail(Status::TypeError);
#endif
                        if (c.is_int()) y = c.as_int();
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (!c.is_float() || !detail::float_to_int(c.as_float(), y)) return fail(Status::TypeError);
#else
                        else return fail(Status::TypeError);
#endif
                        int64_t r = 0;
                        switch (i.op) {
                            case Op::BAnd: r = static_cast<int64_t>(static_cast<uint64_t>(x) & static_cast<uint64_t>(y)); break;
                            case Op::BOr:  r = static_cast<int64_t>(static_cast<uint64_t>(x) | static_cast<uint64_t>(y)); break;
                            case Op::BXor: r = static_cast<int64_t>(static_cast<uint64_t>(x) ^ static_cast<uint64_t>(y)); break;
                            case Op::Shl:  r = detail::shift_left(x, y); break;
                            case Op::Shr:  r = detail::shift_left(x, detail::wrap_neg(y)); break;
                            default: break;
                        }
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_int(r);
                        continue;
                    }
                    case Op::Unm: {
                        const Value b = stack_[base + static_cast<size_t>(i.b)];
                        if (b.is_int()) stack_[base + static_cast<size_t>(i.a)] = Value::from_int(detail::wrap_neg(b.as_int()));
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (b.is_float()) stack_[base + static_cast<size_t>(i.a)] = Value::from_float(-b.as_float());
#endif
                        else return fail(Status::TypeError);
                        continue;
                    }
                    case Op::Not:
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_bool(!stack_[base + static_cast<size_t>(i.b)].is_truthy());
                        continue;
                    case Op::BNot: {
                        const Value b = stack_[base + static_cast<size_t>(i.b)];
                        int64_t x = 0;
                        if (b.is_int()) x = b.as_int();
#if CAMEO_EMBED_ENABLE_FLOAT
                        else if (!b.is_float() || !detail::float_to_int(b.as_float(), x)) return fail(Status::TypeError);
#else
                        else return fail(Status::TypeError);
#endif
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_int(static_cast<int64_t>(~static_cast<uint64_t>(x)));
                        continue;
                    }
                    case Op::Len: {
                        const Value b = stack_[base + static_cast<size_t>(i.b)];
                        if (b.is_string()) {
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(
                                static_cast<int64_t>(string_view_of(b.as_string_id()).size()));
                        } else if (b.is_record()) {
                            stack_[base + static_cast<size_t>(i.a)] = Value::from_int(array_len(b.as_handle()));
                        } else {
                            return fail(Status::TypeError);
                        }
                        continue;
                    }
                    case Op::Jmp:
                        fr.pc = static_cast<uint32_t>(static_cast<int32_t>(fr.pc) + i.b);
                        continue;
                    case Op::JmpIf:
                        if (stack_[base + static_cast<size_t>(i.a)].is_truthy())
                            fr.pc = static_cast<uint32_t>(static_cast<int32_t>(fr.pc) + i.b);
                        continue;
                    case Op::JmpIfNot:
                        if (!stack_[base + static_cast<size_t>(i.a)].is_truthy())
                            fr.pc = static_cast<uint32_t>(static_cast<int32_t>(fr.pc) + i.b);
                        continue;
                    case Op::Eq: {
                        const bool eq = values_equal(rk(i.b), rk(i.c));
                        if (eq != (i.a != 0)) ++fr.pc;
                        continue;
                    }
                    case Op::Lt: {
                        bool lt = false;
                        const Status st = values_lt(rk(i.b), rk(i.c), lt);
                        if (st != Status::Ok) return fail(st);
                        if (lt != (i.a != 0)) ++fr.pc;
                        continue;
                    }
                    case Op::Le: {
                        bool le = false;
                        const Status st = values_le(rk(i.b), rk(i.c), le);
                        if (st != Status::Ok) return fail(st);
                        if (le != (i.a != 0)) ++fr.pc;
                        continue;
                    }
                    case Op::ForPrep: {
                        bool skip = false;
                        const Status st = op_for_prep(base + static_cast<size_t>(i.a), skip);
                        if (st != Status::Ok) return fail(st);
                        if (skip) fr.pc = static_cast<uint32_t>(static_cast<int32_t>(fr.pc) + i.b);
                        continue;
                    }
                    case Op::ForLoop: {
                        const size_t ra = base + static_cast<size_t>(i.a);
                        Value& idx = stack_[ra];
                        if (idx.is_int()) {
                            const uint64_t count = static_cast<uint64_t>(stack_[ra + 1u].as_int());
                            if (count > 0u) {
                                const int64_t nv = detail::wrap_add(idx.as_int(), stack_[ra + 2u].as_int());
                                stack_[ra + 1u] = Value::from_int(static_cast<int64_t>(count - 1u));
                                idx = Value::from_int(nv);
                                stack_[ra + 3u] = idx;
                                fr.pc = static_cast<uint32_t>(static_cast<int32_t>(fr.pc) + i.b);
                            }
                        }
#if CAMEO_EMBED_ENABLE_FLOAT
                        else {
                            const double st = stack_[ra + 2u].as_float();
                            const double nv = idx.as_float() + st;
                            const double lim = stack_[ra + 1u].as_float();
                            if (st > 0.0 ? (nv <= lim) : (lim <= nv)) {
                                idx = Value::from_float(nv);
                                stack_[ra + 3u] = idx;
                                fr.pc = static_cast<uint32_t>(static_cast<int32_t>(fr.pc) + i.b);
                            }
                        }
#endif
                        continue;
                    }
                    case Op::Closure: {
                        const uint16_t child_pid = static_cast<uint16_t>(i.b);
                        uint16_t new_cl = 0;
                        if (!alloc_closure(child_pid, new_cl))
                            return fail(Status::ClosurePoolExhausted);
                        const detail::ProtoHeader cph = read_proto(child_pid);
                        const ClosureSlot& ncs = closures_[new_cl];
                        for (uint16_t u = 0; u < cph.upval_count; ++u) {
                            const uint8_t* ud = image_ + upvals_off_ +
                                static_cast<size_t>(cph.upval_offset + u) * kUpvalDescSize;
                            const bool from_reg = ud[0] != 0u;
                            const uint8_t idx = ud[1];
                            if (from_reg) {
                                const Value box_v = stack_[base + idx];
                                if (box_v.kind != ValueKind::Box) return fail(Status::TypeError);
                                closure_upvals_[ncs.upval_offset + u] = box_v.as_handle();
                            } else {
                                closure_upvals_[ncs.upval_offset + u] = closure_upvals_[cl.upval_offset + idx];
                            }
                        }
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_closure(new_cl);
                        continue;
                    }
                    case Op::IsKind:
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_bool(
                            eval_is_kind(stack_[base + static_cast<size_t>(i.b)], i.c));
                        continue;
                    case Op::MathFn: {
#if CAMEO_EMBED_ENABLE_FLOAT
                        const Value arg = stack_[base + static_cast<size_t>(i.b)];
                        if (!arg.is_number()) return fail(Status::TypeError);
                        double r = 0.0;
                        const Status st = eval_math_unary(static_cast<BuiltinId>(i.c), arg.as_double(), r);
                        if (st != Status::Ok) return fail(st);
                        stack_[base + static_cast<size_t>(i.a)] = Value::from_float(r);
                        continue;
#else
                        return fail(Status::FloatDisabled);
#endif
                    }
                    case Op::Call: {
                        const size_t call_ra = base + static_cast<size_t>(i.a);
                        const size_t nargs = (i.b != 0) ? static_cast<size_t>(i.b - 1) : (top > call_ra + 1u ? top - call_ra - 1u : 0u);
                        const int16_t nres = static_cast<int16_t>(i.c - 1);
                        const Value callee = stack_[call_ra];

                        if (callee.kind == ValueKind::Closure) {
                            if (frame_count >= MaxFrames) return fail(Status::CallStackOverflow);
                            const uint16_t target_cl = callee.as_handle();
                            const uint16_t target_pid = closures_[target_cl].proto_id;
                            const detail::ProtoHeader tph = read_proto(target_pid);
                            const size_t new_base = call_ra + 1u;
                            if (new_base + tph.max_stack > StackSlots) return fail(Status::ValueStackOverflow);
                            const size_t keep = nargs < tph.num_params ? nargs : tph.num_params;
                            for (size_t k = keep; k < tph.max_stack; ++k) stack_[new_base + k] = Value::nil();
                            frames_[frame_count++] = {
                                target_cl, target_pid,
                                static_cast<uint16_t>(new_base),
                                static_cast<uint16_t>(call_ra),
                                nres, 0u
                            };
                            top = new_base + tph.max_stack;
                            goto next_frame;
                        }
                        if (callee.kind == ValueKind::BuiltinFn) {
                            Value b_out[4]{};
                            size_t got = 0;
                            const Status st = eval_builtin_call(
                                static_cast<BuiltinId>(callee.as_handle()),
                                &stack_[call_ra + 1u], nargs, b_out, 4u, got, steps, step_budget);
                            if (st != Status::Ok) return fail(st);
                            if (nres < 0) {
                                if (call_ra + got > StackSlots) return fail(Status::ValueStackOverflow);
                                for (size_t k = 0; k < got; ++k) stack_[call_ra + k] = b_out[k];
                                top = call_ra + got;
                            } else {
                                const size_t m = got < static_cast<size_t>(nres) ? got : static_cast<size_t>(nres);
                                for (size_t k = 0; k < m; ++k) stack_[call_ra + k] = b_out[k];
                                for (size_t k = m; k < static_cast<size_t>(nres); ++k) stack_[call_ra + k] = Value::nil();
                            }
                            continue;
                        }
                        if (callee.kind == ValueKind::HostFn) {
                            const uint16_t hid = callee.as_handle();
                            if (hid >= MaxHostFunctions || hosts_[hid].fn == nullptr)
                                return fail(Status::UnboundHostFunction);
                            Value host_out[8]{};
                            const size_t max_host_out = (nres < 0) ? 8u : (static_cast<size_t>(nres) < 8u ? static_cast<size_t>(nres) : 8u);
                            HostCallContext ctx(&stack_[call_ra + 1u], nargs, host_out, max_host_out,
                                                image_, image_size_, strings_off_, string_count_, str_data_off_, str_data_size_);
                            const Status st = hosts_[hid].fn(hosts_[hid].ctx, ctx);
                            if (st != Status::Ok) return fail(st);
                            const size_t got = ctx.result_count();
                            if (nres < 0) {
                                if (call_ra + got > StackSlots) return fail(Status::ValueStackOverflow);
                                for (size_t k = 0; k < got; ++k) stack_[call_ra + k] = host_out[k];
                                top = call_ra + got;
                            } else {
                                const size_t m = got < static_cast<size_t>(nres) ? got : static_cast<size_t>(nres);
                                for (size_t k = 0; k < m; ++k) stack_[call_ra + k] = host_out[k];
                                for (size_t k = m; k < static_cast<size_t>(nres); ++k) stack_[call_ra + k] = Value::nil();
                            }
                            continue;
                        }
                        return fail(Status::TypeError);
                    }
                    case Op::TailCall: {
                        const size_t call_ra = base + static_cast<size_t>(i.a);
                        const size_t nargs = (i.b != 0) ? static_cast<size_t>(i.b - 1) : (top > call_ra + 1u ? top - call_ra - 1u : 0u);
                        const Value callee = stack_[call_ra];
                        if (callee.kind == ValueKind::Closure) {
                            const uint16_t target_cl = callee.as_handle();
                            const uint16_t target_pid = closures_[target_cl].proto_id;
                            const detail::ProtoHeader tph = read_proto(target_pid);
                            if (base + tph.max_stack > StackSlots) return fail(Status::ValueStackOverflow);
                            const size_t copy_n2 = nargs < tph.num_params ? nargs : tph.num_params;
                            for (size_t k = 0; k < copy_n2; ++k) stack_[base + k] = stack_[call_ra + 1u + k];
                            for (size_t k = copy_n2; k < tph.max_stack; ++k) stack_[base + k] = Value::nil();
                            fr.closure_id = target_cl;
                            fr.proto_id = target_pid;
                            fr.pc = 0u;
                            top = base + tph.max_stack;
                            goto next_frame;
                        }
                        if (callee.kind == ValueKind::BuiltinFn) {
                            Value b_out[4]{};
                            size_t got = 0;
                            const Status st = eval_builtin_call(
                                static_cast<BuiltinId>(callee.as_handle()),
                                &stack_[call_ra + 1u], nargs, b_out, 4u, got, steps, step_budget);
                            if (st != Status::Ok) return fail(st);
                            if (call_ra + got > StackSlots) return fail(Status::ValueStackOverflow);
                            for (size_t k = 0; k < got; ++k) stack_[call_ra + k] = b_out[k];
                            top = call_ra + got;
                            continue;
                        }
                        if (callee.kind == ValueKind::HostFn) {
                            const uint16_t hid = callee.as_handle();
                            if (hid >= MaxHostFunctions || hosts_[hid].fn == nullptr)
                                return fail(Status::UnboundHostFunction);
                            Value host_out[8]{};
                            HostCallContext ctx(&stack_[call_ra + 1u], nargs, host_out, 8u,
                                                image_, image_size_, strings_off_, string_count_, str_data_off_, str_data_size_);
                            const Status st = hosts_[hid].fn(hosts_[hid].ctx, ctx);
                            if (st != Status::Ok) return fail(st);
                            const size_t got = ctx.result_count();
                            if (call_ra + got > StackSlots) return fail(Status::ValueStackOverflow);
                            for (size_t k = 0; k < got; ++k) stack_[call_ra + k] = host_out[k];
                            top = call_ra + got;
                            continue;
                        }
                        return fail(Status::TypeError);
                    }
                    case Op::Return: {
                        const size_t ra = base + static_cast<size_t>(i.a);
                        const size_t n = (i.b != 0) ? static_cast<size_t>(i.b - 1) : (top > ra ? top - ra : 0u);
                        const uint16_t dest = fr.ret_dest;
                        const int16_t want = fr.want_results;
                        --frame_count;
                        if (frame_count == 0u) {
                            paused_ = false;
                            skip_bp_once_ = false;
                            final_res_count = n < out_results.size() ? n : out_results.size();
                            for (size_t k = 0; k < final_res_count; ++k) out_results[k] = stack_[ra + k];
                            return {Status::Ok, steps, final_res_count, 0, 0};
                        }
                        if (want < 0) {
                            if (static_cast<size_t>(dest) + n > StackSlots) return fail(Status::ValueStackOverflow);
                            for (size_t k = 0; k < n; ++k) stack_[dest + k] = stack_[ra + k];
                            top = dest + n;
                        } else {
                            const size_t m = n < static_cast<size_t>(want) ? n : static_cast<size_t>(want);
                            for (size_t k = 0; k < m; ++k) stack_[dest + k] = stack_[ra + k];
                            for (size_t k = m; k < static_cast<size_t>(want); ++k) stack_[dest + k] = Value::nil();
                        }
                        goto next_frame;
                    }
                    default:
                        return fail(Status::InvalidBytecode);
                }
            }
        }

        return {Status::Ok, steps, final_res_count, 0, 0};
    }
};

} // namespace cameo::embed
