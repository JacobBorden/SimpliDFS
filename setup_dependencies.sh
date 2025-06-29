#!/bin/bash
set -euo pipefail

# Desired Protobuf release
REQUIRED_VERSION=3.21.12

# Install core packages
sudo apt-get update -y
sudo apt-get install -y libsodium-dev libzstd-dev pkg-config \
    build-essential cmake meson ninja-build libudev-dev libyaml-cpp-dev \
    libboost-all-dev curl git \
    libprotobuf-dev protobuf-compiler libgrpc-dev protobuf-compiler-grpc \
    libgrpc++-dev libc-ares-dev libcares2

# c-ares package ships libcares_static.a; create libcares.a if missing
LIBDIR=$(pkg-config --variable=libdir libcares)
if [ -f "$LIBDIR/libcares_static.a" ] && [ ! -e "$LIBDIR/libcares.a" ]; then
    sudo ln -s libcares_static.a "$LIBDIR/libcares.a"
fi
# Verify protobuf version
if command -v protoc >/dev/null; then
    PROTOC_VERSION=$(protoc --version | awk '{print $2}')
else
    PROTOC_VERSION="0"
fi
if [ "$PROTOC_VERSION" != "$REQUIRED_VERSION" ]; then
    echo "Installing protobuf ${REQUIRED_VERSION} from source..."
    tmpdir=$(mktemp -d)
    pushd "$tmpdir" >/dev/null
    curl -L -O "https://github.com/protocolbuffers/protobuf/releases/download/v${REQUIRED_VERSION}/protobuf-cpp-${REQUIRED_VERSION}.tar.gz"
    tar xzf "protobuf-cpp-${REQUIRED_VERSION}.tar.gz"
    cd "protobuf-${REQUIRED_VERSION}"
    ./configure --prefix=/usr
    make -j"$(nproc)"
    sudo make install
    sudo ldconfig
    popd >/dev/null
    rm -rf "$tmpdir"
fi

# Install libfuse3 from source if pkg-config cannot find it
if ! pkg-config --exists fuse3; then
    FUSE_VERSION="3.16.2"
    echo "Installing libfuse ${FUSE_VERSION} from source..."
    curl -L -o fuse-${FUSE_VERSION}.tar.gz \
        https://github.com/libfuse/libfuse/releases/download/fuse-${FUSE_VERSION}/fuse-${FUSE_VERSION}.tar.gz
    tar xzf fuse-${FUSE_VERSION}.tar.gz
    cd fuse-${FUSE_VERSION}
    meson setup build --prefix=/usr -Ddefault_library=both
    ninja -C build
    sudo ninja -C build install
    sudo ldconfig
    cd ..
    rm -rf fuse-${FUSE_VERSION} fuse-${FUSE_VERSION}.tar.gz
fi

echo "Dependency installation complete." 
