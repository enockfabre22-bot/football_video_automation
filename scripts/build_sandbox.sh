#!/bin/bash
# Construit l'image Docker d'un profil de sandbox (par défaut : raylib_box2d).
# Usage : bash scripts/build_sandbox.sh [profil]
set -e

PROFILE="${1:-raylib_box2d}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROFILE_DIR="$ROOT/sandbox/profiles/$PROFILE"
IMAGE=$(python3 -c "import json;print(json.load(open('$PROFILE_DIR/profile.json'))['image'])")

echo "=== Construction de l'image $IMAGE (profil $PROFILE) ==="
docker build -t "$IMAGE" "$PROFILE_DIR"

echo "=== Vérification ==="
docker run --rm --network none "$IMAGE" bash -c \
  "ls /usr/local/lib/libraylib.a /usr/local/lib/libbox2d.a && ffmpeg -version | head -1 && which xvfb-run"
echo "✅ Image $IMAGE prête."
