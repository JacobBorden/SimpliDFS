// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Thread compatible: used by the verifier after all appender threads join.
struct FuseReadDiagnostics {
  std::size_t bytes = 0;
  std::string prefix_hex;

  void observe(std::string_view data) {
    constexpr char hex[] = "0123456789abcdef";
    bytes += data.size();
    for (unsigned char byte : data) {
      if (prefix_hex.size() == 128)
        break;
      prefix_hex += hex[byte >> 4];
      prefix_hex += hex[byte & 15];
    }
  }
};
