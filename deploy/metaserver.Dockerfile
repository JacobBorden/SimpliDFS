FROM alpine:3.19
ARG VERSION=v0.9.2
RUN apk add --no-cache curl ca-certificates
RUN curl -L https://github.com/JacobBorden/SimpliDFS/releases/download/${VERSION}/simplidfs-metaserver -o /usr/local/bin/metaserver \
    && chmod +x /usr/local/bin/metaserver
ENTRYPOINT ["/usr/local/bin/metaserver"]
