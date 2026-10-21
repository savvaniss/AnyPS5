#include "BdaAbi.hpp"
#include "Optimization/ResourceProgram.hpp"
#include "Recompiler.hpp"
#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <spirv/unified1/spirv.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ShaderRecompiler;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

SpirvTarget Target() {
    static constexpr std::array<std::uint32_t, 4> capabilities{
        spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess, spv::CapabilityClipDistance,
    };
    static constexpr std::array<std::string_view, 4> extensions{
        "SPV_EXT_descriptor_indexing", "SPV_KHR_8bit_storage", "SPV_KHR_float_controls", "SPV_KHR_physical_storage_buffer",
    };
    return {0x00401000u, 0x00010300u, 32, BdaAbi::Version, capabilities, extensions, false,
            {1024, 1024, 64}, 1024, 32768, {}, {}};
}

class Source {
public:
    explicit Source(std::vector<std::uint32_t> program) : code(std::move(program)) {
        const std::span<const std::uint32_t> words(code);
        memory = {{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(words)}};
        request.shader = {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), words, 0, {}};
        request.target = Target();
        request.layout = {0, 0, 0, 128};
        request.context.waveSize = 32;
        request.context.userDataBaseRegister = 0;
        request.context.userData = userData;
        request.context.compute = ShaderComputeStageInfo{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
        request.context.memory = memory;
        request.useCache = true;
        handle = PrepareShader(request);
    }

    std::shared_ptr<const RecompileResult> Materialize(std::uint32_t input) {
        userData = {input, 0u, 256u, 0x31016facu, 0x20000000u, 0u, 256u, 0x31016facu};
        request.context.userData = userData;
        SrtRuntime runtime{};
        runtime.userData = userData;
        const auto capture = CaptureResources(request, runtime, *handle);
        return MaterializeShader(request, *capture, *handle);
    }

private:
    std::vector<std::uint32_t> code;
    std::array<MemoryRegion, 1> memory{};
    std::array<std::uint32_t, 8> userData{};
    RecompileRequest request{};
    std::shared_ptr<const SourceHandle> handle;
};

constexpr std::uint32_t Repeated = 0x10000000u;

std::uint32_t Distinct(std::size_t index) {
    return 0x40000000u + static_cast<std::uint32_t>(index) * 256u;
}

void MissingSource() {
    Source source({0x34020082u, 0xe0301000u, 0x80000201u, 0xbf8c3f70u, 0xe0701000u, 0x80010201u, 0xbf810000u});
    const auto first = source.Materialize(Repeated);
    Check(source.Materialize(Repeated) == first, "result memo: a repeated snapshot did not reuse its result");
    for (std::size_t index = 2; index < ResultMemoWindow; ++index) static_cast<void>(source.Materialize(Distinct(index)));
    const auto bypassedFirst = source.Materialize(Repeated);
    const auto bypassedSecond = source.Materialize(Repeated);
    Check(bypassedFirst != first && bypassedSecond != bypassedFirst, "result memo: a source with 1 hit in " + std::to_string(ResultMemoWindow) + " lookups still looked its results up and kept them");
    Check(bypassedFirst->variantId == first->variantId && bypassedFirst->bindings.size() == first->bindings.size() && bypassedFirst->pushConstants == first->pushConstants, "result memo: a bypassed result differs from the memoized one");
    for (std::size_t index = 2; index < ResultMemoBypass; ++index) static_cast<void>(source.Materialize(Distinct(ResultMemoWindow + index)));
    Check(source.Materialize(Repeated) == first, "result memo: the source did not use its memo again after " + std::to_string(ResultMemoBypass) + " bypassed results");
}

void HittingSource() {
    Source source({0xbf800000u, 0x34020082u, 0xe0301000u, 0x80000201u, 0xbf8c3f70u, 0xe0701000u, 0x80010201u, 0xbf810000u});
    std::shared_ptr<const RecompileResult> first;
    for (std::size_t index = 0; index < 4 * ResultMemoWindow; ++index) {
        const auto distinct = index % ResultMemoHitShare == 0;
        const auto result = source.Materialize(distinct ? Distinct(index) : Repeated);
        if (distinct) continue;
        if (first == nullptr) first = result;
        Check(result == first, "result memo: a source with " + std::to_string(ResultMemoHitShare - 1u) + " hits in every " + std::to_string(ResultMemoHitShare) + " lookups stopped reusing its results at lookup " + std::to_string(index));
    }
}

}

int main() {
    try {
        MissingSource();
        HittingSource();
        std::printf("result memo bypass tests passed (window %zu, one hit in %zu kept, bypass %zu)\n", ResultMemoWindow, ResultMemoHitShare, ResultMemoBypass);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
