#!/bin/bash
set -e
NODE_NAME=${1:-node}
OUT_DIR=${2:-certs}
mkdir -p "$OUT_DIR"

if [ ! -f "$OUT_DIR/ca.key" ]; then
  openssl genrsa -out "$OUT_DIR/ca.key" 4096
  openssl req -new -x509 -key "$OUT_DIR/ca.key" -subj "/CN=SimpliDFS-CA" -days 3650 -out "$OUT_DIR/ca.crt"
fi

openssl genrsa -out "$OUT_DIR/${NODE_NAME}.key" 2048
openssl req -new -key "$OUT_DIR/${NODE_NAME}.key" -subj "/CN=${NODE_NAME}" -out "$OUT_DIR/${NODE_NAME}.csr"
openssl x509 -req -in "$OUT_DIR/${NODE_NAME}.csr" -CA "$OUT_DIR/ca.crt" -CAkey "$OUT_DIR/ca.key" -CAcreateserial -out "$OUT_DIR/${NODE_NAME}.crt" -days 365

rm "$OUT_DIR/${NODE_NAME}.csr"
echo "Certificates for $NODE_NAME written to $OUT_DIR"
