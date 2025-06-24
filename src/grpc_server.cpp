#include "grpc/grpc_server.h"
#include "proto/filesystem.grpc.pb.h"
#include "utilities/filesystem.h"
#include "utilities/svid_fetcher.h"
#include <cstdlib>
#include <grpcpp/grpcpp.h>
#include <memory>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using simplidfs::CreateFileRequest;
using simplidfs::DeleteFileRequest;
using simplidfs::FileService;
using simplidfs::ReadFileRequest;
using simplidfs::ReadFileResponse;
using simplidfs::SimpleResponse;
using simplidfs::WriteFileRequest;

// ---------------------------------------------------------------------------
// gRPC service implementation
// ---------------------------------------------------------------------------
/** Implementation of the gRPC FileService. */
class FileServiceImpl final : public FileService::Service {
public:
  explicit FileServiceImpl(FileSystem &fs) : fs_(fs) {}

  // Create an empty file on disk
  Status CreateFile(ServerContext *, const CreateFileRequest *req,
                    SimpleResponse *resp) override {
    resp->set_ok(fs_.createFile(req->name()));
    return Status::OK;
  }

  // Write content to a file
  Status WriteFile(ServerContext *, const WriteFileRequest *req,
                   SimpleResponse *resp) override {
    resp->set_ok(fs_.writeFile(req->name(), req->content()));
    return Status::OK;
  }

  // Read file contents and return them to the caller
  Status ReadFile(ServerContext *, const ReadFileRequest *req,
                  ReadFileResponse *resp) override {
    std::string data = fs_.readFile(req->name());
    bool ok = !data.empty();
    resp->set_ok(ok);
    if (ok)
      resp->set_content(data);
    return Status::OK;
  }

  // Remove a file from disk
  Status DeleteFile(ServerContext *, const DeleteFileRequest *req,
                    SimpleResponse *resp) override {
    resp->set_ok(fs_.deleteFile(req->name()));
    return Status::OK;
  }

private:
  FileSystem &fs_;
};

/** Start a gRPC server on the given address using the provided FileSystem. */
void RunGrpcServer(const std::string &address, FileSystem &fs) {
  // Construct the service implementation
  FileServiceImpl service(fs);

  // Configure and start the gRPC server
  int stream_window = 8 * 1024 * 1024; // default 8 MiB
  if (const char *env = std::getenv("SIMPLIDFS_STREAM_WINDOW_SIZE")) {
    int val = std::atoi(env);
    if (val > 0) {
      stream_window = val;
    }
  }

  ServerBuilder builder;
  builder.AddChannelArgument("grpc.grpc_http2_stream_window_size",
                             stream_window);

  auto svid = Utilities::FetchSVID();
  if (svid) {
    grpc::SslServerCredentialsOptions opts;
    grpc::SslServerCredentialsOptions::PemKeyCertPair pair;
    pair.private_key = Utilities::DerToPem(svid->key_der, "PRIVATE KEY");
    pair.cert_chain = Utilities::DerToPem(svid->cert_der, "CERTIFICATE");
    opts.pem_key_cert_pairs.push_back(pair);
    builder.AddListeningPort(address, grpc::SslServerCredentials(opts));
  } else {
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  }
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  // Block the current thread until the server shuts down
  if (server) {
    server->Wait();
  }
}
