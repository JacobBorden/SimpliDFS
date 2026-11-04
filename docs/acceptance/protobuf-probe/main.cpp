#include "proto/filesystem.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <string>

int main() {
  simplidfs::WriteFileRequest original;
  original.set_name("disposable-probe");
  original.set_content(std::string("a\0b", 3));
  std::string wire;
  if (!original.SerializeToString(&wire))
    return 1;
  simplidfs::WriteFileRequest restored;
  if (!restored.ParseFromString(wire) || restored.name() != original.name() ||
      restored.content() != original.content())
    return 2;
  auto stub = simplidfs::FileService::NewStub(
      grpc::CreateChannel("localhost:1", grpc::InsecureChannelCredentials()));
  return stub ? 0 : 3;
}
