#!/ usr / bin / env bash
#install - deps - opensuse.sh
#Installs the build dependencies for SimpliDFS / Neurodeck on openSUSE.
#Works on both Tumbleweed and Leap ≥15.5.

set - euo pipefail

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#Helper : rpm - q returns 0 if the package is installed
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
      have() {
  rpm - q "$1" & > / dev / null;
}

echo "🔄  Refreshing repositories …"
sudo zypper --gpg-auto-import-keys --non-interactive refresh

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#Core development stack
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
core_pkgs=(
  gcc-c++         make            cmake           ninja
  meson           pkgconf-pkg-config
  libsodium-devel libzstd-devel   libudev-devel
  yaml-cpp-devel  boost-devel     curl            git
  protobuf-devel  grpc-devel
)

missing=()
for p in "${core_pkgs[@]}";
do
  have "$p" || missing += ("$p")done

      if (($ { #missing[@] }));
then echo "📦  Installing: ${missing[*]}" sudo zypper-- non -
    interactive install - y-- no -
    recommends "${missing[@]}" else echo
    "✅  All core packages already present." fi

#Verify protobuf version
        REQUIRED_VERSION = 3.21.12 if command - v protoc > / dev / null 2 > &1;
then PROTOC_VERSION =
    $(protoc-- version | awk '{print $2}') else PROTOC_VERSION =
        "0" fi if["$(printf '%s\n%s\n' " $PROTOC_VERSION " " $REQUIRED_VERSION
                  " | sort -V | head -n1)" != "$REQUIRED_VERSION"];
then echo "Installing protobuf ${REQUIRED_VERSION} from source …" tmpdir =
    $(mktemp - d) pushd "$tmpdir" >
    / dev / null curl - L -
        O "https://github.com/protocolbuffers/protobuf/releases/download/"
          "v${REQUIRED_VERSION}/protobuf-cpp-${REQUIRED_VERSION}.tar.gz" tar xf
          "protobuf-cpp-${REQUIRED_VERSION}.tar.gz" cd
          "protobuf-${REQUIRED_VERSION}"./
            configure-- prefix =
        / usr make - j "$(nproc)" sudo make install sudo ldconfig popd >
        / dev / null rm -
            rf "$tmpdir" fi

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#Fuse 3.x – prefer distro package, else build 3.16.2
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
            if pkg -
            config-- exists fuse3;
then echo "✅  fuse3 detected via pkg-config." else if zypper-- no -
            refresh se - i - x fuse3 - devel & >
        / dev / null ||
    zypper-- no - refresh se - x fuse3 - devel | grep - q '^i';
then echo "🔄  Installing fuse3-devel from repo …" sudo zypper-- non -
    interactive install - y fuse3 - devel else FUSE_VERSION =
    3.16.2 echo "🛠   Building libfuse $FUSE_VERSION from source …" tmpdir =
        $(mktemp - d) pushd "$tmpdir" curl - L -
        O "https://github.com/libfuse/libfuse/releases/download/"
          "fuse-${FUSE_VERSION}/fuse-${FUSE_VERSION}.tar.gz" tar xf
          "fuse-${FUSE_VERSION}.tar.gz" cd
          "fuse-${FUSE_VERSION}" meson setup build-- prefix =
            / usr - Dexamples = false ninja - C build sudo ninja -
                                C build install sudo ldconfig popd rm -
                                rf "$tmpdir" fi fi

                                    echo -
                                e "\n🎉  Dependency installation complete."
