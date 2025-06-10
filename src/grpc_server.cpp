#include "grpc/grpc_server.h"
#include "grpc/proto/filesystem.grpc.pb.h"
#include "utilities/filesystem.h"
#include <grpcpp/grpcpp.h>
#include <memory>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using simplidfs::FileService;
using simplidfs::CreateFileRequest;
using simplidfs::WriteFileRequest;
using simplidfs::ReadFileRequest;
using simplidfs::DeleteFileRequest;
using simplidfs::SimpleResponse;
using simplidfs::ReadFileResponse;

// ---------------------------------------------------------------------------
// gRPC service implementation
// ---------------------------------------------------------------------------
/** Implementation of the gRPC FileService. */
class FileServiceImpl final : public FileService::Service {
public:
    explicit FileServiceImpl(FileSystem& fs) : fs_(fs) {}

    // Create an empty file on disk
    Status CreateFile(ServerContext*, const CreateFileRequest* req,
                      SimpleResponse* resp) override {
        resp->set_ok(fs_.createFile(req->name()));
        return Status::OK;
    }

    // Write content to a file
    Status WriteFile(ServerContext*, const WriteFileRequest* req,
                     SimpleResponse* resp) override {
        resp->set_ok(fs_.writeFile(req->name(), req->content()));
        return Status::OK;
    }

    // Read file contents and return them to the caller
    Status ReadFile(ServerContext*, const ReadFileRequest* req,
                    ReadFileResponse* resp) override {
        std::string data = fs_.readFile(req->name());
        bool ok = !data.empty();
        resp->set_ok(ok);
        if (ok) resp->set_content(data);
        return Status::OK;
    }

    // Remove a file from disk
    Status DeleteFile(ServerContext*, const DeleteFileRequest* req,
                      SimpleResponse* resp) override {
        resp->set_ok(fs_.deleteFile(req->name()));
        return Status::OK;
    }

private:
    FileSystem& fs_;
};

/** Start a gRPC server on the given address using the provided FileSystem. */
void RunGrpcServer(const std::string& address, FileSystem& fs) {
    // Construct the service implementation
    FileServiceImpl service(fs);

    // Configure and start the gRPC server
    ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());

    // Block the current thread until the server shuts down
    if (server) {
        server->Wait();
    }
}
