#pragma once

#include <boost/asio/io_context.hpp>
#include "nodeId.hpp"
#include "peerInfo.hpp"
#include <boost/asio/ip/udp.hpp>
#include "dhtLogger.hpp"
#include <boost/json.hpp>
#include <unordered_set>
#include "dht.hpp"

class KademliaRPC {
public:
    struct RPCMessage {
        enum class Type { PING, STORE, FIND_NODE, FIND_VALUE };

        Type type;
        NodeId sender;
        NodeId target;
        std::string value;
        std::vector<PeerInfo> closestNodes;
    };

private:
    std::unordered_map<NodeId, std::string> m_localStore;
    boost::asio::io_context m_ioCtx;
    boost::asio::ip::udp::socket m_socket;
    DHT& m_dht;

    void processMessage(const RPCMessage& msg, const boost::asio::ip::udp::endpoint& sender) {
        switch (msg.type) {
            case RPCMessage::Type::PING:
                respondToPing(msg, sender);
                break;
            case RPCMessage::Type::STORE:
                storeValue(msg);
                break;
            case RPCMessage::Type::FIND_NODE:
                respondToFindNode(msg, sender);
                break;
            case RPCMessage::Type::FIND_VALUE:
                respondToFindValue(msg, sender);
                break;
        }
    }

    void respondToPing(const RPCMessage& msg, const boost::asio::ip::udp::endpoint& sender) {
        RPCMessage response{RPCMessage::Type::PING, NodeId{}, msg.sender, "", {}};
        sendMessage(response, sender);
    }

    void storeValue(const RPCMessage& msg) {
        m_localStore[msg.target] = msg.value;
        DHTLogger::log(DHTLogger::LogLevel::INFO, "Stored value for key {}", msg.target.toString());
    }

    void respondToFindNode(const RPCMessage& msg, const boost::asio::ip::udp::endpoint& sender) {
        std::vector<PeerInfo> closestNodes = m_dht.findClosestPeers(msg.target);
        RPCMessage response{RPCMessage::Type::FIND_NODE, m_dht.getSelfNodeId(), msg.target, "", closestNodes};
        sendMessage(response, sender);
    }

    void respondToFindValue(const RPCMessage& msg, const boost::asio::ip::udp::endpoint& sender) {
        auto it = m_localStore.find(msg.target);
        RPCMessage response{RPCMessage::Type::FIND_VALUE, m_dht.getSelfNodeId(), msg.target,
                            it != m_localStore.end() ? it->second : "",
                            it == m_localStore.end() ? m_dht.findClosestPeers(msg.target) : std::vector<PeerInfo>{}};

        sendMessage(response, sender);
    }

    void sendMessage(const RPCMessage& msg, const boost::asio::ip::udp::endpoint& endpoint) {
        boost::json::object jsonMsg;
        jsonMsg["type"] = static_cast<int>(msg.type);
        jsonMsg["sender"] = msg.sender.toString();
        jsonMsg["target"] = msg.target.toString();

        if (!msg.value.empty()) {
            jsonMsg["value"] = msg.value;
        }

        if (!msg.closestNodes.empty()) {
            boost::json::array nodesArray;
            for (const auto& peer: msg.closestNodes) {
                boost::json::object peerObj;
                peerObj["ip"] = peer.ipAddress;
                peerObj["port"] = peer.port;
                peerObj["nodeId"] = peer.nodeId.toString();
                nodesArray.push_back(peerObj);
            }

            jsonMsg["closestNodes"] = nodesArray;
        }

        std::string serializedMsg = boost::json::serialize(jsonMsg);
        m_socket.async_send_to(
            boost::asio::buffer(serializedMsg), endpoint, [](const boost::system::error_code& error, std::size_t) {
                if (error) {
                    DHTLogger::log(DHTLogger::LogLevel::ERROR, "Failed to send message: {}", error.message());
                }
            });
    }

    void startReceiving() {
        boost::asio::ip::udp::endpoint senderEndpoint;
        std::vector<char> recvBuffer(1024);

        m_socket.async_receive_from(
            boost::asio::buffer(recvBuffer), senderEndpoint,
            [this, &recvBuffer, &senderEndpoint](const boost::system::error_code& error,
                                                 std::size_t bytes_transferred) {
                if (!error) {
                    std::string receivedMsg(recvBuffer.begin(), recvBuffer.begin() + bytes_transferred);

                    try {
                        auto jsonMsg = boost::json::parse(receivedMsg).as_object();

                        RPCMessage msg;
                        msg.type = static_cast<RPCMessage::Type>(jsonMsg["type"].as_int64());
                        msg.sender = NodeId::fromString(jsonMsg["sender"].as_string());
                        msg.target = NodeId::fromString(jsonMsg["target"].as_string());

                        if (jsonMsg.contains("value")) {
                            msg.value = jsonMsg["value"].as_string();
                        }

                        if (jsonMsg.contains("closestNodes")) {
                            for (const auto& peerJson: jsonMsg["closestNodes"].as_array()) {
                                PeerInfo peer;
                                peer.ipAddress = peerJson.at("ip").as_string();
                                peer.port = peerJson.at("port").as_int64();
                                peer.nodeId = NodeId::fromString(peerJson.at("nodeId").as_string());
                                msg.closestNodes.push_back(peer);
                            }
                        }

                        processMessage(msg, senderEndpoint);
                    } catch (const std::exception& e) {
                        DHTLogger::log(DHTLogger::LogLevel::ERROR, "Failed to parse received message: {}", e.what());
                    }
                }

                startReceiving();
            });
    }

public:
    KademliaRPC(DHT& dht, uint16_t port) :
        m_socket(m_ioCtx, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)), m_dht(dht) {
        startReceiving();
    }

    std::optional<std::string> iterativeFindValue(const NodeId& key) {
        std::vector<PeerInfo> closestNodes = m_dht.findClosestPeers(key);
        std::unordered_set<NodeId> visited;

        while (!closestNodes.empty()) {
            for (const auto& peer: closestNodes) {
                if (visited.contains(peer.nodeId))
                    continue;
                visited.insert(peer.nodeId);
                RPCMessage findMsg{RPCMessage::Type::FIND_VALUE, m_dht.getSelfNodeId(), key, "", {}};

                // TODO: Implement actual network send and wait for response
                // This is a placeholder for actual network communication
            }

            // TODO: Update closest nodes based on responses
            break; // Placeholder
        }

        return std::nullopt;
    }

    void iterativeStore(const NodeId& key, const std::string& value) {
        std::vector<PeerInfo> closestNodes = m_dht.findClosestPeers(key);

        for (const auto& peer: closestNodes) {
            RPCMessage storeMsg{RPCMessage::Type::STORE, m_dht.getSelfNodeId(), key, value, {}};

            // TODO: Implement actual network send
        }
    }
};
