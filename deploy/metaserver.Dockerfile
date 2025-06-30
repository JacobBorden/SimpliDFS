# Explicitly set the build platform so arm64 hosts don't produce
# images with mismatched binaries.
ARG TARGETARCH=amd64
FROM --platform=linux/${TARGETARCH} alpine:3.19
ARG VERSION
RUN apk add --no-cache curl ca-certificates
# Strip any "-devel" suffix so development snapshots resolve to the
# corresponding stable release assets.
RUN RELEASE_VERSION="${VERSION%-devel}" && \
    curl -L https://github.com/JacobBorden/SimpliDFS/releases/download/${RELEASE_VERSION}/metaserver -o /usr/local/bin/metaserver && \
    chmod +x /usr/local/bin/metaserver
ENTRYPOINT ["/usr/local/bin/metaserver"]
