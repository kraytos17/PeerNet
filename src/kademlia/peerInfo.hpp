#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include "nodeId.hpp"

struct PeerInfo {
    std::string ipAddress{};
    uint16_t port{};
    std::chrono::system_clock::time_point lastSeen{};
    NodeId nodeId{};
    bool isExpired{false};

    [[nodiscard]] bool isValid() const {
        return !ipAddress.empty() && port > 0 && nodeId.bytes() != NodeId::zero().bytes();
    }

    auto operator<=>(const PeerInfo& other) const noexcept = default;
};
