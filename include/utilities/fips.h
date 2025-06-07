#pragma once
#include <cstddef>
/**
 * @brief Perform a basic FIPS self test.
 *
 * The test verifies libsodium's SHA-256 implementation by hashing a known
 * string and comparing the result to a hard coded value.
 *
 * @return true if the digest matches, otherwise false.
 */
bool fips_self_test();
