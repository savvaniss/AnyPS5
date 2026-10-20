#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

static constexpr int SCE_NP_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80550003);
static constexpr int SCE_NP_UTIL_ERROR_NOT_MATCH = static_cast<int>(0x80550609);
static constexpr std::size_t NP_ONLINE_ID_MAX_LENGTH = 16;

extern "C" {

int APS5_VABI sceNpCmpOnlineId(const NpOnlineId* first, const NpOnlineId* second) {
    if (!first || !second) return SCE_NP_ERROR_INVALID_ARGUMENT;
    return std::strncmp(first->data, second->data, NP_ONLINE_ID_MAX_LENGTH) == 0 ? 0 : SCE_NP_UTIL_ERROR_NOT_MATCH;
}

int APS5_VABI sceNpCmpNpId(const NpId* first, const NpId* second) {
    if (!first || !second) return SCE_NP_ERROR_INVALID_ARGUMENT;
    if (sceNpCmpOnlineId(&first->online_id, &second->online_id) != 0) return SCE_NP_UTIL_ERROR_NOT_MATCH;
    const bool same = std::memcmp(first->opt, second->opt, sizeof(first->opt)) == 0 &&
        std::memcmp(first->reserved, second->reserved, sizeof(first->reserved)) == 0;
    return same ? 0 : SCE_NP_UTIL_ERROR_NOT_MATCH;
}

}
