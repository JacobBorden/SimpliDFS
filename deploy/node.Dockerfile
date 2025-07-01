# Ensure the base image matches the static binary architecture.
ARG TARGETARCH=amd64
FROM --platform=linux/${TARGETARCH} alpine:3.19
# VERSION is the GitHub release tag (e.g. "v0.11.41" or "v0.11.41-devel")
ARG VERSION
RUN apk add --no-cache curl ca-certificates
RUN curl -L https://github.com/JacobBorden/SimpliDFS/releases/download/${VERSION}/node -o /usr/local/bin/node && \
    chmod +x /usr/local/bin/node
ENTRYPOINT ["/usr/local/bin/node"]
