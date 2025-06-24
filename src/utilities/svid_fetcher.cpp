#include "utilities/svid_fetcher.h"
#include "spiffe/workload/workload.grpc.pb.h"
#include <cstdlib>
#include <cstring>
#include <grpcpp/grpcpp.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;

namespace Utilities {

std::string DerToPem(const std::string &der, const char *type) {
  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio)
    return {};
  if (std::strcmp(type, "CERTIFICATE") == 0) {
    const unsigned char *ptr =
        reinterpret_cast<const unsigned char *>(der.data());
    X509 *cert = d2i_X509(nullptr, &ptr, der.size());
    if (!cert) {
      BIO_free(bio);
      return {};
    }
    PEM_write_bio_X509(bio, cert);
    X509_free(cert);
  } else if (std::strcmp(type, "PRIVATE KEY") == 0) {
    const unsigned char *ptr =
        reinterpret_cast<const unsigned char *>(der.data());
    EVP_PKEY *key = d2i_AutoPrivateKey(nullptr, &ptr, der.size());
    if (!key) {
      BIO_free(bio);
      return {};
    }
    PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
    EVP_PKEY_free(key);
  } else {
    BIO_free(bio);
    return {};
  }
  BUF_MEM *mem = nullptr;
  BIO_get_mem_ptr(bio, &mem);
  std::string pem(mem->data, mem->length);
  BIO_free(bio);
  return pem;
}

std::optional<SVIDData> FetchSVID(const std::string &socketPath) {
  std::string path = socketPath;
  if (path.empty()) {
    const char *env = std::getenv("SPIFFE_ENDPOINT_SOCKET");
    path = env ? env : "/tmp/spire-agent/public/api.sock";
  }

  auto channel =
      grpc::CreateChannel("unix://" + path, grpc::InsecureChannelCredentials());
  auto stub = SpiffeWorkloadAPI::NewStub(channel);
  ClientContext ctx;
  X509SVIDRequest req;
  std::unique_ptr<ClientReader<X509SVIDResponse>> reader(
      stub->FetchX509SVID(&ctx, req));
  X509SVIDResponse resp;
  if (reader && reader->Read(&resp)) {
    if (resp.svids_size() > 0) {
      const auto &svid = resp.svids(0);
      SVIDData out{
          std::string(svid.x509_svid().begin(), svid.x509_svid().end()),
          std::string(svid.x509_svid_key().begin(),
                      svid.x509_svid_key().end())};
      return out;
    }
  }
  return std::nullopt;
}

} // namespace Utilities
