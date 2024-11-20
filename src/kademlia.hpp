#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

enum class DHTError { Success = 0, PeerNotFound, StaleData, NetworkError, PeerLimitExceeded };

class NodeId {
    std::array<uint8_t, 20> m_id;

public:
    constexpr NodeId() noexcept : m_id{} {}

    explicit constexpr NodeId(const std::array<uint8_t, 20>& bytes) : m_id(bytes) {}

    static NodeId random() {
        NodeId id;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<unsigned short> dis(0, 255);
        for (auto& byte: id.m_id) {
            byte = static_cast<uint8_t>(dis(gen));
        }
        return id;
    }

    static constexpr NodeId zero() noexcept { return NodeId{}; }

    constexpr auto operator<=>(const NodeId& other) const noexcept = default;

    [[nodiscard]] constexpr NodeId distanceTo(const NodeId& other) const noexcept {
        NodeId result{};
        for (size_t i{0}; i < m_id.size(); ++i) {
            result.m_id[i] = m_id[i] ^ other.m_id[i];
        }
        return result;
    }

    [[nodiscard]] constexpr const std::array<uint8_t, 20>& bytes() const noexcept { return m_id; }

    struct Hash {
        constexpr size_t operator()(const NodeId& id) const noexcept {
            size_t hash{0};
            for (auto byte: id.bytes()) {
                hash ^= (hash << 5) + (hash >> 2) + byte;
            }
            return hash;
        }
    };
};

struct PeerInfo {
    std::string ipAddress{};
    uint16_t port{};
    std::chrono::system_clock::time_point lastSeen{};
    NodeId nodeId{};
    bool isExpired{false};
};

class DHT {
public:
    struct Config {
        std::chrono::seconds refreshInterval{60};
        std::chrono::seconds staleThreshold{300};
        size_t maxPeers{1000};
        uint16_t k{20};
    };

private:
    std::unordered_map<NodeId, PeerInfo, NodeId::Hash> m_peerMap;
    Config m_config;
    std::jthread m_refreshThread;
    mutable std::shared_mutex m_mutex;
    std::atomic<bool> m_isRunning{true};

    bool isPeerStale(const PeerInfo& peer, std::chrono::system_clock::time_point now) const {
        return (now - peer.lastSeen) > m_config.staleThreshold;
    }

    void backgroundRefresh(std::stop_token stopToken) {
        while (!stopToken.stop_requested() && m_isRunning) {
            std::this_thread::sleep_for(m_config.refreshInterval);
            refreshDht();
        }
    }

    void refreshDht() {
        auto now = std::chrono::system_clock::now();
        std::unique_lock lock(m_mutex);

        for (auto& [id, peer]: m_peerMap) {
            if (isPeerStale(peer, now)) {
                peer.isExpired = true;
            }
        }

        std::erase_if(m_peerMap, [](const auto& pair) { return pair.second.isExpired; });
    }

public:
    explicit DHT(Config config) noexcept :
        m_config(std::move(config)),
        m_refreshThread([this](std::stop_token stopToken) { this->backgroundRefresh(stopToken); }) {}

    ~DHT() noexcept {
        m_isRunning = false;
        if (m_refreshThread.joinable()) {
            m_refreshThread.request_stop();
        }
    }

    std::optional<DHTError> addPeer(const NodeId& nodeId, const PeerInfo& peer) noexcept {
        std::unique_lock lock(m_mutex);

        if (m_peerMap.size() >= m_config.maxPeers) {
            return DHTError::PeerLimitExceeded;
        }

        PeerInfo newPeer = peer;
        newPeer.lastSeen = std::chrono::system_clock::now();
        newPeer.isExpired = false;
        m_peerMap[nodeId] = std::move(newPeer);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<PeerInfo> getPeer(const NodeId& nodeId) const {
        std::shared_lock lock(m_mutex);

        auto it = m_peerMap.find(nodeId);
        if (it == m_peerMap.end() || it->second.isExpired) {
            return std::nullopt;
        }

        return it->second;
    }

    [[nodiscard]] std::vector<PeerInfo> findClosestPeers(const NodeId& target, size_t k = 0) const {
        if (k == 0) {
            k = m_config.k;
        }

        auto now = std::chrono::system_clock::now();
        std::shared_lock lock(m_mutex);

        std::vector<std::pair<NodeId, PeerInfo>> validPeers;
        validPeers.reserve(m_peerMap.size());

        for (const auto& [id, peer]: m_peerMap) {
            if (!peer.isExpired && !isPeerStale(peer, now)) {
                validPeers.emplace_back(id, peer);
            }
        }

        std::nth_element(validPeers.begin(), validPeers.begin() + std::min(k, validPeers.size()), validPeers.end(),
                         [&target](const auto& a, const auto& b) {
                             return a.first.distanceTo(target) < b.first.distanceTo(target);
                         });

        std::vector<PeerInfo> result;
        result.reserve(std::min(k, validPeers.size()));
        for (size_t i = 0; i < std::min(k, validPeers.size()); ++i) {
            result.push_back(validPeers[i].second);
        }

        return result;
    }

    [[nodiscard]] size_t getPeerCount() const {
        std::shared_lock lock(m_mutex);
        return m_peerMap.size();
    }
};
