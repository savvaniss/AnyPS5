#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <span>
#include <utility>
#include <vector>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/VideoOutput.hpp"
#include "prx/libSceAgcDriver/Eq/include/Event.hpp"
#include "prx/libSceVideoOut/include/Buffer.hpp"

namespace {

constexpr std::uint32_t OpNop = 0x10;
constexpr std::uint32_t OpClearState = 0x12;
constexpr std::uint32_t OpContextControl = 0x28;
constexpr std::uint32_t OpNumInstances = 0x2f;
constexpr std::uint32_t OpWriteData = 0x37;
constexpr std::uint32_t OpWaitRegMem = 0x3c;
constexpr std::uint32_t OpAcquireMem = 0x58;
constexpr std::uint32_t OpSetContextReg = 0x69;
constexpr std::uint32_t OpSetShReg = 0x76;
constexpr std::uint32_t OpSetUconfigReg = 0x79;
constexpr std::uint32_t OpSurfaceSync = 0x43;
constexpr std::uint32_t OpDmaData = 0x50;
constexpr std::uint32_t DmaRegisterDestination = 1u << 27;
constexpr std::uint32_t DmaNoDestinationIncrement = 1u << 29;
constexpr std::uint32_t CpDmaReadTagsBytes = 0xc08bu * 4;
constexpr std::uint32_t OpEventWriteEop = 0x47;
constexpr std::uint32_t OpEventWriteEos = 0x48;
constexpr std::uint32_t OpReleaseMem = 0x49;
constexpr std::uint32_t OpLoadConstRam = 0x80;
constexpr std::uint32_t OpWriteConstRam = 0x81;
constexpr std::uint32_t OpDumpConstRam = 0x83;
constexpr std::uint32_t OpIncrementCeCounter = 0x84;
constexpr std::uint32_t OpIncrementDeCounter = 0x85;
constexpr std::uint32_t OpWaitOnCeCounter = 0x86;
constexpr std::uint32_t OpWaitOnDeCounterDiff = 0x88;
constexpr std::uint32_t EventControlMask = 0xf3fu;
constexpr std::uint32_t EosStoreData = 2;
constexpr std::uint32_t ReleaseMemData32 = 1u << 29;
constexpr std::uint32_t GfxEndOfPipeEvent = 0x40;

constexpr std::uint32_t HwInitPacketWords = 0x100;
constexpr std::uint32_t FlipReserveWords = 64;
constexpr std::uint32_t PrepareFlipHeader = 0xc03e1000u;
constexpr std::uint32_t MarkerPush = 0x68750001u;
constexpr std::uint32_t MarkerPop = 0x68750002u;
constexpr std::uint32_t MarkerSet = 0x68750003u;
constexpr std::uint32_t MarkerColorPush = 0x6875000eu;
constexpr std::uint32_t PrepareFlip = 0x68750777u;
constexpr std::uint32_t PrepareFlipLabel = 0x68750778u;
constexpr std::uint32_t WriteDataToMemory = 0x500u;
constexpr std::uint32_t WaitMemoryEqual = 0x13u;
constexpr std::uint32_t MaxCommandBufferBytes = 0x3ffffc;
constexpr std::int16_t EvfiltGraphicsCore = -14;

constexpr int GNM_ERROR_SUBMISSION_FAILED_INVALID_ARGUMENT = static_cast<int>(0x80d11000u);
constexpr int GNM_ERROR_VALIDATION_NOT_ENABLED = static_cast<int>(0x80d13fffu);
constexpr int GNM_ERROR_CAPTURE_RAZOR_NOT_LOADED = static_cast<int>(0x80d15001u);
constexpr int GNM_ERROR_CAPTURE_FAILED_INTERNAL = static_cast<int>(0x80d1500fu);

using RegisterValue = std::pair<std::uint32_t, std::uint32_t>;

constexpr std::uint32_t Header(std::uint32_t opcode, std::uint32_t bodyWords, bool compute = false) {
    return 0xc0000000u | ((bodyWords - 1) << 16) | (opcode << 8) | (compute ? 2u : 0u);
}

std::uint32_t* Emit(std::uint32_t* at, std::uint32_t opcode, std::initializer_list<std::uint32_t> body, bool compute = false) {
    *at++ = Header(opcode, static_cast<std::uint32_t>(body.size()), compute);
    for (const auto word : body) *at++ = word;
    return at;
}

std::uint32_t* Nop(std::uint32_t* at, std::uint32_t bodyWords) {
    if (bodyWords == 0) throw std::logic_error("GnmDriver: empty NOP");
    *at = Header(OpNop, bodyWords);
    std::fill_n(at + 1, bodyWords, 0u);
    return at + 1 + bodyWords;
}

std::uint32_t* SetRegisters(std::uint32_t* at, std::uint32_t opcode, std::initializer_list<RegisterValue> values) {
    for (const auto& [reg, value] : values) at = Emit(at, opcode, {reg, value});
    return at;
}

std::uint32_t* DefaultState(std::uint32_t* at, std::uint32_t scModeCntl1) {
    at = SetRegisters(at, OpSetShReg, {{0x216, 0xffffffffu}, {0x217, 0xffffffffu}, {0x215, 0}});
    at = SetRegisters(at, OpSetContextReg, {{0x2f9, 0x2d}, {0x282, 8}, {0x280, 0x80008}, {0x281, 0xffff0000u}, {0x204, 0}, {0x206, 0x43f}, {0x83, 0xffff}, {0x317, 0x10},
        {0x2fa, 0x3f800000u}, {0x2fc, 0x3f800000u}, {0x2fb, 0x3f800000u}, {0x2fd, 0x3f800000u}, {0x202, 0xcc0010}, {0x30e, 0xffffffffu}, {0x30f, 0xffffffffu}});
    at = Emit(at, OpNumInstances, {1});
    at = SetRegisters(at, OpSetShReg, {{7, 0x1ff}, {0x46, 0x1ff}, {0x87, 0x1ff}, {0xc7, 0x1ff}, {0x107, 0}, {0x147, 0x1ff}});
    at = SetRegisters(at, OpSetContextReg, {{0x1b1, 2}, {0x101, 0}, {0x100, 0xffffffffu}, {0x103, 0}, {0x284, 0}, {0x290, 0}, {0x2ae, 0}, {0x292, 0}, {0x293, scModeCntl1},
        {0x2f8, 0}, {0x2de, 0x1e9}});
    at = Emit(at, OpSetContextReg, {0x295, 0x100, 0x100, 4});
    return Emit(at, OpSetUconfigReg, {0x200, 0xe0000000u});
}

struct FrameStart {
    std::uint32_t words[HwInitPacketWords]{};
    std::uint32_t size = 0;
    FrameStart() {
        auto* at = Emit(words, OpClearState, {0});
        size = static_cast<std::uint32_t>(DefaultState(at, 0x6000000) - words);
    }
};

std::mutex g_submitMutex;
bool g_frameStarted = false;

int CheckSubmission(std::uint32_t count, std::uint32_t* const* dcbs, const std::uint32_t* dcbBytes, std::uint32_t* const* ccbs, const std::uint32_t* ccbBytes) {
    if (count != 0 && (!dcbs || !dcbBytes || (ccbBytes && !ccbs))) return GNM_ERROR_SUBMISSION_FAILED_INVALID_ARGUMENT;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (dcbBytes[i] == 0 || dcbBytes[i] > MaxCommandBufferBytes || (ccbBytes && ccbBytes[i] > MaxCommandBufferBytes)) return GNM_ERROR_SUBMISSION_FAILED_INVALID_ARGUMENT;
    }
    return 0;
}

