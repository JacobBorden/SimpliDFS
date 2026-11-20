// SPDX-License-Identifier: MIT
#include "../../../tests/fuse_read_diagnostics.hpp"
#include <sstream>

int main() {
  for (const std::string &input :
       {std::string{}, std::string("a\nb\n"), std::string("a\nb"),
        std::string("\0\xff\n", 3), std::string(1000, 'x')}) {
    std::istringstream stream(input);
    FuseReadDiagnostics diagnostic;
    std::string line;
    while (std::getline(stream, line)) {
      diagnostic.observe(line);
      if (!stream.eof())
        diagnostic.observe("\n");
    }
    FuseReadDiagnostics expected;
    expected.observe(input);
    if (diagnostic.bytes != input.size() ||
        diagnostic.prefix_hex != expected.prefix_hex ||
        diagnostic.prefix_hex.size() > 128)
      return 1;
  }
  FuseReadDiagnostics binary;
  binary.observe(std::string("\0\xff\n", 3));
  return binary.prefix_hex == "00ff0a" ? 0 : 1;
}
