#!/bin/bash
set -e

# Install core packages
sudo zypper refresh
sudo zypper install -y libsodium-devel libzstd-devel pkgconf-pkg-config \
    gcc-c++ make cmake meson ninja libudev-devel yaml-cpp-devel \
    boost-devel curl git \
    protobuf-devel protobuf grpc-devel libgrpc++-devel libprotobuf-lite-devel

# Install libfuse3 from source if pkg-config cannot find it
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