std::span<const std::uint32_t> PacketAt(std::span<const std::uint32_t> words, std::size_t at, const char* stream) {
    const auto header = words[at];
    if ((header >> 30) != 3) throw std::runtime_error(std::string("GnmDriver: unsupported PM4 packet type in the ") + stream);
    const std::size_t total = ((header >> 16) & 0x3fffu) + 2;
    if (total > words.size() - at) throw std::runtime_error(std::string("GnmDriver: truncated packet in the ") + stream);
    return words.subspan(at, total);
}

std::uint32_t Opcode(std::uint32_t header) {
    return (header >> 8) & 0xffu;
}

void Append(std::vector<std::uint32_t>& out, std::initializer_list<std::uint32_t> words) {
    out.insert(out.end(), words);
}

void Lower(std::vector<std::uint32_t>& out, std::span<const std::uint32_t> packet) {
    switch (Opcode(packet[0])) {
        case OpEventWriteEop:
            if (packet.size() != 6) throw std::runtime_error("GnmDriver: invalid EVENT_WRITE_EOP size");
            Append(out, {Header(OpReleaseMem, 7), packet[1] & EventControlMask, packet[3] & 0xe3000000u, packet[2], packet[3] & 0xffffu, packet[4], packet[5], 0});
            return;
        case OpEventWriteEos:
            if (packet.size() != 5) throw std::runtime_error("GnmDriver: invalid EVENT_WRITE_EOS size");
            if ((packet[3] >> 29) != EosStoreData) NotImplemented_nid_no_patch("EVENT_WRITE_EOS GDS store");
            Append(out, {Header(OpReleaseMem, 7), packet[1] & EventControlMask, ReleaseMemData32, packet[2], packet[3] & 0xffffu, packet[4], 0, 0});
            return;
        case OpSurfaceSync:
            if (packet.size() != 5) throw std::runtime_error("GnmDriver: invalid SURFACE_SYNC size");
            Append(out, {Header(OpAcquireMem, 6), packet[1], packet[2], 0, packet[3], 0, packet[4]});
            return;
        case OpNop:
            out.insert(out.end(), packet.begin(), packet.end());
            if (packet.size() >= 2 && (packet[1] & 0xffff0000u) == 0x68750000u) out[out.size() - packet.size() + 1] = 0;
            return;
        case OpDmaData:
            if (packet.size() == 7 && (packet[6] & DmaRegisterDestination) && (packet[6] & DmaNoDestinationIncrement) && packet[4] == CpDmaReadTagsBytes && packet[5] == 0) {
                const auto at = out.size();
                out.resize(at + packet.size());
                Nop(&out[at], static_cast<std::uint32_t>(packet.size()) - 1);
                return;
            }
            out.insert(out.end(), packet.begin(), packet.end());
            return;
        default:
            out.insert(out.end(), packet.begin(), packet.end());
    }
}

