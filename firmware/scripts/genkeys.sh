#!/usr/bin/env bash
# Generates the RSA key pair used to sign firmware images for Wi-Fi updates.
#   keys/private.key  signs images at build time; keep it secret and backed up (it is gitignored)
#   keys/public.key   is compiled into the firmware; the device only accepts images signed by the matching private key
#
# A device only switches to a new key after firmware built with it is flashed over serial
# (or uploaded while signed with the old key).
#
# Usage: scripts/genkeys.sh [--force]
set -euo pipefail

cd "$(dirname "$0")/.."
keys=keys
force=${1:-}

if ! command -v openssl >/dev/null; then
  echo "openssl is required" >&2
  exit 1
fi

if [[ -e $keys/private.key && $force != --force ]]; then
  echo "$keys/private.key already exists. Devices running firmware built with it will reject images" >&2
  echo "signed by a new key. Re-run with --force to replace it." >&2
  exit 1
fi

mkdir -p "$keys"
(umask 077 && openssl genrsa -out "$keys/private.key" 2048 2>/dev/null)
openssl rsa -in "$keys/private.key" -outform PEM -pubout -out "$keys/public.key" 2>/dev/null

echo "Created $keys/private.key (keep secret, back it up) and $keys/public.key."
