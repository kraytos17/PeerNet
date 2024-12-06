#pragma once

#include <chrono>
#include <thread>
#include "routingTable.hpp"
#include <atomic>

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