std::uint64_t g_constantCounter = 0;
std::uint64_t g_drawCounter = 0;

std::vector<std::uint32_t> MergeEngines(std::span<const std::uint32_t> dcb, std::span<const std::uint32_t> ccb) {
    std::vector<std::uint32_t> out;
    out.reserve(dcb.size() + ccb.size());
    std::size_t constant = 0;
    const auto runConstant = [&](bool untilAhead) {
        while (constant < ccb.size() && !(untilAhead && g_constantCounter > g_drawCounter)) {
            const auto packet = PacketAt(ccb, constant, "constant command buffer");
            constant += packet.size();
            switch (Opcode(packet[0])) {
                case OpIncrementCeCounter:
                    ++g_constantCounter;
                    break;
                case OpWaitOnDeCounterDiff:
                    if (packet.size() != 2) throw std::runtime_error("GnmDriver: invalid WAIT_ON_DE_COUNTER_DIFF size");
                    if (g_constantCounter - g_drawCounter >= packet[1]) NotImplemented_nid_no_patch("a constant engine wait on draw engine progress that has not happened yet");
                    break;
                case OpLoadConstRam: case OpWriteConstRam: case OpDumpConstRam: case OpNop:
                    Lower(out, packet);
                    break;
                default:
                    throw std::runtime_error("GnmDriver: constant engine packet " + std::to_string(Opcode(packet[0])) + " is not implemented");
            }
        }
    };
    for (std::size_t draw = 0; draw < dcb.size();) {
        const auto packet = PacketAt(dcb, draw, "draw command buffer");
        draw += packet.size();
        switch (Opcode(packet[0])) {
            case OpWaitOnCeCounter:
                runConstant(true);
                if (g_constantCounter <= g_drawCounter) throw std::runtime_error("GnmDriver: the draw engine waits on a constant engine counter that is never incremented");
                break;
            case OpIncrementDeCounter:
                ++g_drawCounter;
                break;
            default:
                Lower(out, packet);
        }
    }
    runConstant(false);
    return out;
}

