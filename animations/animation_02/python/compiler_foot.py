import sys
import json
import subprocess
from pathlib import Path
import pandas as pd
from pydub import AudioSegment

# 1. Lecture du contrat job.json passé en argument par le moteur
if len(sys.argv) < 2:
    print("      [ Python - ERREUR ] Chemin du fichier job.json manquant.")
    sys.exit(1)

job_file_path = Path(sys.argv[1])
with open(job_file_path, 'r', encoding='utf-8') as f:
    job_data = json.load(f)

# 2. Définition des chemins dynamiques
work_dir = Path(job_data["work_dir"])
output_video = Path(job_data["output_video"])

# Dossier assets propre à animation_01 (situé à ../assets par rapport à ce script)
anim_assets_dir = Path(__file__).resolve().parent.parent / "assets"

fps = 60
dossier_frames = work_dir / "frames"
fichier_csv = work_dir / "audio_log.csv"
son_bounce_path = anim_assets_dir / "bounce.mp3"
son_goal_path = anim_assets_dir / "goal.mp3"
chemin_audio_temp = work_dir / "audio_temp.wav"

def generer_video():
    print("      [ Python ] 1. Chargement des pistes audio...")
    if not son_bounce_path.exists() or not son_goal_path.exists():
        print(f"      [ Python - ERREUR ] Sons introuvables dans {anim_assets_dir}")
        sys.exit(1)

    bounce = AudioSegment.from_mp3(str(son_bounce_path))
    goal = AudioSegment.from_mp3(str(son_goal_path))

    print("      [ Python ] 2. Lecture du log de collision...")
    if not fichier_csv.exists():
        print(f"      [ Python - ERREUR ] Le fichier {fichier_csv} est introuvable.")
        sys.exit(1)

    log = pd.read_csv(fichier_csv)

    derniere_frame = log['frame'].max() if not log.empty else 0
    duree_totale_ms = int((derniere_frame / fps) * 1000) + 1000

    piste_finale = AudioSegment.silent(duration=duree_totale_ms)

    print("      [ Python ] 3. Mixage de l'audio en cours...")
    for _, row in log.iterrows():
        frame = row['frame']
        type_son = row['sound']
        position_ms = int((frame / fps) * 1000)

        if type_son == "bounce":
            piste_finale = piste_finale.overlay(bounce, position=position_ms)
        elif type_son == "goal":
            piste_finale = piste_finale.overlay(goal, position=position_ms)

    piste_finale.export(str(chemin_audio_temp), format="wav")
    print(f"      [ Python ] Audio temporaire généré : {chemin_audio_temp.name}")

    print("      [ Python ] 4. Assemblage final avec FFmpeg...")
    commande_ffmpeg = [
        "ffmpeg",
        "-y",
        "-framerate", str(fps),
        "-i", str(dossier_frames / "frame-%04d.png"),
        "-i", str(chemin_audio_temp),
        "-c:v", "libx264",
        "-pix_fmt", "yuv420p",
        "-c:a", "aac",
        "-b:a", "192k",
        "-shortest",
        str(output_video)
    ]

    # Exécution silencieuse de FFmpeg (affiche uniquement en cas d'erreur)
    res = subprocess.run(commande_ffmpeg, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"      [ Python - ERREUR FFmpeg ] :\n{res.stderr}")
        sys.exit(1)

    if chemin_audio_temp.exists():
        chemin_audio_temp.unlink()

    print(f"      [ Python ] VIDÉO TERMINÉE ET PRÊTE : {output_video.name}")

if __name__ == "__main__":
    generer_video()