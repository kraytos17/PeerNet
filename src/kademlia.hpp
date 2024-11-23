#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <expected>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

enum class DHTError { Success = 0, PeerNotFound, StaleData, NetworkError, PeerLimitExceeded, InvalidPeer };

class NodeId {
public:
    constexpr NodeId() noexcept : m_id{} {}
    explicit constexpr NodeId(const std::array<uint8_t, 20>& bytes) : m_id(bytes) {}

    static NodeId random() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint32_t> dis;

        NodeId id{};
        for (size_t i = 0; i < id.m_id.size(); i += sizeof(uint32_t)) {
            uint32_t randomValue = dis(gen);
            std::memcpy(&id.m_id[i], &randomValue, std::min(sizeof(uint32_t), id.m_id.size() - i));
        }
        return id;
    }

    static constexpr NodeId zero() noexcept { return NodeId{}; }

    [[nodiscard]] constexpr const std::array<uint8_t, 20>& bytes() const noexcept { return m_id; }

    constexpr auto operator<=>(const NodeId& other) const noexcept = default;

    [[nodiscard]] constexpr NodeId distanceTo(const NodeId& other) const noexcept {
        NodeId result{};
        for (size_t i = 0; i < m_id.size(); ++i) {
            result.m_id[i] = m_id[i] ^ other.m_id[i];
        }
        return result;
    }

    [[nodiscard]] constexpr size_t logDistance(const NodeId& other) const noexcept {
        NodeId xorDistance = distanceTo(other);

        for (size_t byte_index = 0; byte_index < xorDistance.bytes().size(); ++byte_index) {
            uint8_t byte = xorDistance.bytes()[byte_index];
            if (byte != 0) {
                return (xorDistance.bytes().size() - byte_index - 1) * 8 + (7 - std::countl_zero(byte));
            }
        }

        return 0;
    }

    struct Hash {
        constexpr size_t operator()(const NodeId& id) const noexcept {
            size_t hashConstant = 0xCBF29CE484222325ULL;
            for (auto byte: id.m_id) {
                hashConstant ^= static_cast<size_t>(byte);
                hashConstant *= 0x100000001B3ULL;
            }
            return hashConstant;
        }
    };

private:
    std::array<uint8_t, 20> m_id;
};

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

class KBucket {
private:
    std::vector<PeerInfo> m_peers;
    size_t m_maxSize;

    bool pingPeer(const PeerInfo& /*peer*/) {
        return true; // Placeholder for actual ping implementation
    }

public:
    explicit KBucket(size_t maxSize = 20) : m_maxSize(maxSize) {}

    std::expected<void, DHTError> add(const PeerInfo& peer) {
        if (!peer.isValid()) {
            return std::unexpected(DHTError::InvalidPeer);
        }

        auto it = std::ranges::find_if(m_peers, [&peer](const PeerInfo& p) { return p.nodeId == peer.nodeId; });

        if (it != m_peers.end()) {
            *it = peer;
            return {};
        }

        if (m_peers.size() < m_maxSize) {
            m_peers.push_back(peer);
            return {};
        }

        auto oldestIt = std::ranges::min_element(m_peers, {}, &PeerInfo::lastSeen);
        if (!pingPeer(*oldestIt)) {
            *oldestIt = peer;
        }
        return {};
    }

    void removeStalePeers(const std::chrono::seconds& staleThreshold) {
        auto now = std::chrono::system_clock::now();
        std::erase_if(
            m_peers, [&now, &staleThreshold](const PeerInfo& peer) { return (now - peer.lastSeen) >= staleThreshold; });
    }

    std::optional<PeerInfo> find(const NodeId& nodeId) const {
        auto it = std::ranges::find_if(m_peers, [&nodeId](const PeerInfo& peer) { return peer.nodeId == nodeId; });
        return it != m_peers.end() ? std::optional(*it) : std::nullopt;
    }

    [[nodiscard]] const std::vector<PeerInfo>& getAllPeers() const { return m_peers; }
    [[nodiscard]] size_t size() const { return m_peers.size(); }
};

class RoutingTable {
private:
    NodeId m_selfNodeId;
    std::vector<KBucket> m_buckets;
    mutable std::shared_mutex m_mutex;
    size_t m_bucketSize;

public:
    RoutingTable(const NodeId& selfNodeId, size_t bucketSize = 20) :
        m_selfNodeId(selfNodeId), m_buckets(160), m_bucketSize(bucketSize) {}

