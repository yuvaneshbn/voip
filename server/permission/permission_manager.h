#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class PermissionManager {
public:
    void set_channel(uint32_t ssrc, uint32_t channel_id);
    void mute(uint32_t listener, uint32_t target);
    void unmute(uint32_t listener, uint32_t target);
    bool can_receive(uint32_t listener, uint32_t sender) const;
    void remove_user(uint32_t ssrc);

private:
    std::unordered_map<uint32_t, uint32_t> channel_by_user_;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> muted_by_listener_;
};
