# Explicitly set the build platform so arm64 hosts don't produce
# images with mismatched binaries.
ARG TARGETARCH=amd64
FROM --platform=linux/${TARGETARCH} alpine:3.19
# VERSION corresponds to the GitHub release tag, e.g. "v0.11.41" or
# "v0.11.41-devel".
ARG VERSION
RUN apk add --no-cache curl ca-certificates
RUN curl -L https://github.com/JacobBorden/SimpliDFS/releases/download/${VERSION}/metaserver -o /usr/local/bin/metaserver && \
    chmod +x /usr/local/bin/metaserver
ENTRYPOINT ["/usr/local/bin/metaserver"]