    std::expected<void, DHTError> addPeer(const PeerInfo& peer) {
        std::unique_lock lock(m_mutex);
        size_t bucketIndex = m_selfNodeId.logDistance(peer.nodeId);
        return m_buckets[bucketIndex].add(peer);
    }

    void refreshBuckets(const std::chrono::seconds& staleThreshold) {
        std::unique_lock lock(m_mutex);
        for (auto& bucket: m_buckets) {
            bucket.removeStalePeers(staleThreshold);
        }
    }

    std::optional<PeerInfo> findPeer(const NodeId& nodeId) const {
        std::shared_lock lock(m_mutex);
        size_t bucketIndex = m_selfNodeId.logDistance(nodeId);
        return m_buckets[bucketIndex].find(nodeId);
    }

    std::vector<PeerInfo> findClosestPeers(const NodeId& target, size_t k) const {
        std::shared_lock lock(m_mutex);
        std::vector<PeerInfo> closestPeers;

        size_t startBucket = m_selfNodeId.logDistance(target);
        for (size_t offset = 0; closestPeers.size() < k && offset < m_buckets.size(); ++offset) {
            if (startBucket + offset < m_buckets.size()) {
                auto& rightPeers = m_buckets[startBucket + offset].getAllPeers();
                closestPeers.insert(closestPeers.end(), rightPeers.begin(), rightPeers.end());
            }
            if (offset > 0 && startBucket >= offset) {
                auto& leftPeers = m_buckets[startBucket - offset].getAllPeers();
                closestPeers.insert(closestPeers.end(), leftPeers.begin(), leftPeers.end());
            }
        }

        std::ranges::partial_sort(closestPeers, closestPeers.begin() + std::min(k, closestPeers.size()),
                                  [&target](const PeerInfo& a, const PeerInfo& b) {
                                      return a.nodeId.distanceTo(target) < b.nodeId.distanceTo(target);
                                  });

        closestPeers.resize(std::min(k, closestPeers.size()));
        return closestPeers;
    }

    [[nodiscard]] size_t getPeerCount() const {
        std::shared_lock lock(m_mutex);
        return std::accumulate(m_buckets.begin(), m_buckets.end(), size_t{0},
                               [](size_t sum, const KBucket& bucket) { return sum + bucket.size(); });
    }
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
    RoutingTable m_routingTable;
    Config m_config{};
    std::jthread m_refreshThread;
    std::atomic<bool> m_isRunning{true};

    void backgroundRefresh(std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(m_config.refreshInterval);
            if (stopToken.stop_requested())
                break;
            m_routingTable.refreshBuckets(m_config.staleThreshold);
        }
    }

public:
    explicit DHT(const NodeId& selfNodeId, Config config) noexcept :
        m_routingTable(selfNodeId, config.k), m_config(std::move(config)),
        m_refreshThread([this](std::stop_token stopToken) { this->backgroundRefresh(stopToken); }) {}

    ~DHT() noexcept {
        if (m_refreshThread.joinable()) {
            m_refreshThread.request_stop();
            m_refreshThread.join();
        }
    }

    std::expected<void, DHTError> addPeer(const PeerInfo& peer) noexcept {
        if (m_routingTable.getPeerCount() >= m_config.maxPeers) {
            return std::unexpected(DHTError::PeerLimitExceeded);
        }

        PeerInfo newPeer = peer;
        newPeer.lastSeen = std::chrono::system_clock::now();
        newPeer.isExpired = false;
        return m_routingTable.addPeer(newPeer);
    }

    [[nodiscard]] std::optional<PeerInfo> getPeer(const NodeId& nodeId) const {
        return m_routingTable.findPeer(nodeId);
    }

    [[nodiscard]] std::vector<PeerInfo> findClosestPeers(const NodeId& target, size_t k = 0) const {
        return m_routingTable.findClosestPeers(target, k ? k : m_config.k);
    }

    [[nodiscard]] size_t getPeerCount() const { return m_routingTable.getPeerCount(); }

    void refresh() { m_routingTable.refreshBuckets(m_config.staleThreshold); }
};