void SubmitLocked(std::uint32_t count, std::uint32_t* const* dcbs, const std::uint32_t* dcbBytes, std::uint32_t* const* ccbs, const std::uint32_t* ccbBytes) {
    static FrameStart frameStart;
    std::vector<std::vector<std::uint32_t>> merged;
    static const bool logSubmits = std::getenv("APS5_GNM_LOG_SUBMITS") != nullptr;
    for (std::uint32_t i = 0; logSubmits && i < count; ++i) std::fprintf(stderr, "[gnm] submit %u/%u dcb %p + 0x%x ccb %p + 0x%x\n", i, count, static_cast<void*>(dcbs[i]), dcbBytes[i], ccbs ? static_cast<void*>(ccbs[i]) : nullptr, ccbBytes ? ccbBytes[i] : 0u);
    static std::uint32_t submitIndex = 0;
    static const std::uint32_t dumpFrom = std::getenv("APS5_GNM_DUMP_FROM") ? static_cast<std::uint32_t>(std::strtoul(std::getenv("APS5_GNM_DUMP_FROM"), nullptr, 0)) : 0xffffffffu;
    static const std::uint32_t dumpCount = std::getenv("APS5_GNM_DUMP_COUNT") ? static_cast<std::uint32_t>(std::strtoul(std::getenv("APS5_GNM_DUMP_COUNT"), nullptr, 0)) : 2u;
    if (submitIndex >= dumpFrom && submitIndex - dumpFrom < dumpCount) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::span<const std::uint32_t> words(dcbs[i], dcbBytes[i] / 4);
            for (std::size_t at = 0; at < words.size();) {
                const auto packet = PacketAt(words, at, "draw command buffer");
                const auto opcode = Opcode(packet[0]);
                if (opcode != OpSetContextReg) {
                    std::fprintf(stderr, "[gnm] #%u %p +0x%zx op 0x%02x", submitIndex, static_cast<const void*>(dcbs[i] + at), at * 4, opcode);
                    for (std::size_t w = 1; w < packet.size() && w <= 9; ++w) std::fprintf(stderr, " %08x", packet[w]);
                    std::fprintf(stderr, packet.size() > 10 ? " (+%zu)\n" : "\n", packet.size() - 10);
                }
                at += packet.size();
            }
        }
    }
    static const bool watchFlags = std::getenv("APS5_GNM_WATCH_FLAGS") != nullptr;
    static const auto started = std::chrono::steady_clock::now();
    static_cast<void>(started);
    const auto sinceStart = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    if (watchFlags && count != 0) {
        static std::atomic<std::uintptr_t> flags[2]{};
        const std::span<const std::uint32_t> words(dcbs[0], dcbBytes[0] / 4);
        std::uintptr_t flag = 0;
        for (std::size_t at = 0; at < words.size() && flag == 0;) {
            const auto packet = PacketAt(words, at, "draw command buffer");
            if (Opcode(packet[0]) == OpWaitRegMem && packet.size() == 7 && ((packet[1] >> 4) & 3u) == 1u) flag = packet[2] | (static_cast<std::uintptr_t>(packet[3]) << 32);
            at += packet.size();
        }
        if (flag != 0 && flags[0].load() != flag && flags[1].load() != flag) {
            if (flags[0].load() == 0) flags[0] = flag; else if (flags[1].load() == 0) flags[1] = flag;
            static std::once_flag once;
            std::call_once(once, [sinceStart] {
                std::thread([sinceStart] {
                    std::uint32_t last[2] = {0xffffffffu, 0xffffffffu};
                    const char* watchText = std::getenv("APS5_GNM_WATCH_ADDR");
                    const auto watched = watchText ? static_cast<std::uintptr_t>(std::strtoull(watchText, nullptr, 0)) : std::uintptr_t{0};
                    std::uint32_t seen[48]{};
                    bool primed = false;
                    for (;;) {
                        if (watched != 0) {
                            std::uint32_t now[48];
                            std::memcpy(now, reinterpret_cast<const void*>(watched), sizeof(now));
                            if (!primed || std::memcmp(now, seen, sizeof(now)) != 0) {
                                std::fprintf(stderr, "[watch] %.3f ms", sinceStart());
                                for (std::size_t w = 0; w < 48; ++w) std::fprintf(stderr, "%s%08x", w % 4 == 0 ? "  " : " ", now[w]);
                                std::fprintf(stderr, "\n");
                                std::memcpy(seen, now, sizeof(now));
                                primed = true;
                            }
                        }
                        for (int f = 0; f < 2; ++f) {
                            const auto address = flags[f].load();
                            if (address == 0) continue;
                            const auto value = std::atomic_ref<const std::uint32_t>(*reinterpret_cast<const std::uint32_t*>(address)).load();
                            if (value != last[f]) std::fprintf(stderr, "[flag] %.3f ms flag %d %p = %u\n", sinceStart(), f, reinterpret_cast<void*>(address), value);
                            last[f] = value;
                        }
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                }).detach();
            });
        }
        std::fprintf(stderr, "[flag] %.3f ms submit %u dcb %p flag %p = %u\n", sinceStart(), submitIndex, static_cast<void*>(dcbs[0]), reinterpret_cast<void*>(flag), flag ? *reinterpret_cast<const volatile std::uint32_t*>(flag) : 0u);
    }
    ++submitIndex;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto constantWords = ccbBytes && ccbs[i] ? ccbBytes[i] / 4 : 0;
        merged.push_back(MergeEngines({dcbs[i], dcbBytes[i] / 4}, {ccbs ? ccbs[i] : nullptr, constantWords}));
    }
    if (!g_frameStarted) {
        static const bool gcn = (AgcDriverUseGcnShaders_nid_postfix(), true);
        static_cast<void>(gcn);
        AgcDriverSubmitGraphics_nid_postfix(frameStart.words, frameStart.size);
        g_frameStarted = true;
    }
    static const bool measureLag = std::getenv("APS5_GNM_LAG") != nullptr;
    static std::uint32_t processed = 0;
    if (measureLag && !merged.empty()) {
        const auto address = reinterpret_cast<std::uintptr_t>(&processed);
        auto& last = merged.back();
        last.insert(last.end(), {Header(OpWriteData, 4), WriteDataToMemory, static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32), submitIndex});
        const auto seen = std::atomic_ref<std::uint32_t>(processed).load();
        if (submitIndex - seen >= 3) std::fprintf(stderr, "[gnm] lag: submit %u while the worker finished %u\n", submitIndex, seen);
    }
    for (auto& commands : merged) AgcDriverSubmitGraphics_nid_postfix(commands.data(), static_cast<std::uint32_t>(commands.size()));
    static const bool syncSubmit = std::getenv("APS5_GNM_SYNC_SUBMIT") != nullptr;
    if (syncSubmit) AgcDriverWaitIdle_nid_postfix();
}

