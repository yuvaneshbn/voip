#include "permission_manager.h"

void PermissionManager::set_channel(uint32_t ssrc, uint32_t channel_id) {
    channel_by_user_[ssrc] = channel_id;
}

void PermissionManager::mute(uint32_t listener, uint32_t target) {
    muted_by_listener_[listener].insert(target);
}

void PermissionManager::unmute(uint32_t listener, uint32_t target) {
    auto it = muted_by_listener_.find(listener);
    if (it == muted_by_listener_.end()) {
        return;
    }
    it->second.erase(target);
    if (it->second.empty()) {
        muted_by_listener_.erase(it);
    }
}

bool PermissionManager::can_receive(uint32_t listener, uint32_t sender) const {
    auto mute_it = muted_by_listener_.find(listener);
    if (mute_it != muted_by_listener_.end()) {
        if (mute_it->second.find(sender) != mute_it->second.end()) {
            return false;
        }
    }

    auto sender_ch = channel_by_user_.find(sender);
    auto listener_ch = channel_by_user_.find(listener);
    if (sender_ch != channel_by_user_.end() && listener_ch != channel_by_user_.end()) {
        return sender_ch->second == listener_ch->second;
    }
    return true;
}

void PermissionManager::remove_user(uint32_t ssrc) {
    channel_by_user_.erase(ssrc);
    muted_by_listener_.erase(ssrc);
    for (auto& [listener, muted] : muted_by_listener_) {
        muted.erase(ssrc);
    }
}
