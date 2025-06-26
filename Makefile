VERSION := $(shell cat VERSION)
V_TAG := v$(VERSION)
.PHONY: devnet build-images

build-images:
        docker build --build-arg VERSION=$(V_TAG) -t simplidfs-metaserver:$(VERSION) -f deploy/metaserver.Dockerfile .
        docker build --build-arg VERSION=$(V_TAG) -t simplidfs-node:$(VERSION) -f deploy/node.Dockerfile .

devnet: build-images
	docker compose -f deploy/docker-compose.yml up