std::uintptr_t BufferLabel(std::uint32_t handle, std::uint32_t index) {
    return (VideoOutBufferLabelAddress_nid_postfix(static_cast<int>(handle)) + index * sizeof(std::uint64_t)) & ~std::uintptr_t{3};
}

void PatchFlip(std::uint32_t* reserve, std::uint32_t handle, std::uint32_t index, std::uint32_t flipMode, std::int64_t flipArg) {
    if (reserve[0] != PrepareFlipHeader) throw std::runtime_error("GnmDriver: the command buffer does not end with a flip reservation");
    const std::uint32_t kind = reserve[1];
    const std::uint32_t labelLow = reserve[2];
    const std::uint32_t labelHigh = reserve[3];
    const std::uint32_t labelValue = reserve[4];
    if (kind != PrepareFlip && kind != PrepareFlipLabel) NotImplemented_nid_no_patch("sceGnmSubmitAndFlipCommandBuffers with an interrupt flip reservation");
    const auto bufferLabel = BufferLabel(handle, index);
    auto* at = Emit(reserve, OpWriteData, {WriteDataToMemory, static_cast<std::uint32_t>(bufferLabel), static_cast<std::uint32_t>(bufferLabel >> 32), 1});
    const auto argument = static_cast<std::uint64_t>(flipArg);
    *at++ = AgcDriver::FlipPacketHeader;
    *at++ = handle;
    *at++ = index;
    *at++ = flipMode;
    *at++ = static_cast<std::uint32_t>(argument);
    *at++ = static_cast<std::uint32_t>(argument >> 32);
    if (kind == PrepareFlipLabel) at = Emit(at, OpWriteData, {WriteDataToMemory, labelLow & ~3u, labelHigh, labelValue});
    Nop(at, static_cast<std::uint32_t>(reserve + FlipReserveWords - at) - 1);
}

std::uint32_t MarkerWords(const char* marker, std::size_t extra) {
    const auto length = std::strlen(marker);
    return static_cast<std::uint32_t>(((length + 8 + extra) >> 2) + ((length + 0xc + extra) >> 3) * 2);
}

void WriteMarker(std::uint32_t* at, std::uint32_t kind, const char* marker, std::uint32_t words, const std::uint32_t* color) {
    const auto length = std::strlen(marker) + 1;
    *at = Header(OpNop, words + 1);
    at[1] = kind;
    auto* text = reinterpret_cast<std::uint8_t*>(at + 2);
    std::memset(text, 0, words * 4);
    std::memcpy(text, marker, length);
    if (color) std::memcpy(text + length + 8, color, sizeof(*color));
}

}

extern "C" {
int APS5_VABI sceAgcDriverSetTFRing(const volatile void* base, std::uint32_t size);


int APS5_VABI sceGnmSetPsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs) {
    if (!cmd || size <= 0x27) return -1;
    if (!regs) {
        cmd = Emit(cmd, OpSetShReg, {8, 0, 0});
        cmd = Emit(cmd, OpSetContextReg, {0x203, 0});
        Nop(cmd, 0x20);
        return 0;
    }
    if (regs[1] != 0) return -1;
    cmd = Emit(cmd, OpSetShReg, {8, regs[0], 0});
    cmd = Emit(cmd, OpSetShReg, {10, regs[2], regs[3]});
    cmd = Emit(cmd, OpSetContextReg, {0x1c4, regs[4], regs[5]});
    cmd = Emit(cmd, OpSetContextReg, {0x1b3, regs[6], regs[7]});
    cmd = SetRegisters(cmd, OpSetContextReg, {{0x1b6, regs[8]}, {0x1b8, regs[9]}, {0x203, regs[10]}, {0x8f, regs[11]}});
    Nop(cmd, 11);
    return 0;
}

