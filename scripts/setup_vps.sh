#!/bin/bash
set -e

echo "=== 1. Mise à jour du système et installation des paquets requis ==="
sudo apt update && sudo apt upgrade -y
sudo apt install -y python3 python3-pip python3-venv ffmpeg xvfb libxi6 libxrender1 libxtst6 wget tar git

echo "=== 2. Installation de Processing 4 CLI (Linux x64) ==="
if ! command -v processing-java &> /dev/null; then
    wget -q https://github.com/processing/processing4/releases/download/processing-1293-4.3/processing-4.3-linux-x64.tgz -O /tmp/processing.tgz
    sudo tar -xzf /tmp/processing.tgz -C /opt/
    sudo ln -sf /opt/processing-4.3/processing-java /usr/local/bin/processing-java
    rm /tmp/processing.tgz
    echo "✅ Processing installé avec succès dans /opt/processing-4.3"
else
    echo "✅ processing-java est déjà installé."
fi

echo "=== 3. Création de l'environnement virtuel Python ==="
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt

echo "✅ Installation du VPS terminée ! Pensez à créer votre fichier .env avant de lancer le service."