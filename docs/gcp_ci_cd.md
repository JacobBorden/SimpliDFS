#GCP CI/CD Deployment

This guide outlines how to run the metaserver on Google Cloud and keep it up to date automatically.

## 1. Build Docker Images
1. Configure Docker for the Google Container Registry:
   ```sh
   gcloud auth configure-docker
   ```
2. Build the metaserver image using the provided Dockerfile. Pass the repository
   version as a build argument. Development snapshots are tagged with `-devel`,
   but the Dockerfiles automatically strip this suffix when downloading
   binaries:
   ```sh
   VERSION=$(cat VERSION)
   docker build --build-arg VERSION=v${VERSION} -f deploy/metaserver.Dockerfile \
     -t us-docker.pkg.dev/<PROJECT_ID>/simplidfs/simplidfs-metaserver:${VERSION} .
   ```
3. Build the storage node image if needed. The same `-devel` suffix is ignored
   during the build:
   ```sh
   VERSION=$(cat VERSION)
   docker build --build-arg VERSION=v${VERSION} -f deploy/node.Dockerfile \
     -t us-docker.pkg.dev/<PROJECT_ID>/simplidfs/simplidfs-node:${VERSION} .
   ```

## 2. Push Images to Google Container Registry
After building, push the images so your VM or cluster can pull them:
```sh
docker push us-docker.pkg.dev/<PROJECT_ID>/simplidfs/simplidfs-metaserver:<TAG>
docker push us-docker.pkg.dev/<PROJECT_ID>/simplidfs/simplidfs-node:<TAG>
```

## 3. Deploy the Container
On a Compute Engine VM (or in a GKE cluster), pull the image and run it as a service. A minimal systemd unit might look like:
```ini
[Unit]
Description=SimpliDFS Metaserver
After=docker.service
Requires=docker.service

[Service]
Restart=always
ExecStart=/usr/bin/docker run --rm \
  --name metaserver \
  -p 8080:8080 \
  us-docker.pkg.dev/<PROJECT_ID>/simplidfs/simplidfs-metaserver:<TAG>

[Install]
WantedBy=multi-user.target
```
Enable the unit with `systemctl enable --now simplidfs-metaserver`.

## 4. GitHub Actions Workflow
The following workflow builds a new image on each tag, pushes it to GCR and restarts the service on a VM.
```yaml
name: Deploy Metaserver to GCP
on:
  push:
    tags: ['v*']
  workflow_run:
    workflows: ['Tag Development Snapshot']
    types:
      - completed

env:
  GIT_SHA: ${{ github.event.workflow_run.head_sha || github.sha }}

jobs:
  deploy:
    if: ${{ github.event_name == 'push' || github.event.workflow_run.conclusion == 'success' }}
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v4
    - id: auth
      uses: google-github-actions/auth@v2
      with:
        credentials_json: ${{ secrets.GCP_SA_KEY }}
    - uses: google-github-actions/setup-gcloud@v2
      with:
        project_id: ${{ secrets.GCP_PROJECT }}
    - name: Read version
      id: version
      run: echo "tag=v$(cat VERSION)" >> "$GITHUB_OUTPUT"
    - name: Build Docker image
      run: |
        docker build --build-arg VERSION=${{ steps.version.outputs.tag }} -f deploy/metaserver.Dockerfile \
          -t us-docker.pkg.dev/${{ secrets.GCP_PROJECT }}/simplidfs/simplidfs-metaserver:${{ env.GIT_SHA }} .
    - name: Push image
      run: |
        gcloud auth configure-docker us-docker.pkg.dev --quiet
        docker push us-docker.pkg.dev/${{ secrets.GCP_PROJECT }}/simplidfs/simplidfs-metaserver:${{ env.GIT_SHA }}
    - name: Get image digest
      id: digest
      run: |
        DIGEST=$(gcloud artifacts docker images describe us-docker.pkg.dev/${{ secrets.GCP_PROJECT }}/simplidfs/simplidfs-metaserver:${{ env.GIT_SHA }} --format='value(image_summary.digest)')
        echo "digest=us-docker.pkg.dev/${{ secrets.GCP_PROJECT }}/simplidfs/simplidfs-metaserver@${DIGEST}" >> "$GITHUB_OUTPUT"
    - name: Restart Metaserver service
      run: |
        TOKEN=$(gcloud auth print-access-token)
        gcloud compute ssh ${{ secrets.GCE_INSTANCE }} --zone ${{ secrets.GCE_ZONE }} --command="echo $TOKEN | sudo docker login -u oauth2accesstoken --password-stdin https://us-docker.pkg.dev && sudo docker pull ${{ steps.digest.outputs.digest }} && sudo systemctl restart simplidfs-metaserver"
        # Example digest pull
        # docker pull us-docker.pkg.dev/galvanic-ripsaw-439813-f2/simplidfs/simplidfs-metaserver@sha256:d1d57720f635303c677d97a8ad9e986c2bed022e23069a4ca3904a9d87783e4c
```
This requires secrets for the service account key and instance details (`GCP_SA_KEY`, `GCP_PROJECT`, `GCE_INSTANCE`, and `GCE_ZONE`).
