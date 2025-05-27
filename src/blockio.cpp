#include "blockio.hpp"

#include <algorithm> // For std::copy

void BlockIO::ingest(const std::byte* data, size_t size) {
    if (data && size > 0) { // Check for null data pointer and non-zero size
        buffer_.insert(buffer_.end(), data, data + size);
    }
}

std::vector<std::byte> BlockIO::finalize_raw() {
    // Return a copy of the buffer.
    return buffer_;
}
