#include "utilities/cid_utils.hpp"
#include "utilities/digest.hpp"
#include "gtest/gtest.h" // Google Test header

#include "cppcodec/base32_rfc4648.hpp" // For encoding test data
#include <array>
#include <cstdint>  // For uint8_t
#include <iostream> // For potential debug output (optional)
#include <random> // For std::random_device, std::mt19937, std::uniform_int_distribution
#include <stdexcept> // For std::runtime_error
#include <vector>

// Fuzz test for CID conversion
TEST(CIDConversionFuzzTest, RoundTripConsistency) {
  const int num_iterations = 10000;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint8_t> distrib(0, 255);

  for (int i = 0; i < num_iterations; ++i) {
    std::array<uint8_t, sgns::utils::DIGEST_SIZE> original_digest;
    for (size_t j = 0; j < sgns::utils::DIGEST_SIZE; ++j) {
      original_digest[j] = distrib(gen);
    }

    // Convert digest to CID
    std::string cid_str;
    ASSERT_NO_THROW(cid_str = sgns::utils::digest_to_cid(original_digest));

    // Convert CID back to digest
    std::array<uint8_t, sgns::utils::DIGEST_SIZE> round_tripped_digest;
    ASSERT_NO_THROW(round_tripped_digest = sgns::utils::cid_to_digest(cid_str));

    // Assert that the original and round-tripped digests are identical
    ASSERT_EQ(original_digest, round_tripped_digest);

    // Optional: print progress or a sample CID
    // if (i % 1000 == 0 && i > 0) {
    //     std::cout << "Fuzz test iteration: " << i << ", CID: " << cid_str <<
    //     std::endl;
    // }
  }
}

// Test cases for invalid CID inputs
TEST(CIDInvalidInputTest, HandlesInvalidCIDs) {
  // 1. Empty CID
  std::string empty_cid = "";
  ASSERT_THROW(sgns::utils::cid_to_digest(empty_cid), std::runtime_error);

  // 2. CID too short (after base32 decoding) to contain prefix
  std::string short_cid_prefix = "b"; // Decodes to too few bytes for prefix
  ASSERT_THROW(sgns::utils::cid_to_digest(short_cid_prefix),
               std::runtime_error);

  std::string short_cid_slightly_longer =
      "bahca"; // prefix is 0x01, 0x70 - still too short for full prefix
  ASSERT_THROW(sgns::utils::cid_to_digest(short_cid_slightly_longer),
               std::runtime_error);

  // 3. CID with invalid Base32 characters
  // A valid prefix + valid hash length + invalid base32 characters for the hash
  // part
  std::array<uint8_t, sgns::utils::DIGEST_SIZE> dummy_digest;
  dummy_digest.fill(0);
  std::string valid_cid_start = sgns::utils::digest_to_cid(dummy_digest);
  std::string invalid_base32_cid =
      valid_cid_start.substr(0, valid_cid_start.length() - 1) +
      "!"; // '!' is not valid base32
  ASSERT_THROW(
      sgns::utils::cid_to_digest(invalid_base32_cid),
      std::runtime_error); // cppcodec throws std::out_of_range for this, caught
                           // as runtime_error in cid_utils

  // 4. CID with correct Base32 but wrong prefix (e.g., wrong CID version)
  std::vector<uint8_t> bad_prefix_bytes = {0x02, 0x70, 0x12,
                                           0x20}; // Changed 0x01 to 0x02
  for (int i = 0; i < 32; ++i)
    bad_prefix_bytes.push_back(static_cast<uint8_t>(i)); // Dummy data
  std::string bad_prefix_cid;
  ASSERT_NO_THROW(bad_prefix_cid =
                      cppcodec::base32_rfc4648::encode(bad_prefix_bytes));
  ASSERT_THROW(sgns::utils::cid_to_digest(bad_prefix_cid), std::runtime_error);

  // 5. CID with correct prefix but wrong length field in prefix (e.g., 0x21
  // instead of 0x20)
  std::vector<uint8_t> bad_length_bytes = {0x01, 0x70, 0x12,
                                           0x21}; // 0x21 = 33 bytes length
  for (int i = 0; i < 32; ++i)
    bad_length_bytes.push_back(
        static_cast<uint8_t>(i)); // Still 32 bytes of actual data after prefix
  std::string bad_length_cid;
  ASSERT_NO_THROW(bad_length_cid =
                      cppcodec::base32_rfc4648::encode(bad_length_bytes));
  ASSERT_THROW(sgns::utils::cid_to_digest(bad_length_cid),
               std::runtime_error); // Expects 33 bytes of hash, gets 32.

  // 6. CID with correct prefix and length field, but actual data is too short
  std::vector<uint8_t> short_data_bytes = {0x01, 0x70, 0x12,
                                           0x20}; // Expects 32 bytes of hash
  for (int i = 0; i < 31; ++i)
    short_data_bytes.push_back(
        static_cast<uint8_t>(i)); // Only 31 bytes of data provided after prefix
  std::string short_data_cid;
  ASSERT_NO_THROW(short_data_cid =
                      cppcodec::base32_rfc4648::encode(short_data_bytes));
  ASSERT_THROW(sgns::utils::cid_to_digest(short_data_cid),
               std::runtime_error); // Decoded data too short overall.
}

TEST(CIDToBytesTest, ProducesCorrectByteVector) {
  std::array<uint8_t, sgns::utils::DIGEST_SIZE> digest;
  for (size_t i = 0; i < digest.size(); ++i) {
    digest[i] = static_cast<uint8_t>(i);
  }
  std::string cid = sgns::utils::digest_to_cid(digest);

  std::vector<uint8_t> result = sgns::utils::cid_to_bytes(cid);

  std::vector<uint8_t> expected(sgns::utils::CID_PREFIX_BLAKE3.begin(),
                                sgns::utils::CID_PREFIX_BLAKE3.end());
  expected.insert(expected.end(), digest.begin(), digest.end());
  ASSERT_EQ(result, expected);
}
