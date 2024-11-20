#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include "../src/kademlia.hpp"

// Test fixture for DHT tests
class DHTTest : public ::testing::Test {
protected:
    DHT::Config defaultConfig;

    void SetUp() override {
        // Default configuration for tests
        defaultConfig.refreshInterval = std::chrono::seconds(1);
        defaultConfig.staleThreshold = std::chrono::seconds(5);
        defaultConfig.maxPeers = 100;
        defaultConfig.k = 20;
    }
};

// NodeId Tests
TEST(NodeIdTest, RandomGeneration) {
    NodeId id1 = NodeId::random();
    NodeId id2 = NodeId::random();

    // Verify that two random IDs are different
    EXPECT_NE(id1, id2);

    // Verify the size of the NodeId is 20 bytes
    EXPECT_EQ(id1.bytes().size(), 20);
}

TEST(NodeIdTest, DistanceCalculation) {
    std::array<uint8_t, 20> bytes1 = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::array<uint8_t, 20> bytes2 = {1, 2, 3, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    NodeId id1(bytes1);
    NodeId id2(bytes2);

    NodeId distance = id1.distanceTo(id2);

    // Check XOR result for the 4th byte (4 ^ 5)
    EXPECT_EQ(distance.bytes()[3], 1);

    // Ensure symmetry of distance calculation
    EXPECT_EQ(id1.distanceTo(id2), id2.distanceTo(id1));
}

TEST(NodeIdTest, HashFunction) {
    NodeId id1 = NodeId::random();
    NodeId id2 = NodeId::random();

    NodeId::Hash hasher;

    // Verify that different NodeIds have different hash values
    EXPECT_NE(hasher(id1), hasher(id2));

    // Verify that the same NodeId consistently yields the same hash value
    EXPECT_EQ(hasher(id1), hasher(id1));
}

// DHT Tests
TEST_F(DHTTest, AddPeer) {
    DHT dht(defaultConfig);

    NodeId nodeId1 = NodeId::random();
    PeerInfo peer1{"192.168.1.1", 8080, std::chrono::system_clock::now(), nodeId1};

    // Add peer should succeed and return no error
    auto result = dht.addPeer(nodeId1, peer1);
    EXPECT_FALSE(result.has_value());

    // Verify that peer count has increased to 1
    EXPECT_EQ(dht.getPeerCount(), 1);
}

TEST_F(DHTTest, AddPeerMaxLimit) {
    // Set maxPeers to 1 to test peer limit
    defaultConfig.maxPeers = 1;
    DHT dht(defaultConfig);

    NodeId nodeId1 = NodeId::random();
    NodeId nodeId2 = NodeId::random();

    PeerInfo peer1{"192.168.1.1", 8080, std::chrono::system_clock::now(), nodeId1};
    PeerInfo peer2{"192.168.1.2", 8081, std::chrono::system_clock::now(), nodeId2};

    // First peer should be added successfully
    auto result1 = dht.addPeer(nodeId1, peer1);
    EXPECT_FALSE(result1.has_value());

    // Second peer addition should fail due to max peer limit
    auto result2 = dht.addPeer(nodeId2, peer2);
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), DHTError::PeerLimitExceeded);
}

TEST_F(DHTTest, GetPeer) {
    DHT dht(defaultConfig);

    NodeId nodeId = NodeId::random();
    PeerInfo peer{"192.168.1.1", 8080, std::chrono::system_clock::now(), nodeId};

    dht.addPeer(nodeId, peer);

    // Retrieve the peer and verify its properties
    auto retrievedPeer = dht.getPeer(nodeId);
    ASSERT_TRUE(retrievedPeer.has_value());
    EXPECT_EQ(retrievedPeer->ipAddress, "192.168.1.1");
    EXPECT_EQ(retrievedPeer->port, 8080);
}

TEST_F(DHTTest, GetNonExistentPeer) {
    DHT dht(defaultConfig);

    NodeId nonExistentId = NodeId::zero();
    auto retrievedPeer = dht.getPeer(nonExistentId);

    // Ensure the optional has no value, indicating peer not found
    EXPECT_FALSE(retrievedPeer.has_value());
}

TEST_F(DHTTest, FindClosestPeers) {
    DHT dht(defaultConfig);

    NodeId targetId = NodeId::random();
    for (int i = 0; i < 30; ++i) {
        NodeId nodeId = NodeId::random();
        PeerInfo peer{"192.168.1." + std::to_string(i), static_cast<uint16_t>(8000 + i),
                      std::chrono::system_clock::now(), nodeId};
        dht.addPeer(nodeId, peer);
    }

    // Retrieve closest peers to targetId
    auto closestPeers = dht.findClosestPeers(targetId);

    // Default `k` is 20, so expect up to 20 closest peers
    EXPECT_LE(closestPeers.size(), 20);
    EXPECT_GT(closestPeers.size(), 0);
}

TEST_F(DHTTest, StalePeerRemoval) {
    // Set stale threshold to 1 second for quick testing
    defaultConfig.staleThreshold = std::chrono::seconds(1);
    DHT dht(defaultConfig);

    NodeId nodeId = NodeId::random();
    PeerInfo peer{"192.168.1.1", 8080,
                  std::chrono::system_clock::now() - std::chrono::seconds(2), // Peer is stale
                  nodeId};

    dht.addPeer(nodeId, peer);

    // Wait to allow for stale peer removal during background refresh
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Attempt to retrieve stale peer
    auto retrievedPeer = dht.getPeer(nodeId);
    EXPECT_FALSE(retrievedPeer.has_value()); // Peer should be removed
}
