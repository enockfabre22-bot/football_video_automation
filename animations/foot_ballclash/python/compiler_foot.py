import sys
import subprocess
import os
import glob
import json  # <--- NOUVEAU
import pandas as pd
from pydub import AudioSegment

# ==========================================
# FIX EMOJIS POUR LA CONSOLE WINDOWS
# ==========================================
if sys.stdout.encoding.lower() != 'utf-8':
    sys.stdout.reconfigure(encoding='utf-8')
# ==========================================
# 1. DOSSIERS STATIQUES (Ressources de l'animation)
# ==========================================
# On trouve où est ce script (dossier python/)
script_dir = os.path.dirname(os.path.abspath(__file__))
# On pointe vers le dossier assets/ (qui est à côté de python/)
assets_dir = os.path.join(script_dir, "..", "assets")

# Dictionnaire des sons attendus (chemins absolus vers assets/)
fichiers_sons = {
    "bounce": os.path.join(assets_dir, "bounce.mp3"),
    "hit": os.path.join(assets_dir, "hit.mp3"),
    "saw": os.path.join(assets_dir, "saw.mp3"),
    "heart": os.path.join(assets_dir, "heart.mp3"),
    "death": os.path.join(assets_dir, "death.mp3")
}

# ==========================================
# 2. DOSSIER DYNAMIQUE (Le job temporaire fourni par le Cerveau)
# ==========================================
# ==========================================
# 2. DOSSIER DYNAMIQUE (Le job temporaire fourni par le Cerveau)
# ==========================================
# ==========================================
# 2. DOSSIER DYNAMIQUE (Le job temporaire fourni par le Cerveau)
# ==========================================
output_video = None

if len(sys.argv) > 1:
    chemin_recu = sys.argv[1]
    if chemin_recu.endswith(".json"):
        job_dir = os.path.dirname(chemin_recu)
        
        # On lit le job.json pour connaître l'adresse de destination finale
        with open(chemin_recu, 'r', encoding='utf-8') as f:
            job_data = json.load(f)
            output_video = job_data.get("output_video")
    else:
        job_dir = chemin_recu
else:
    job_dir = os.getcwd()  # Fallback si on lance le script à la main

fps = 60
dossier_frames = os.path.join(job_dir, "frames")
fichier_csv = os.path.join(job_dir, "audio_log.csv")

# Si on n'a pas trouvé la destination dans le JSON, on la sauve dans le dossier temporaire
if not output_video:
    nom_job = os.path.basename(os.path.normpath(job_dir))
    output_video = os.path.join(job_dir, f"{nom_job}.mp4")
# ==========================================
# 3. GÉNÉRATION DE LA VIDÉO
# ==========================================
def generer_video():
    print("[AUDIO] 1. Chargement des pistes audio...")
    sons_charges = {}
    for nom_son, chemin in fichiers_sons.items():
        if os.path.exists(chemin):
            sons_charges[nom_son] = AudioSegment.from_mp3(chemin)
            print(f"   [+] {nom_son} charge.")
        else:
            print(f"   [!] Attention : Fichier '{chemin}' introuvable. Ce son sera ignore.")

    print("[LOG] 2. Lecture du log de collision et des frames...")
    if not os.path.exists(fichier_csv):
        print(f"[ERREUR] Le fichier {fichier_csv} est introuvable.")
        return

    images = glob.glob(os.path.join(dossier_frames, "*.png"))
    total_frames = len(images)
    if total_frames == 0:
        print(f"[ERREUR] Aucune image trouvee dans '{dossier_frames}'.")
        return

    duree_totale_ms = int((total_frames / fps) * 1000)
    print(f"   -> {total_frames} frames detectees (Duree : {duree_totale_ms/1000:.2f}s)")

    piste_finale = AudioSegment.silent(duration=duree_totale_ms)
    log = pd.read_csv(fichier_csv)

    print("[MIXAGE] 3. Mixage de l'audio en cours...")
    for index, row in log.iterrows():
        frame = row['frame']
        type_son = row['sound']
        position_ms = int((frame / fps) * 1000)

        if type_son in sons_charges:
            piste_finale = piste_finale.overlay(sons_charges[type_son], position=position_ms)

    chemin_audio_temp = os.path.join(job_dir, "audio_temp.wav")
    piste_finale.export(chemin_audio_temp, format="wav")
    print(f"[OK] Audio mixe et genere : {chemin_audio_temp}")

    print("[FFMPEG] 4. Assemblage final HD avec FFmpeg...")
    commande_ffmpeg = [
        "ffmpeg",
        "-y",
        "-framerate", str(fps),
        "-i", os.path.join(dossier_frames, "frame-%04d.png"),
        "-i", chemin_audio_temp,
        "-c:v", "libx264",
        "-preset", "slow",
        "-crf", "18",
        "-pix_fmt", "yuv420p",
        "-c:a", "aac",
        "-b:a", "320k",
        "-shortest",
        output_video
    ]

    subprocess.run(commande_ffmpeg)
    
    if os.path.exists(chemin_audio_temp):
        os.remove(chemin_audio_temp)
        
    print(f"[SUCCES] VIDEO TERMINEE ET PRETE : {output_video}")

if __name__ == "__main__":
    try:
        generer_video()
    except Exception as e:
        print(f"\n❌ Une erreur est survenue : {e}")