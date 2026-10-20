#include <cstdint>
#include <cstddef>
#include <atomic>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr int MoveErrorNoControllerConnected = 1;
constexpr int MoveErrorNotInit = static_cast<int>(0x80EE0001u);
constexpr int MoveErrorAlreadyInit = static_cast<int>(0x80EE0002u);
constexpr int MoveErrorInvalidArg = static_cast<int>(0x80EE0003u);

std::mutex moveMutex;
bool moveInitialized = false;
std::atomic<std::int32_t> nextHandle{0x30b0000};

bool Initialized() {
    std::lock_guard lock(moveMutex);
    return moveInitialized;
}

}

extern "C" {

int APS5_VABI sceMoveInit(void) {
    std::lock_guard lock(moveMutex);
    if (moveInitialized) return MoveErrorAlreadyInit;
    moveInitialized = true;
    return 0;
}

int APS5_VABI sceMoveTerm(void) {
    std::lock_guard lock(moveMutex);
    if (!moveInitialized) return MoveErrorNotInit;
    moveInitialized = false;
    return 0;
}

int APS5_VABI sceMoveOpen(std::int32_t user_id, std::int32_t type, std::int32_t index) {
    (void)user_id;
    (void)type;
    (void)index;
    if (!Initialized()) return MoveErrorNotInit;
    return nextHandle.fetch_add(0x100) + 0x100;
}

int APS5_VABI sceMoveClose(std::int32_t handle) {
    (void)handle;
    if (!Initialized()) return MoveErrorNotInit;
    return 0;
}

int APS5_VABI sceMoveGetDeviceInfo(std::int32_t handle, void* info) {
    (void)handle;
    if (!Initialized()) return MoveErrorNotInit;
    if (!info) return MoveErrorInvalidArg;
    return MoveErrorNoControllerConnected;
}

int APS5_VABI sceMoveReadStateLatest(std::int32_t handle, void* data) {
    (void)handle;
    if (!Initialized()) return MoveErrorNotInit;
    if (!data) return MoveErrorInvalidArg;
    return MoveErrorNoControllerConnected;
}

int APS5_VABI sceMoveReadStateRecent(std::int32_t handle, std::int64_t timestamp, void* data, std::int32_t* out_count) {
    (void)handle;
    if (!Initialized()) return MoveErrorNotInit;
    if (timestamp < 0 || !data || !out_count) return MoveErrorInvalidArg;
    return MoveErrorNoControllerConnected;
}

int APS5_VABI sceMoveGetExtensionPortInfo(std::int32_t handle, void* data) {
    (void)handle;
    (void)data;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceMoveResetLightSphere(std::int32_t handle) {
    (void)handle;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceMoveSetLightSphere(std::int32_t handle, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    (void)handle;
    (void)red;
    (void)green;
    (void)blue;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
