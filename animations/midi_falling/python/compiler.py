import sys
import subprocess
import os
import glob
import json

# ==========================================
# 1. DOSSIER DYNAMIQUE (Lecture du job.json)
# ==========================================
output_video = None
midi_file = None

if len(sys.argv) > 1:
    chemin_recu = sys.argv[1]
    if chemin_recu.endswith(".json"):
        job_dir = os.path.dirname(chemin_recu)
        
        # Lecture des instructions du Cerveau
        with open(chemin_recu, 'r', encoding='utf-8') as f:
            job_data = json.load(f)
            output_video = job_data.get("output_video")
            
            # Recherche du fichier MIDI sélectionné pour ce job
            if "resources" in job_data:
                midi_file = job_data["resources"].get("midi_file")
            else:
                midi_file = job_data.get("inputs", {}).get("midi") # Fallback
    else:
        job_dir = chemin_recu
else:
    job_dir = os.getcwd()

dossier_frames = os.path.join(job_dir, "frames")

if not output_video:
    nom_job = os.path.basename(os.path.normpath(job_dir))
    output_video = os.path.join(job_dir, f"{nom_job}.mp4")

# ==========================================================
# 2. RÉCUPÉRATION DU FICHIER AUDIO (Généré par AudioPlayerTXT)
# ==========================================================
audio_input = os.path.join(job_dir, "audio.wav")

if not os.path.exists(audio_input):
    print(f"[ERREUR] Le fichier audio est introuvable : {audio_input}")
    print("-> L'étape AudioPlayerTXT a-t-elle échoué ?")
    exit(1)

print(f"[AUDIO] Fichier prêt pour le mixage : {audio_input}")

# ==========================================================
# 3. DÉTECTION DU MODE (TEST RAPIDE ou ULTRA HD)
# ==========================================================
images_jpg = glob.glob(os.path.join(dossier_frames, "frame-*.jpg"))
images_png = glob.glob(os.path.join(dossier_frames, "frame-*.png"))

if len(images_jpg) > 0:
    print("[DÉTECTION] Mode TEST RAPIDE détecté (.jpg)")
    fps = 30
    extension = "jpg"
elif len(images_png) > 0:
    print("[DÉTECTION] Mode PRODUCTION ULTRA HD détecté (.png)")
    fps = 60
    extension = "png"
else:
    print(f"[ERREUR] Aucune image trouvée dans {dossier_frames}. Processing a-t-il planté ?")
    exit(1)

# ==========================================================
# 4. ASSEMBLAGE DIRECT AVEC FFMPEG
# ==========================================================
print(f"[FFMPEG] Assemblage en une passe (Images {extension} + Audio) à {fps} FPS...")

# Format attendu par FFmpeg pour les séquences d'images (ex: frame-%06d.png)
input_pattern = os.path.join(dossier_frames, f"frame-%06d.{extension}")

commande_ffmpeg = [
    "ffmpeg",
    "-y",  # Écraser si existant
    "-framerate", str(fps),
    "-i", input_pattern,
    "-i", audio_input,
    "-c:v", "libx264",
    "-preset", "slow",     # Qualité de compression maximale
    "-crf", "18",          # Très haute qualité visuelle
    "-pix_fmt", "yuv420p", # Compatibilité universelle (YouTube/TikTok)
    "-c:a", "aac",
    "-b:a", "320k",        # Qualité audio maximale pour YouTube
    "-shortest",           # Coupe la vidéo quand l'audio (ou la vidéo) le plus court se termine
    output_video
]

# Exécution de la commande
result = subprocess.run(commande_ffmpeg)

if result.returncode == 0:
    print(f"\n[SUCCES] Video YouTube generee : {output_video}")
else:
    print("\n[ERREUR] Un probleme est survenu avec FFmpeg.")