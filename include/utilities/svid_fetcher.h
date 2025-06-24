#pragma once
#include <optional>
#include <string>

namespace Utilities {
struct SVIDData {
  std::string cert_der;
  std::string key_der;
};

std::optional<SVIDData> FetchSVID(const std::string &socketPath = "");

std::string DerToPem(const std::string &der, const char *type);
} // namespace Utilities