int APS5_VABI sceGnmSetVsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs, std::uint32_t modifier) {
    if (!cmd || size <= 0x1c || !regs || (modifier & 0xfcfffc3fu) || regs[1] != 0) return -1;
    const std::uint32_t rsrc1 = modifier == 0 ? regs[2] : (regs[2] & 0xfcfffc3fu) | modifier;
    cmd = Emit(cmd, OpSetShReg, {0x48, regs[0], 0});
    cmd = Emit(cmd, OpSetShReg, {0x4a, rsrc1, regs[3]});
    cmd = SetRegisters(cmd, OpSetContextReg, {{0x207, regs[6]}, {0x1b1, regs[4]}, {0x1c3, regs[5]}});
    Nop(cmd, 11);
    return 0;
}

int APS5_VABI sceGnmSetCsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs) {
    if (!cmd || size <= 0x18 || !regs || regs[1] != 0) return -1;
    cmd = Emit(cmd, OpSetShReg, {0x20c, regs[0], 0});
    cmd = Emit(cmd, OpSetShReg, {0x212, regs[2], regs[3]});
    cmd = Emit(cmd, OpSetShReg, {0x207, regs[4], regs[5], regs[6]});
    Nop(cmd, 11);
    return 0;
}

int APS5_VABI sceGnmSetEsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs, std::uint32_t modifier) {
    if (!cmd || size < 0x14 || !regs || (modifier & 0xfcfffc3fu) || regs[1] != 0) return -1;
    const std::uint32_t rsrc1 = modifier == 0 ? regs[2] : (regs[2] & 0xfcfffc3fu) | modifier;
    cmd = Emit(cmd, OpSetShReg, {0xc8, regs[0], 0});
    cmd = Emit(cmd, OpSetShReg, {0xca, rsrc1, regs[3]});
    Nop(cmd, 11);
    return 0;
}

int APS5_VABI sceGnmSetGsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs) {
    if (!cmd || size < 0x1d || !regs || regs[1] != 0) return -1;
    cmd = Emit(cmd, OpSetShReg, {0x88, regs[0], 0});
    cmd = Emit(cmd, OpSetShReg, {0x8a, regs[2], regs[3]});
    cmd = SetRegisters(cmd, OpSetContextReg, {{0x2e5, regs[4]}, {0x29b, regs[5]}, {0x2e4, regs[6]}});
    Nop(cmd, 11);
    return 0;
}

int APS5_VABI sceGnmSetHsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs, std::uint32_t lsHsConfig) {
    if (!cmd || size < 0x1e || !regs || regs[1] != 0) return -1;
    cmd = Emit(cmd, OpSetShReg, {0x108, regs[0], 0});
    cmd = Emit(cmd, OpSetShReg, {0x10a, regs[2], regs[3]});
    cmd = Emit(cmd, OpSetContextReg, {0x286, regs[5], regs[6]});
    cmd = SetRegisters(cmd, OpSetContextReg, {{0x2db, regs[4]}, {0x2d6, lsHsConfig}});
    Nop(cmd, 11);
    return 0;
}

int APS5_VABI sceGnmSetLsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs, std::uint32_t modifier) {
    if (!cmd || size < 0x17 || !regs) return -1;
    const std::uint32_t mask = (modifier & 0xfffffc3fu) == 0 ? 0xfffffc3fu : 0xfcfffc3fu;
    if ((modifier & mask) || regs[1] != 0) return -1;
    const std::uint32_t rsrc1 = modifier == 0 ? regs[2] : (regs[2] & mask) | modifier;
    cmd = Emit(cmd, OpSetShReg, {0x148, regs[0], 0});
    cmd = Emit(cmd, OpSetShReg, {0x14b, regs[3]});
    cmd = Emit(cmd, OpSetShReg, {0x14a, rsrc1, regs[3]});
    Nop(cmd, 11);
    return 0;
}

std::uint32_t APS5_VABI sceGnmDrawInitDefaultHardwareState175(std::uint32_t* cmd, std::uint32_t size) {
    if (size < HwInitPacketWords) return 0;
    auto* at = Emit(cmd, OpContextControl, {0x80000000u, 0x80000000u});
    at = Emit(at, OpClearState, {0});
    at = Emit(at, OpAcquireMem, {0x2ec47fc0u, 0xffffffffu, 0, 0, 0, 10});
    at = DefaultState(at, 0x6020000);
    Nop(at, static_cast<std::uint32_t>(cmd + HwInitPacketWords - at) - 1);
    return HwInitPacketWords;
}

