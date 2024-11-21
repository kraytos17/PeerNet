#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include "../src/kademlia.hpp"

class DHTTest : public ::testing::Test {
protected:
    DHT::Config defaultConfig{};

    void SetUp() override {
        defaultConfig.refreshInterval = std::chrono::seconds(1);
        defaultConfig.staleThreshold = std::chrono::seconds(5);
        defaultConfig.maxPeers = 100;
        defaultConfig.k = 20;
    }
};

TEST(NodeIdTest, RandomGeneration) {
    NodeId id1 = NodeId::random();
    NodeId id2 = NodeId::random();

    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1.bytes().size(), 20);
}

TEST(NodeIdTest, DistanceCalculation) {
    std::array<uint8_t, 20> bytes1 = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::array<uint8_t, 20> bytes2 = {1, 2, 3, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    NodeId id1(bytes1);
    NodeId id2(bytes2);

    NodeId distance = id1.distanceTo(id2);

    EXPECT_EQ(distance.bytes()[3], 1);
    EXPECT_EQ(id1.distanceTo(id2), id2.distanceTo(id1));
}

TEST(NodeIdTest, HashFunction) {
    NodeId id1 = NodeId::random();
    NodeId id2 = NodeId::random();

    NodeId::Hash hasher;

    EXPECT_NE(hasher(id1), hasher(id2));
    EXPECT_EQ(hasher(id1), hasher(id1));
}

TEST_F(DHTTest, AddPeer) {
    DHT dht(NodeId::random(), defaultConfig);

    NodeId nodeId1 = NodeId::random();
    PeerInfo peer1{"192.168.1.1", 8080, std::chrono::system_clock::now(), nodeId1};

    auto result = dht.addPeer(peer1);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(dht.getPeerCount(), 1);
}

TEST_F(DHTTest, AddPeerMaxLimit) {
    defaultConfig.maxPeers = 1;
    DHT dht(NodeId::random(), defaultConfig);

    NodeId nodeId1 = NodeId::random();
    NodeId nodeId2 = NodeId::random();

    PeerInfo peer1{"192.168.1.1", 8080, std::chrono::system_clock::now(), nodeId1};
    PeerInfo peer2{"192.168.1.2", 8081, std::chrono::system_clock::now(), nodeId2};

    auto result1 = dht.addPeer(peer1);
    EXPECT_FALSE(result1.has_value());

    auto result2 = dht.addPeer(peer2);
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), DHTError::PeerLimitExceeded);
}

TEST_F(DHTTest, GetPeer) {
    DHT dht(NodeId::random(), defaultConfig);

    NodeId nodeId = NodeId::random();
    PeerInfo peer{"192.168.1.1", 8080, std::chrono::system_clock::now(), nodeId};

    dht.addPeer(peer);

    auto retrievedPeer = dht.getPeer(nodeId);
    ASSERT_TRUE(retrievedPeer.has_value());
    EXPECT_EQ(retrievedPeer->ipAddress, "192.168.1.1");
    EXPECT_EQ(retrievedPeer->port, 8080);
}

TEST_F(DHTTest, GetNonExistentPeer) {
    DHT dht(NodeId::random(), defaultConfig);

    NodeId nonExistentId = NodeId::zero();
    auto retrievedPeer = dht.getPeer(nonExistentId);

    EXPECT_FALSE(retrievedPeer.has_value());
}

TEST_F(DHTTest, FindClosestPeers) {
    DHT dht(NodeId::random(), defaultConfig);

    NodeId targetId = NodeId::random();
    for (int i = 0; i < 30; ++i) {
        NodeId nodeId = NodeId::random();
        PeerInfo peer{"192.168.1." + std::to_string(i), static_cast<uint16_t>(8000 + i),
                      std::chrono::system_clock::now(), nodeId};
        dht.addPeer(peer);
    }

    auto closestPeers = dht.findClosestPeers(targetId);

    EXPECT_LE(closestPeers.size(), 20);
    EXPECT_GT(closestPeers.size(), 0);
}

TEST_F(DHTTest, StalePeerRemoval) {
    defaultConfig.staleThreshold = std::chrono::seconds(1);
    DHT dht(NodeId::random(), defaultConfig);

    NodeId nodeId = NodeId::random();
    PeerInfo peer{"192.168.1.1", 8080, std::chrono::system_clock::now() - std::chrono::seconds(2), nodeId};

    dht.addPeer(peer);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    dht.refresh();
    auto retrievedPeer = dht.getPeer(nodeId);
    EXPECT_FALSE(retrievedPeer.has_value());
}
