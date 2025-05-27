#ifndef BLOCKIO_HPP
#define BLOCKIO_HPP

#include <vector>
#include <span>
#include <cstddef> // For std::byte

class BlockIO {
public:
    // Appends data to the internal buffer.
    void ingest(std::span<const std::byte> data);

    // Returns a copy of the concatenated plaintext data.
    std::vector<std::byte> finalize_raw();

private:
    std::vector<std::byte> buffer_;
};

#endif // BLOCKIO_HPP
