#GCP CI/CD Deployment

This guide outlines how to run the metaserver on Google Cloud and keep it up to date automatically.

## 1. Build Docker Images
1. Configure Docker for the Google Container Registry:
   ```sh
   gcloud auth configure-docker
   ```
2. Build the metaserver image using the provided Dockerfile:
   ```sh
   docker build -f deploy/metaserver.Dockerfile \
     -t gcr.io/<PROJECT_ID>/simplidfs-metaserver:<TAG> .
   ```
3. Build the storage node image if needed:
   ```sh
   docker build -f deploy/node.Dockerfile \
     -t gcr.io/<PROJECT_ID>/simplidfs-node:<TAG> .
   ```

## 2. Push Images to Google Container Registry
After building, push the images so your VM or cluster can pull them:
```sh
docker push gcr.io/<PROJECT_ID>/simplidfs-metaserver:<TAG>
docker push gcr.io/<PROJECT_ID>/simplidfs-node:<TAG>
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
  gcr.io/<PROJECT_ID>/simplidfs-metaserver:<TAG>

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

jobs:
  deploy:
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
    - name: Build Docker image
      run: |
        docker build -f deploy/metaserver.Dockerfile \
          -t gcr.io/${{ secrets.GCP_PROJECT }}/simplidfs-metaserver:${{ github.sha }} .
    - name: Push image
      run: |
        gcloud auth configure-docker --quiet
        docker push gcr.io/${{ secrets.GCP_PROJECT }}/simplidfs-metaserver:${{ github.sha }}
    - name: Restart Metaserver service
      run: |
        gcloud compute ssh ${{ secrets.GCE_INSTANCE }} --zone ${{ secrets.GCE_ZONE }} --command='sudo docker pull gcr.io/${{ secrets.GCP_PROJECT }}/simplidfs-metaserver:${{ github.sha }} && sudo systemctl restart simplidfs-metaserver'
```
This requires secrets for the service account key and instance details (`GCP_SA_KEY`, `GCP_PROJECT`, `GCE_INSTANCE`, and `GCE_ZONE`).
