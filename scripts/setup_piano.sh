#!/bin/bash
# Installation du moteur piano (engines/piano/) sur le VPS. À lancer depuis la racine du projet :
#   bash scripts/setup_piano.sh
# - FluidSynth (synthèse du piano à partir du MIDI)
# - SoundFont Salamander Grand Piano V3 (Yamaha C5 échantillonné, licence CC-BY 3.0, ~1,3 Go)
# - dépendances Python (mido, numpy, scipy)
# Le rendu des images utilise l'image Docker du sandbox (bash scripts/build_sandbox.sh raylib_box2d).
set -e
cd "$(dirname "$0")/.."

echo "=== 1. FluidSynth ==="
if ! command -v fluidsynth &> /dev/null; then
    sudo apt update && sudo apt install -y fluidsynth
fi
fluidsynth --version | head -1

echo "=== 2. SoundFont Salamander Grand Piano ==="
SF_DIR=assets/soundfonts
if ls "$SF_DIR"/*/*.sf2 &> /dev/null || ls "$SF_DIR"/*.sf2 &> /dev/null; then
    echo "SoundFont déjà présent :"; ls -la "$SF_DIR"/*/*.sf2 "$SF_DIR"/*.sf2 2>/dev/null || true
else
    mkdir -p "$SF_DIR"
    wget -q --show-progress -O /tmp/salamander.tar.xz \
        "https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SF2-V3+20200602.tar.xz"
    tar -xJf /tmp/salamander.tar.xz -C "$SF_DIR"
    rm /tmp/salamander.tar.xz
    ls -la "$SF_DIR"/*/*.sf2
fi

echo "=== 3. Dépendances Python ==="
if [ -d .venv ]; then
    .venv/bin/pip install -r requirements.txt
else
    pip3 install -r requirements.txt
fi

echo "=== 4. Image Docker du sandbox ==="
if docker image inspect anim-sandbox-raylib-box2d:2 &> /dev/null; then
    echo "Image présente."
else
    echo "Image absente : lance 'bash scripts/build_sandbox.sh raylib_box2d'"
fi

echo "✅ Moteur piano prêt. Test sans publication : python scripts/test_animation.py midi_falling_short"