std::uint32_t APS5_VABI sceGnmDispatchInitDefaultHardwareState(std::uint32_t* cmd, std::uint32_t size) {
    if (size < HwInitPacketWords) return 0;
    auto* at = Emit(cmd, OpSetShReg, {0x216, 0xffffffffu}, true);
    at = Emit(at, OpSetShReg, {0x217, 0xffffffffu}, true);
    at = Emit(at, OpSetShReg, {0x215, 0x170}, true);
    at = Emit(at, OpAcquireMem, {0x28000000u, 0, 0, 0, 0, 10});
    Nop(at, static_cast<std::uint32_t>(cmd + HwInitPacketWords - at) - 1);
    return HwInitPacketWords;
}

int APS5_VABI sceGnmInsertPushMarker(std::uint32_t* cmd, std::uint32_t size, const char* marker) {
    if (!cmd || !marker) return -1;
    const auto words = MarkerWords(marker, 0);
    if (words + 2 != size) return -1;
    WriteMarker(cmd, MarkerPush, marker, words, nullptr);
    return 0;
}

int APS5_VABI sceGnmInsertSetMarker(std::uint32_t* cmd, std::uint32_t size, const char* marker) {
    if (!cmd || !marker) return -1;
    const auto words = MarkerWords(marker, 0);
    if (words + 2 != size) return -1;
    WriteMarker(cmd, MarkerSet, marker, words, nullptr);
    return 0;
}

int APS5_VABI sceGnmInsertPushColorMarker(std::uint32_t* cmd, std::uint32_t size, const char* marker, std::uint32_t color) {
    if (!cmd || !marker) return -1;
    const auto words = MarkerWords(marker, 4);
    if (words + 2 != size) return -1;
    WriteMarker(cmd, MarkerColorPush, marker, words, &color);
    return 0;
}

int APS5_VABI sceGnmInsertPopMarker(std::uint32_t* cmd, std::uint32_t size) {
    if (!cmd || size != 6) return -1;
    Emit(cmd, OpNop, {MarkerPop, 0, 0, 0, 0});
    return 0;
}

int APS5_VABI sceGnmInsertSetColorMarker() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmInsertWaitFlipDone(std::uint32_t* cmd, std::uint32_t size, std::uint32_t handle, std::uint32_t index) {
    if (size != 7) return -1;
    const auto label = BufferLabel(handle, index);
    Emit(cmd, OpWaitRegMem, {WaitMemoryEqual, static_cast<std::uint32_t>(label), static_cast<std::uint32_t>(label >> 32), 0, 0xffffffffu, 10});
    return 0;
}

int APS5_VABI sceGnmSubmitCommandBuffers(std::uint32_t count, std::uint32_t* const* dcbs, const std::uint32_t* dcbBytes, std::uint32_t* const* ccbs, const std::uint32_t* ccbBytes) {
    if (!dcbs || !dcbBytes) return GNM_ERROR_SUBMISSION_FAILED_INVALID_ARGUMENT;
    if (const int error = CheckSubmission(count, dcbs, dcbBytes, ccbs, ccbBytes)) return error;
    std::lock_guard lock(g_submitMutex);
    SubmitLocked(count, dcbs, dcbBytes, ccbs, ccbBytes);
    return 0;
}

int APS5_VABI sceGnmSubmitAndFlipCommandBuffers(std::uint32_t count, std::uint32_t* const* dcbs, const std::uint32_t* dcbBytes, std::uint32_t* const* ccbs, const std::uint32_t* ccbBytes,
    std::uint32_t handle, std::uint32_t index, std::uint32_t flipMode, std::int64_t flipArg) {
    if (count == 0) return GNM_ERROR_SUBMISSION_FAILED_INVALID_ARGUMENT;
    if (const int error = CheckSubmission(count, dcbs, dcbBytes, ccbs, ccbBytes)) return error;
    if (dcbBytes[count - 1] < FlipReserveWords * 4) throw std::runtime_error("GnmDriver: the last command buffer has no room for a flip reservation");
    std::lock_guard lock(g_submitMutex);
    PatchFlip(dcbs[count - 1] + dcbBytes[count - 1] / 4 - FlipReserveWords, handle, index, flipMode, flipArg);
    SubmitLocked(count, dcbs, dcbBytes, ccbs, ccbBytes);
    return 0;
}

