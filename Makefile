VERSION := $(shell cat VERSION)
.PHONY: devnet build-images

build-images:
        docker build -t simplidfs-metaserver:$(VERSION) -f deploy/metaserver.Dockerfile .
        docker build -t simplidfs-node:$(VERSION) -f deploy/node.Dockerfile .

devnet: build-images
	docker compose -f deploy/docker-compose.yml up

