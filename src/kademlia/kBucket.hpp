#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include "peerInfo.hpp"
#include "dhtError.hpp"

class KBucket {
private:
    std::vector<PeerInfo> m_peers;
    size_t m_maxSize;
    boost::asio::io_context m_ioCtx{};
    boost::asio::steady_timer m_refreshTimer;
    std::atomic_bool isRefreshing{false};

    void pingPeer(const PeerInfo& peer, std::function<void(bool)> callback) {
        using tcp = boost::asio::ip::tcp;

        auto socket = std::make_shared<tcp::socket>(m_ioCtx);
        tcp::endpoint endpoint(boost::asio::ip::make_address(peer.ipAddress), peer.port);
        socket->async_connect(endpoint, [socket, callback](const boost::system::error_code& ec) {
            if (ec) {
                callback(false);
                return;
            }

            boost::asio::steady_timer timer(socket->get_executor(), std::chrono::seconds(2));
            timer.async_wait([socket, callback](const boost::system::error_code& ec) {
                if (ec) {
                    callback(false);
                    return;
                }

                callback(true);
            });
        });
    }

public:
    explicit KBucket(size_t maxSize = 20) : m_maxSize(maxSize), m_refreshTimer(m_ioCtx) {}

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
        pingPeer(*oldestIt, [this, oldestIt, peer](bool success) {
            if (!success) {
                *oldestIt = peer;
            }
        });

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
