#include "blockio.hpp"

#include <algorithm> // For std::copy

void BlockIO::ingest(std::span<const std::byte> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

std::vector<std::byte> BlockIO::finalize_raw() {
    // Return a copy of the buffer.
    return buffer_;
}
