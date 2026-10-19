#include <cstdint>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr int NpSignalingErrorNotInitialized = static_cast<int>(0x80552701u);
constexpr int NpSignalingErrorAlreadyInitialized = static_cast<int>(0x80552702u);

std::mutex g_signalingMutex;
bool g_signalingInitialized = false;

}

extern "C" {

int APS5_VABI sceNpSignalingActivateConnection() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpSignalingCreateContext() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpSignalingDeleteContext() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpSignalingGetConnectionInfo() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpSignalingInitialize(std::int64_t poolSize, int threadPriority, int cpuAffinityMask, std::int64_t threadStackSize) {
    std::lock_guard lock(g_signalingMutex);
    if (g_signalingInitialized) return NpSignalingErrorAlreadyInitialized;
    g_signalingInitialized = true;
    return 0;
}

int APS5_VABI sceNpSignalingSetContextOption() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpSignalingTerminate(void) {
    std::lock_guard lock(g_signalingMutex);
    if (!g_signalingInitialized) return NpSignalingErrorNotInitialized;
    g_signalingInitialized = false;
    return 0;
}

int APS5_VABI sceNpSignalingTerminateConnection() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpSignalingDeactivateConnection(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
