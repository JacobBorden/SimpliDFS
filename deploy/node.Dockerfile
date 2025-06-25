FROM alpine:3.19
ARG VERSION=v0.9.2
RUN apk add --no-cache curl ca-certificates
RUN curl -L https://github.com/JacobBorden/SimpliDFS/releases/download/${VERSION}/simplidfs-node -o /usr/local/bin/node \
    && chmod +x /usr/local/bin/node
ENTRYPOINT ["/usr/local/bin/node"]
