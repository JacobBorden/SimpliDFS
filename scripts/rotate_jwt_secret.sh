#!/bin/sh
set -euo pipefail

SECRET_FILE="${JWT_SECRET_FILE:-/etc/simplidfs/jwt_secret}"
NODES="${JWT_NODES:-}"

new_secret=$(openssl rand -hex 32)

mkdir -p "$(dirname "$SECRET_FILE")"
printf '%s\n' "$new_secret" | sudo tee "$SECRET_FILE" >/dev/null
sudo chmod 600 "$SECRET_FILE"

for node in $NODES; do
  ssh "$node" "sudo mkdir -p \"$(dirname \"$SECRET_FILE\")\" && echo '$new_secret' | sudo tee '$SECRET_FILE' >/dev/null && sudo chmod 600 '$SECRET_FILE'"
done

echo "JWT secret rotated"

