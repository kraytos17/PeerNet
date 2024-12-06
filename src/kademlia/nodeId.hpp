#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <functional>
#include <random>
#include <string>

class NodeId {
public:
    static constexpr size_t NODE_ID_LEN{20};

    constexpr NodeId() noexcept : m_id{} {}
    explicit constexpr NodeId(const std::array<uint8_t, NODE_ID_LEN>& bytes) : m_id(bytes) {}

    static NodeId random() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint8_t> dis(0, 255);

        NodeId id{};
        for (auto& byte: id.m_id) {
            byte = dis(gen);
        }
        return id;
    }

    [[nodiscard]] std::string toString() const {
        std::string result;
        result.reserve(m_id.size() * 2);
        for (auto byte: m_id) {
            result += std::format("{:02x}", byte);
        }

        return result;
    }

    static constexpr NodeId zero() noexcept { return NodeId{}; }

    [[nodiscard]] constexpr const std::array<uint8_t, NODE_ID_LEN>& bytes() const noexcept { return m_id; }

    constexpr auto operator<=>(const NodeId& other) const noexcept = default;

    [[nodiscard]] constexpr NodeId distanceTo(const NodeId& other) const noexcept {
        NodeId result{};
        std::transform(m_id.begin(), m_id.end(), other.m_id.begin(), result.m_id.begin(), std::bit_xor<uint8_t>{});
        return result;
    }

    [[nodiscard]] constexpr size_t logDistance(const NodeId& other) const noexcept {
        NodeId xorDistance = distanceTo(other);
        auto it = std::ranges::find_if_not(xorDistance.bytes(), [](uint8_t b) { return b == 0; });

        if (it == xorDistance.bytes().end()) {
            return 0;
        }

        size_t byteIdx = std::distance(xorDistance.bytes().begin(), it);
        return (xorDistance.bytes().size() - byteIdx - 1) * 8 + (7 - std::countl_zero(*it));
    }

private:
    std::array<uint8_t, NODE_ID_LEN> m_id;
};

namespace std {
    template<>
    struct hash<NodeId> {
        constexpr size_t operator()(const NodeId& id) const noexcept {
            size_t hash = 0xCBF29CE484222325ULL; // FNV-1a offset basis
            for (auto byte: id.bytes()) {
                hash ^= static_cast<size_t>(byte);
                hash *= 0x100000001B3ULL; // FNV-1a prime
            }
            return hash;
        }
    };
} // namespace std