int APS5_VABI sceGnmSubmitDone() {
    std::lock_guard lock(g_submitMutex);
    g_frameStarted = false;
    return 0;
}

int APS5_VABI sceGnmAreSubmitsAllowed() {
    return 1;
}

void APS5_VABI sceGnmFlushGarlic() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

int APS5_VABI sceGnmAddEqEvent(KernelEqueue eq, std::uint64_t id, void* udata) {
    if (id != GfxEndOfPipeEvent) NotImplemented_nid_no_patch("sceGnmAddEqEvent for a compute queue event");
    return AgcDriverAddQueueEqEvent_nid_postfix(eq, 0, static_cast<std::uintptr_t>(id), udata);
}

int APS5_VABI sceGnmDeleteEqEvent(KernelEqueue eq, std::uint64_t id) {
    return AgcDriverDeleteQueueEqEvent_nid_postfix(eq, static_cast<std::uintptr_t>(id));
}

int APS5_VABI sceGnmGetEqEventType(const KernelEvent* ev) {
    if (!ev || ev->filter != EvfiltGraphicsCore) throw std::runtime_error(std::string(__func__) + ": not a graphics event");
    return static_cast<int>(ev->ident);
}

bool APS5_VABI sceGnmIsUserPaEnabled() {
    return false;
}

std::uint32_t APS5_VABI sceGnmLogicalCuMaskToPhysicalCuMask(std::int64_t, std::uint32_t logicalMask) {
    return logicalMask;
}

bool APS5_VABI sceGnmDriverCaptureInProgress() {
    return false;
}

int APS5_VABI sceGnmDriverTriggerCapture() {
    return GNM_ERROR_CAPTURE_RAZOR_NOT_LOADED;
}

int APS5_VABI sceGnmValidateCommandBuffers() {
    return GNM_ERROR_VALIDATION_NOT_ENABLED;
}

bool APS5_VABI sceRazorIsLoaded() {
    return false;
}

int APS5_VABI sceRazorCaptureImmediate() {
    return GNM_ERROR_CAPTURE_FAILED_INTERNAL;
}

int APS5_VABI sceRazorCaptureSinceLastFlip() {
    return GNM_ERROR_CAPTURE_FAILED_INTERNAL;
}

int APS5_VABI sceRazorCaptureCommandBuffersOnlyImmediate() {
    return GNM_ERROR_CAPTURE_FAILED_INTERNAL;
}

int APS5_VABI sceRazorCaptureCommandBuffersOnlySinceLastFlip() {
    return GNM_ERROR_CAPTURE_FAILED_INTERNAL;
}

int APS5_VABI sceGnmDebugHardwareStatus() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmDingDong() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmDisableMipStatsReport() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmGetEqTimeStamp() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::uintptr_t APS5_VABI sceGnmGetTheTessellationFactorRingBufferBaseAddress() {
    constexpr std::size_t ringSize = 0x20000;
    constexpr std::size_t ringAlignment = 0x10000;
    static const std::uintptr_t ring = [] {
        auto* memory = static_cast<std::uint8_t*>(::operator new(ringSize, std::align_val_t{ringAlignment}));
        std::memset(memory, 0, ringSize);
        sceAgcDriverSetTFRing(memory, static_cast<std::uint32_t>(ringSize));
        return reinterpret_cast<std::uintptr_t>(memory);
    }();
    return ring;
}

int APS5_VABI sceGnmLogicalCuIndexToPhysicalCuIndex() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmMapComputeQueue() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmRequestFlipAndSubmitDone() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmRequestMipStatsReportAndReset() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmSetGsRingSizes() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmSetupMipStatsReport() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmUnmapComputeQueue() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::uint32_t APS5_VABI sceGnmDrawInitDefaultHardwareState200(std::uint32_t* cmd, std::uint32_t size) {
    return sceGnmDrawInitDefaultHardwareState175(cmd, size);
}

int APS5_VABI sceGnmMapComputeQueueWithPriority(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceGnmUpdatePsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs) {
    return sceGnmSetPsShader(cmd, size, regs);
}

int APS5_VABI sceGnmUpdateHsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs, std::uint32_t lsHsConfig) {
    return sceGnmSetHsShader(cmd, size, regs, lsHsConfig);
}

int APS5_VABI sceGnmUpdateVsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs, std::uint32_t modifier) {
    return sceGnmSetVsShader(cmd, size, regs, modifier);
}

int APS5_VABI sceGnmUpdateGsShader(std::uint32_t* cmd, std::uint32_t size, const std::uint32_t* regs) {
    return sceGnmSetGsShader(cmd, size, regs);
}

}
