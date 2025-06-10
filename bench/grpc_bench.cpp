#include "proto/filesystem.grpc.pb.h"
#include "utilities/filesystem.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>
#include <thread>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using simplidfs::FileService;
using simplidfs::CreateFileRequest;
using simplidfs::WriteFileRequest;
using simplidfs::ReadFileRequest;
using simplidfs::DeleteFileRequest;
using simplidfs::SimpleResponse;
using simplidfs::ReadFileResponse;

class FileServiceImpl final : public FileService::Service {
public:
    explicit FileServiceImpl(FileSystem& fs) : fs_(fs) {}
    grpc::Status CreateFile(grpc::ServerContext*, const CreateFileRequest* req,
                            SimpleResponse* resp) override {
        resp->set_ok(fs_.createFile(req->name()));
        return grpc::Status::OK;
    }
    grpc::Status WriteFile(grpc::ServerContext*, const WriteFileRequest* req,
                           SimpleResponse* resp) override {
        resp->set_ok(fs_.writeFile(req->name(), req->content()));
        return grpc::Status::OK;
    }
    grpc::Status ReadFile(grpc::ServerContext*, const ReadFileRequest* req,
                          ReadFileResponse* resp) override {
        std::string data = fs_.readFile(req->name());
        bool ok = !data.empty();
        resp->set_ok(ok);
        if(ok) resp->set_content(data);
        return grpc::Status::OK;
    }
    grpc::Status DeleteFile(grpc::ServerContext*, const DeleteFileRequest* req,
                            SimpleResponse* resp) override {
        resp->set_ok(fs_.deleteFile(req->name()));
        return grpc::Status::OK;
    }
private:
    FileSystem& fs_;
};

static void benchGrpc(FileService::Stub& stub, int iterations) {
    auto start = std::chrono::steady_clock::now();
    for(int i=0;i<iterations;i++) {
        CreateFileRequest cReq; cReq.set_name("file"+std::to_string(i));
        grpc::ClientContext ctx;
        SimpleResponse resp;
        stub.CreateFile(&ctx, cReq, &resp);
    }
    auto end = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
    std::cout << "gRPC create avg " << us/iterations << " us" << std::endl;
}

int main() {
    FileSystem fs;
    FileServiceImpl service(fs);
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::thread serverThread([&](){ server->Wait(); });
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    auto stub = FileService::NewStub(channel);
    benchGrpc(*stub, 1000);
    server->Shutdown();
    serverThread.join();
    return 0;
}
