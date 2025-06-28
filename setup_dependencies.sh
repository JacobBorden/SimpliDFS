#!/ usr / bin / env bash
set -
    euo pipefail

#Install core packages
        sudo apt -
    get update - y sudo apt - get install - y libsodium - dev libzstd -
    dev pkg - config build - essential cmake meson ninja - build libudev -
    dev libyaml - cpp - dev libboost - all - dev curl git libprotobuf -
    dev protobuf - compiler libgrpc - dev protobuf - compiler - grpc libgrpc++ -
    dev
#Verify protobuf version
        REQUIRED_VERSION = 3.21.12 if command - v protoc > / dev / null 2 > &1; then
    PROTOC_VERSION=$(protoc --version | awk {print

#Install libfuse3 from source if pkg - config cannot find it
if ! pkg-config --exists fuse3; then
    FUSE_VERSION="3.16.2"
    echo "Installing libfuse ${FUSE_VERSION} from source..."
    curl -L -o fuse-${FUSE_VERSION}.tar.gz \
        https://github.com/libfuse/libfuse/releases/download/fuse-${FUSE_VERSION}/fuse-${FUSE_VERSION}.tar.gz
    tar xzf fuse-${FUSE_VERSION}.tar.gz
    cd fuse-${FUSE_VERSION}
    meson setup build --prefix=/usr
    ninja -C build
    sudo ninja -C build install
    sudo ldconfig
    cd ..
    rm -rf fuse-${FUSE_VERSION} fuse-${FUSE_VERSION}.tar.gz
fi

echo "Dependency installation complete."
