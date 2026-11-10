"""
Étape d'animation : ajoute une musique de fond à la vidéo du job (output_video), en place.

Usage (dans les "steps" d'une animation) :
  {"type": "python", "name": "Musique de fond", "script": "scripts/steps/add_background_music.py",
   "args": ["--music-dir", "assets/music/satisfying4k", "--volume", "0.9"]}

- pistes tirées au hasard dans le dossier, enchaînées en fondu enchaîné jusqu'à couvrir la vidéo ;
- fondu d'entrée / de sortie, piste audio AAC ; la vidéo n'est pas réencodée ;
- remplace un éventuel son existant ; écrit <work_dir>/music.json (crédits des pistes utilisées).
"""
import argparse
import json
import random
import subprocess
import sys
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[2]
AUDIO_EXTS = {".mp3", ".wav", ".ogg", ".m4a", ".flac"}
CROSSFADE = 3.0


def duration_of(path):
    res = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                          "-of", "default=nw=1:nk=1", str(path)], capture_output=True, text=True)
    try:
        return float(res.stdout.strip())
    except ValueError:
        return 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("job_file")
    parser.add_argument("--music-dir", required=True)
    parser.add_argument("--volume", type=float, default=0.9)
    parser.add_argument("--fade-in", type=float, default=2.0)
    parser.add_argument("--fade-out", type=float, default=4.0)
    args = parser.parse_args()

    with open(args.job_file, "r", encoding="utf-8") as f:
        job = json.load(f)
    video = Path(job["output_video"])
    work_dir = Path(job["work_dir"])

    music_dir = Path(args.music_dir)
    if not music_dir.is_absolute():
        music_dir = BASE_DIR / music_dir
    tracks = sorted(p for p in music_dir.glob("*") if p.suffix.lower() in AUDIO_EXTS)
    if not tracks:
        print(f"[MUSIQUE] Aucune musique dans {music_dir}")
        return 1
    if not video.exists():
        print(f"[MUSIQUE] Vidéo introuvable : {video}")
        return 1

    video_d = duration_of(video)
    if video_d <= 0:
        print("[MUSIQUE] Durée de la vidéo illisible")
        return 1

    # Pistes au hasard (sans répétition tant que possible) jusqu'à couvrir la durée
    rng = random.Random(job.get("job_id"))
    chosen, total, pool = [], 0.0, []
    while total < video_d + 1 and len(chosen) < 50:
        if not pool:
            pool = tracks[:]
            rng.shuffle(pool)
        t = pool.pop()
        d = duration_of(t)
        if d <= CROSSFADE * 2:
            continue
        chosen.append(t)
        total += d - (CROSSFADE if len(chosen) > 1 else 0)
    if not chosen:
        print("[MUSIQUE] Aucune piste exploitable")
        return 1

    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", str(video)]
    for t in chosen:
        cmd += ["-i", str(t)]
    filters = [f"[{i + 1}:a]aformat=sample_rates=44100:channel_layouts=stereo[a{i}]" for i in range(len(chosen))]
    last = "a0"
    for i in range(1, len(chosen)):
        filters.append(f"[{last}][a{i}]acrossfade=d={CROSSFADE}[x{i}]")
        last = f"x{i}"
    fade_out_start = max(0.0, video_d - args.fade_out)
    filters.append(f"[{last}]atrim=0:{video_d:.3f},afade=t=in:st=0:d={args.fade_in},"
                   f"afade=t=out:st={fade_out_start:.3f}:d={args.fade_out},volume={args.volume}[music]")

    tmp = video.with_name(video.stem + ".music.mp4")
    cmd += ["-filter_complex", ";".join(filters), "-map", "0:v:0", "-map", "[music]",
            "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-t", f"{video_d:.3f}",
            "-movflags", "+faststart", str(tmp)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0 or not tmp.exists():
        print(f"[MUSIQUE] ffmpeg a échoué :\n{res.stderr[-2000:]}")
        return 1
    tmp.replace(video)

    names = [t.stem for t in chosen]
    work_dir.mkdir(parents=True, exist_ok=True)
    with open(work_dir / "music.json", "w", encoding="utf-8") as f:
        json.dump({"tracks": names}, f, ensure_ascii=False, indent=2)
    print(f"[MUSIQUE] Ajoutée : {', '.join(names)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
