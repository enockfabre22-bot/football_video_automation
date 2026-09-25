import sys
import json
from pathlib import Path

job_file_path = Path(sys.argv[1])

with open(job_file_path, 'r', encoding='utf-8') as f:
    job_data = json.load(f)

work_dir = Path(job_data["work_dir"])
output_video = Path(job_data["output_video"])

# Lecture du fichier généré par l'étape 1
temp_file = work_dir / "intermediaire.txt"
if not temp_file.exists():
    print("      [ Script 2 - ERREUR ] Le fichier intermédiaire de l'étape 1 est introuvable !")
    sys.exit(1)

print("      [ Script 2 ] Lecture des données intermédiaires OK. Lancement du rendu...")

# Simulation de la création de la vidéo finale (.mp4)
with open(output_video, 'w', encoding='utf-8') as f:
    f.write("CONTENU_VIDEO_FACTICE_MP4_POUR_TEST")

print(f"      [ Script 2 ] Vidéo finale générée : {output_video.name}")