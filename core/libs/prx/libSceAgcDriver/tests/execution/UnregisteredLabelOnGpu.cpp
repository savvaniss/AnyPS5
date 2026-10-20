#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::size_t BlockBytes = 65536;

class HostBlock {
public:
    HostBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "unregistered label: cannot allocate a host block");
        std::memset(block, 0, BlockBytes);
    }

    ~HostBlock() {
        if (registered) GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        VirtualFree(block, 0, MEM_RELEASE);
#else
        std::free(block);
#endif
    }

    HostBlock(const HostBlock&) = delete;
    HostBlock& operator=(const HostBlock&) = delete;

    void Register() {
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
        registered = true;
    }

    volatile std::uint32_t* Words() { return reinterpret_cast<volatile std::uint32_t*>(block); }

    std::uint64_t Address() const { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block)); }

private:
    std::uint8_t* block = nullptr;
    bool registered = false;
};

int StoreLabel(AgcDriver::VulkanDevice& device, std::uint64_t address, std::uint32_t value, std::uint64_t stamp) {
    return device.WriteLabelOnGpu(address, std::as_bytes(std::span(&value, 1)), stamp, 0);
}

void RequireReason(int reason, int expected, const char* what) {
    Require(reason == expected, ("unregistered label: " + std::string(what) + " returned reason " + std::to_string(reason) + " instead of " + std::to_string(expected)).c_str());
}

}

int main() {
    try {
        HostBlock labels;
        labels.Register();
        HostBlock unregistered;
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        const std::array<std::uint32_t, 4> pattern{0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
        if (!device->FillBuffer(labels.Address() + 4096, 4096, pattern)) {
            std::printf("skipped, the device does not import guest memory\n");
            return VulkanTestSkipped;
        }
        RequireReason(StoreLabel(*device, unregistered.Address() + 8, 0x5a5a0001u, 1), 0, "a label in unregistered host memory");
        Require(unregistered.Words()[2] == 0, "unregistered label: the label landed before its batch was submitted");
#ifdef _WIN32
        DWORD previous = 0;
        Require(VirtualProtect(reinterpret_cast<void*>(unregistered.Address() + 4096), 4096, PAGE_READONLY, &previous) != 0, "unregistered label: cannot protect the read-only page");
        RequireReason(StoreLabel(*device, unregistered.Address() + 4096 + 8, 0x5a5a0003u, 3), 3, "a label in read-only unregistered memory");
#endif
        RequireReason(StoreLabel(*device, labels.Address() + 4, 0x5a5a0004u, 4), 0, "a label in registered GPU memory");
        device->SubmitRecorded(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (labels.Words()[1] != 0x5a5a0004u) {
            Require(std::chrono::steady_clock::now() < deadline, "unregistered label: the later label never landed");
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        const std::uint32_t seen = unregistered.Words()[2];
        device->WaitIdle();
        std::array<char, 192> message{};
        std::snprintf(message.data(), message.size(), "unregistered label: the label held 0x%08x when a later label had landed, 0x%08x after the device went idle", seen, unregistered.Words()[2]);
        Require(seen == 0x5a5a0001u && unregistered.Words()[2] == 0x5a5a0001u, message.data());
        Require(unregistered.Words()[1024 + 2] == 0, "unregistered label: the refused label was stored");
        std::puts("unregistered label tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
