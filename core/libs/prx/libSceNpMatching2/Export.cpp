#include <cstddef>
#include <cstdint>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr int NpMatching2ErrorAlreadyInitialized = static_cast<int>(0x80550C02u);
constexpr int NpMatching2ErrorNotInitialized = static_cast<int>(0x80550C03u);
constexpr int NpMatching2ErrorInvalidArgument = static_cast<int>(0x80550C0Au);

struct NpMatching2InitializeParameter {
    std::uint64_t poolSize;
    std::uint64_t cpuAffinityMask;
    std::int32_t threadPriority;
    std::uint8_t padding[4];
    std::uint64_t threadStackSize;
    std::uint64_t size;
    std::uint64_t sslPoolSize;
};
static_assert(offsetof(NpMatching2InitializeParameter, size) == 0x20);

std::mutex g_matching2Mutex;
bool g_matching2Initialized = false;

}

extern "C" {

int APS5_VABI sceNpMatching2ContextStart() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2ContextStop() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2CreateContext() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2CreateJoinRoom() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2DestroyContext() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2GetRoomDataExternalList() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2GetRoomDataInternal() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2GetRoomMemberDataInternal() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2GetServerId() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2GetUserInfoList() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2GetWorldInfoList() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2Initialize(const NpMatching2InitializeParameter* param) {
    std::lock_guard lock(g_matching2Mutex);
    if (g_matching2Initialized) return NpMatching2ErrorAlreadyInitialized;
    if (param == nullptr || (param->size != offsetof(NpMatching2InitializeParameter, sslPoolSize) && param->size != sizeof(NpMatching2InitializeParameter))) return NpMatching2ErrorInvalidArgument;
    g_matching2Initialized = true;
    return 0;
}

int APS5_VABI sceNpMatching2JoinRoom() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2KickoutRoomMember() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2LeaveRoom() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2RegisterContextCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2RegisterRoomEventCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2RegisterRoomMessageCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2RegisterSignalingCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SearchRoom() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SendRoomChatMessage() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SendRoomMessage() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SetDefaultRequestOptParam() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SetRoomDataExternal() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SetRoomDataInternal() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SetRoomMemberDataInternal() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SetUserInfo() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SignalingGetConnectionInfo() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2Terminate(void) {
    std::lock_guard lock(g_matching2Mutex);
    if (!g_matching2Initialized) return NpMatching2ErrorNotInitialized;
    g_matching2Initialized = false;
    return 0;
}

int APS5_VABI sceNpMatching2AbortContextStart(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SignalingGetConnectionStatus(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2GrantRoomOwner(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SignalingGetPingInfo(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpMatching2SignalingGetLocalNetInfo(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
