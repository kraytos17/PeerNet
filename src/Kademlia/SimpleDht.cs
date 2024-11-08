using System.Buffers;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;

namespace PeerNet.Kademlia;

public sealed class SimpleDht {
    private const int KeySizeBytes = 20; // 160-bit keys
    private const int K = 20; // Standard Kademlia bucket size
    private const int Alpha = 3; // Number of parallel lookups
    private static readonly TimeSpan RefreshInterval = TimeSpan.FromHours(1);
    private static readonly TimeSpan NodeTimeout = TimeSpan.FromSeconds(15);

    private readonly byte[] _nodeId = new byte[KeySizeBytes];
    private readonly ConcurrentDictionary<long, KBucket> _routingTable = new();
    private readonly Socket _socket;
    private readonly CancellationTokenSource _cts = new();
    private readonly ConcurrentDictionary<string, byte[]> _storage = new();
    private readonly ArrayPool<byte> _arrayPool = ArrayPool<byte>.Shared;

    private readonly record struct NodeInfo(byte[] NodeId, IPEndPoint EndPoint, long LastSeen, int FailCount) {
        public byte[] CalculateDistance(byte[] targetNodeId) {
            var res = new byte[KeySizeBytes];
            for (var i = 0; i < KeySizeBytes; ++i) res[i] = (byte)(NodeId[i] ^ targetNodeId[i]);

            return res;
        }

        public bool IsStale(long now) => now - LastSeen > NodeTimeout.Ticks;
    }

    private sealed class KBucket(int maxSize) {
        private readonly ConcurrentDictionary<string, NodeInfo> _nodes = new();
        private readonly ReaderWriterLockSlim _locker = new();

        public bool TryAdd(NodeInfo nodeInfo) {
            var nodeKey = Convert.ToHexString(nodeInfo.NodeId);
            _locker.EnterUpgradeableReadLock();
            try {
                if (_nodes.TryGetValue(nodeKey, out var existing)) {
                    if (nodeInfo.LastSeen < existing.LastSeen) {
                        _nodes[nodeKey] = nodeInfo;
                        return true;
                    }

                    return false;
                }

                if (_nodes.Count >= maxSize) {
                    _locker.EnterReadLock();
                    try {
                        var staleNode = _nodes.Values.Where(n => n.FailCount > 0)
                                              .OrderByDescending(n => n.FailCount)
                                              .ThenBy(n => n.LastSeen)
                                              .FirstOrDefault();

                        if (staleNode.NodeId is not null) {
                            _nodes.TryRemove(Convert.ToHexString(staleNode.NodeId), out _);
                            _nodes[nodeKey] = nodeInfo;
                            return true;
                        }

                        return false;
                    }
                    finally {
                        _locker.ExitReadLock();
                    }
                }

                _nodes[nodeKey] = nodeInfo;
                return true;
            }
            finally {
                _locker.ExitUpgradeableReadLock();
            }
        }

        public IEnumerable<NodeInfo> GetNodes() {
            _locker.EnterReadLock();
            try {
                return _nodes.Values.ToList();
            }
            finally {
                _locker.ExitReadLock();
            }
        }

        public bool TryUpdate(byte[] id, Func<NodeInfo, NodeInfo> updateFunc) {
            var key = Convert.ToHexString(id);
            _locker.EnterWriteLock();
            try {
                if (_nodes.TryGetValue(key, out var nodeInfo)) {
                    _nodes[key] = updateFunc(nodeInfo);
                    return true;
                }

                return false;
            }
            finally {
                _locker.ExitWriteLock();
            }
        }
    }

    public SimpleDht(int port) {
        RandomNumberGenerator.Fill(_nodeId);
        for (var i = 0; i < KeySizeBytes * 8; ++i) _routingTable[i] = new KBucket(K);

        _socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp) {
            ReceiveBufferSize = 65536,
            SendBufferSize = 65536
        };

        _socket.Bind(new IPEndPoint(IPAddress.Any, port));
        _ = Task.Run(ProcessMessageAsync);
        _ = Task.Run(RefreshBucketsAsync);
    }

    private static int GetBucketIdx(byte[] distance) {
        for (var i = 0; i < KeySizeBytes; ++i) {
            if (distance[i] == 0) continue;

            for (var j = 0; j < 8; ++j)
                if ((distance[i] & (1 << (7 - j))) != 0)
                    return i * 8 + j;
        }

        return KeySizeBytes * 8 - 1;
    }
}