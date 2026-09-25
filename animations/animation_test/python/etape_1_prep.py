import sys
import json
from pathlib import Path

# Le moteur passe le chemin du fichier job.json en premier argument
job_file_path = Path(sys.argv[1])

with open(job_file_path, 'r', encoding='utf-8') as f:
    job_data = json.load(f)

match = job_data["match"]
work_dir = Path(job_data["work_dir"])

print(f"      [ Script 1 ] Préparation du match : {match['home']} VS {match['away']}")

# Création d'un fichier intermédiaire dans le dossier de travail temporaire
temp_file = work_dir / "intermediaire.txt"
with open(temp_file, 'w', encoding='utf-8') as f:
    f.write(f"Données préparées pour {match['home']} vs {match['away']}\n")
    f.write(f"Logo 1 : {job_data['resources']['home_logo']}\n")
    f.write(f"Logo 2 : {job_data['resources']['away_logo']}\n")

print(f"      [ Script 1 ] Fichier intermédiaire créé avec succès.")