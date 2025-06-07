#!/bin/bash
set -e

BUNDLE_DIR="bundle"
DATE=$(date +%Y%m%d)
OUTPUT="$BUNDLE_DIR/ato_${DATE}.tar.gz"

mkdir -p "$BUNDLE_DIR"
TMP_DIR=$(mktemp -d)

AUDIT_LOGS_DIR=${AUDIT_LOGS_DIR:-logs}
FIPS_RESULTS=${FIPS_RESULTS:-fips_selftest.log}
RBAC_POLICY=${RBAC_POLICY:-rbac_policy.yaml}

if [ -d "$AUDIT_LOGS_DIR" ]; then
  cp -r "$AUDIT_LOGS_DIR" "$TMP_DIR/"
elif [ -f "$AUDIT_LOGS_DIR" ]; then
  cp "$AUDIT_LOGS_DIR" "$TMP_DIR/"
else
  echo "WARN: Audit logs not found at $AUDIT_LOGS_DIR" >&2
fi

if [ -f "$FIPS_RESULTS" ]; then
  cp "$FIPS_RESULTS" "$TMP_DIR/"
else
  echo "WARN: FIPS self-test results not found at $FIPS_RESULTS" >&2
fi

if [ -f "$RBAC_POLICY" ]; then
  cp "$RBAC_POLICY" "$TMP_DIR/"
else
  echo "WARN: RBAC policy not found at $RBAC_POLICY" >&2
fi

tar -czf "$OUTPUT" -C "$TMP_DIR" .
rm -rf "$TMP_DIR"

echo "ATO bundle written to $OUTPUT"
