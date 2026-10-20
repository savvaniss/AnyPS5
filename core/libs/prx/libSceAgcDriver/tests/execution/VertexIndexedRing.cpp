#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;
constexpr std::uint32_t Records = 4096;
constexpr std::uint32_t OutputVertices = 32;
alignas(256) std::array<std::byte, Width * Height * 4> Pixels{};
alignas(256) std::array<std::uint32_t, OutputVertices * 4> Output{};
alignas(256) std::array<std::uint32_t, Records * 4> Ring{};
alignas(256) std::array<std::uint16_t, 4> Indices{0, 2, 1, 0};

alignas(256) constexpr std::array<std::uint32_t, 10> VertexCode{
    0xe00c2000u, 0x80020805u, 0xbf8c3f70u, 0x34020a84u, 0xe0781000u, 0x80010801u, 0xf80008cfu, 0x0b0a0908u, 0xbf810000u, 0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 4> PixelCode{
    0x7e0e02f2u, 0xf800180fu, 0x07070707u, 0xbf810000u,
};

struct Case {
    const char* what;
    bool indexed;
    std::uint32_t count;
    std::uint32_t firstVertex;
    std::array<std::uint32_t, 3> vertices;
};

std::array<std::uint32_t, 4> Descriptor(const void* pointer, std::uint32_t stride, std::uint32_t records, std::uint32_t word3) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return {static_cast<std::uint32_t>(address), (static_cast<std::uint32_t>(address >> 32u) & 0xffffu) | (stride << 16u), records, word3};
}

void Draw(AgcDriver::VulkanDevice& device, const Case& value) {
    Pixels.fill(std::byte{0x40});
    Output.fill(0xdeadbeefu);
    std::vector<std::uint32_t> userData(12, 0u);
    const auto output = Descriptor(Output.data(), 0u, static_cast<std::uint32_t>(sizeof(Output)), 0x31016facu);
    const auto ring = Descriptor(Ring.data(), 16u, Records, 0x1104bfacu);
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    std::copy(ring.begin(), ring.end(), userData.begin() + 8);
    const std::array<ShaderRecompiler::MemoryRegion, 1> vertexMemory{{{reinterpret_cast<std::uintptr_t>(VertexCode.data()), std::as_bytes(std::span(VertexCode))}}};
    ShaderRecompiler::RecompileRequest vertexRequest{
        {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(VertexCode.data()), VertexCode, 0, {}},
        {64u, 0, userData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, vertexMemory},
        device.Target(),
        {0, 0, 0, 64}
    };
    vertexRequest.useCache = false;
    const auto vertexResult = ShaderRecompiler::Recompile(vertexRequest);
    const auto guest = std::ranges::find_if(vertexResult.bindings, [](const ShaderRecompiler::DescriptorBinding& binding) { return binding.role == ShaderRecompiler::DescriptorRole::GuestBuffers; });
    Require(guest != vertexResult.bindings.end() && guest->count == 2u && guest->bufferVertexExtent.size() == 2u, std::string(value.what) + ": the vertex shader does not bind its two V#s with vertex extents");
    for (std::uint32_t element = 0; element < 2u; ++element) {
        const bool ringElement = guest->guestDescriptor[element * 4u + 2u] == Records;
        Require(guest->bufferVertexExtent[element] == (ringElement ? 16u : 0u), std::string(value.what) + ": " + (ringElement ? "the ring read by the vertex index has no vertex extent" : "the output written at a VGPR offset has a vertex extent"));
    }
    const auto vertexPush = static_cast<std::uint32_t>(vertexResult.pushConstants.size());

    ShaderRecompiler::ShaderPixelStageInfo pixel{};
    pixel.inputAddr = ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionX) | ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionY);
    pixel.posX = true;
    pixel.posY = true;
    pixel.targetOutputMode[0] = 9;
    pixel.targetExportMapping.fill(0xe4u);
    const std::vector<std::uint32_t> pixelUserData(8, 0u);
    const std::array<ShaderRecompiler::MemoryRegion, 1> pixelMemory{{{reinterpret_cast<std::uintptr_t>(PixelCode.data()), std::as_bytes(std::span(PixelCode))}}};
    ShaderRecompiler::RecompileRequest fragment{
        {ShaderStage::Fragment, reinterpret_cast<std::uintptr_t>(PixelCode.data()), PixelCode, 0, {}},
        {64u, 0, pixelUserData, std::nullopt, pixel, std::nullopt, pixelMemory},
        device.Target(),
        PixelPushLayout(vertexPush, device.Target())
    };
    fragment.useCache = false;
    const auto pixelResult = ShaderRecompiler::Recompile(fragment);
    const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{
        {ShaderStage::Vertex, &vertexResult, 0},
        {ShaderStage::Fragment, &pixelResult, PixelPushOffset(vertexPush, device.Target())}
    }};

    AgcDriver::Graphics::State state{};
    state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 64u, 64u, std::nullopt, std::nullopt};
    state.color = {reinterpret_cast<std::uintptr_t>(Pixels.data()), {Width, Height}, VK_FORMAT_R8G8B8A8_UNORM, Pixels.size(), 0xe4u};
    state.colors = {state.color};
    state.hasColorTarget = true;
    state.renderExtent = {Width, Height};
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0, static_cast<float>(Height), static_cast<float>(Width), -static_cast<float>(Height), 0, 1};
    state.negativeOneToOne = false;
    state.scissor = {{0, 0}, {Width, Height}};
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.blend.colorWriteMask = 15;
    state.blends = {state.blend};
    state.blendConstants = {};
    AgcDriver::Pm4::DrawParameters draw{value.indexed ? reinterpret_cast<std::uintptr_t>(Indices.data()) : 0u, value.count, value.indexed ? 2u : 0u, 1, 0, value.indexed};
    draw.firstVertex = value.firstVertex;
    device.Draw(state, draw, shaders);
    device.WaitIdle();
}

void Check(const Case& value) {
    for (std::uint32_t vertex = 0; vertex < OutputVertices; ++vertex) {
        const bool drawn = std::ranges::find(value.vertices, vertex) != value.vertices.end();
        for (std::uint32_t component = 0; component < 4u; ++component) {
            const auto expected = drawn ? Ring[vertex * 4u + component] : 0xdeadbeefu;
            const auto actual = Output[vertex * 4u + component];
            Require(actual == expected, std::string(value.what) + ": vertex " + std::to_string(vertex) + " component " + std::to_string(component) + " is " + std::to_string(actual) + ", expected " + std::to_string(expected));
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        for (std::uint32_t index = 0; index < Ring.size(); ++index) Ring[index] = 0x01000000u + index * 0x10001u;
        constexpr std::array<Case, 3> cases{{
            {"auto draw from vertex 5", false, 3u, 5u, {5u, 6u, 7u}},
            {"indexed draw with base vertex 20", true, 3u, 20u, {20u, 22u, 21u}},
            {"auto draw from vertex 0", false, 3u, 0u, {0u, 1u, 2u}},
        }};
        for (const auto& value : cases) {
            Draw(*device, value);
            Check(value);
        }
        std::puts("vertex-indexed ring tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
