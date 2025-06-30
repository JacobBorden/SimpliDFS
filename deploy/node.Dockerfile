FROM --platform=linux/amd64 alpine:3.19
ARG VERSION
RUN apk add --no-cache curl ca-certificates
# Development tags end with "-devel" but binaries live under the stable
# release. Trim the suffix before downloading.
RUN RELEASE_VERSION="${VERSION%-devel}" && \
    curl -L https://github.com/JacobBorden/SimpliDFS/releases/download/${RELEASE_VERSION}/node -o /usr/local/bin/node && \
    chmod +x /usr/local/bin/node
ENTRYPOINT ["/usr/local/bin/node"]
