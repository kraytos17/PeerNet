#pragma once

#include <shared_mutex>
#include "kBucket.hpp"
#include "nodeId.hpp"

class RoutingTable {
private:
    NodeId m_selfNodeId;
    std::vector<KBucket> m_buckets;
    std::shared_mutex m_mutex;
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
