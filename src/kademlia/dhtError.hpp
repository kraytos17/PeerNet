#pragma once

#include <cstdint>

enum class DHTError : uint8_t {
    Success = 0,
    PeerNotFound,
    StaleData,
    NetworkError,
    PeerLimitExceeded,
    InvalidPeer,
    PingFailure,
    StorageError,
    LookupFailed
};
