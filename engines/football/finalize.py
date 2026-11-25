"""
finalize.py — Dernière étape du moteur foot.

Usage : python engines/football/finalize.py <job.json> [--bed-gain-db -9]

- mixe une ambiance de stade (assets/sounds/football/beds/) sous les effets sonores de la scène, avec des
  montées de foule aux moments forts (événements de la scène), puis normalise à -14 LUFS (réseaux sociaux) ;
- extrait la miniature (instant choisi par la scène : metrics.json -> scene.thumb_time)
  -> <work_dir>/thumbnail_source.jpg (reprise automatiquement à la publication).
"""
import argparse
import json
import random
import subprocess
import sys
from pathlib import Path

import numpy as np

BASE_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
BEDS_DIR = BASE_DIR / "assets" / "sounds" / "football" / "beds"
RATE = 48000
SWELL_SOUNDS = {"crowd_roar": 1.0, "crowd_goal": 0.9, "crowd_cheer": 0.8, "explode_big": 0.7,
                "crowd_cheer_small": 0.45, "crowd_ooh": 0.35, "glass": 0.35, "explode_1": 0.3, "explode_2": 0.3}


def log(msg):
    print(f"      [FOOT] {msg}", flush=True)


def decode(path, start=0.0, dur=None):
    cmd = ["ffmpeg", "-v", "error", "-ss", f"{start:.3f}"] + (["-t", f"{dur:.3f}"] if dur else []) + \
          ["-i", str(path), "-vn", "-ac", "2", "-ar", str(RATE), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, capture_output=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).copy() if raw else np.zeros((0, 2), np.float32)


def probe_duration(path):
    res = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(path)],
                         capture_output=True, text=True)
    try:
        return float(res.stdout.strip())
    except ValueError:
        return 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("job")
    ap.add_argument("--bed-gain-db", type=float, default=-8.0)
    ap.add_argument("--no-bed", action="store_true", help="pas d'ambiance de stade (quiz)")
    ap.add_argument("--music-dir", help="musique de fond : dossier de pistes + credits.json (quiz)")
    ap.add_argument("--music-gain-db", type=float, default=-15.0)
    ap.add_argument("--quiz-thumbnail", action="store_true", help="miniature YouTube personnalisée du quiz")
    a = ap.parse_args()
    with open(a.job, "r", encoding="utf-8") as f:
        job = json.load(f)
    work = Path(job["work_dir"])
    video = Path(job["output_video"])
    fb = work / "football"
    if not video.exists():
        log(f"ERREUR : vidéo absente {video}")
        return 1
    meta = json.loads((fb / "meta.json").read_text(encoding="utf-8")) if (fb / "meta.json").exists() else {}
    metrics_path = work / "sandbox_out" / "metrics.json"
    metrics = json.loads(metrics_path.read_text(encoding="utf-8")) if metrics_path.exists() else {}
    events = []
    ev_path = work / "sandbox_out" / "events.jsonl"
    if ev_path.exists():
        for line in ev_path.read_text(encoding="utf-8").splitlines():
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                pass

    dur = probe_duration(video)
    n = int(dur * RATE)
    fx = decode(video)
    if len(fx) < n:
        fx = np.vstack([fx, np.zeros((n - len(fx), 2), np.float32)])
    fx = fx[:n]

    # --- ambiance de stade : extrait aléatoire, fondus, montées aux moments forts ---
    bed_file = BEDS_DIR / meta.get("bed", "") if meta.get("bed") else None
    if a.no_bed:
        bed_file = None
    elif not bed_file or not bed_file.exists():
        beds = sorted(BEDS_DIR.glob("*.ogg"))
        bed_file = random.choice(beds) if beds else None
    mix = fx.copy()
    if bed_file:
        total = probe_duration(bed_file)
        start = random.uniform(0, max(0.0, total - dur - 1))
        bed = decode(bed_file, start, dur + 0.5)[:n]
        if len(bed) < n:
            reps = int(np.ceil(n / max(1, len(bed))))
            bed = np.tile(bed, (reps, 1))[:n]
        t = np.arange(n) / RATE
        env = np.full(n, 10 ** (a.bed_gain_db / 20), np.float32)
        for e in events:
            k = SWELL_SOUNDS.get(e.get("sound", ""))
            if k:
                bump = k * np.exp(-np.maximum(0, t - e["t"]) / 1.6) * (t >= e["t"] - 0.15)
                env *= (1 + 1.2 * bump).astype(np.float32)
        env *= np.clip(t / 0.6, 0, 1) * np.clip((dur - t) / 1.2, 0, 1)
        mix = fx + bed * env[:, None]

    music_credits = []
    if a.music_dir:
        music, music_credits = music_bed(BASE_DIR / a.music_dir, dur, n, seed=job.get("job_id", ""))
        if music is not None:
            mix = mix + music * (10 ** (a.music_gain_db / 20))
            add_music_credits(work, music_credits)

    wav = work / "football" / "final_mix.f32"
    mix.astype(np.float32).tofile(wav)
    tmp = video.with_name(video.stem + "_mix.mp4")
    cmd = ["ffmpeg", "-y", "-v", "error", "-i", str(video),
           "-f", "f32le", "-ar", str(RATE), "-ac", "2", "-i", str(wav),
           "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy",
           "-af", "loudnorm=I=-14:TP=-1.5:LRA=11", "-ar", str(RATE),
           "-c:a", "aac", "-b:a", "256k", "-shortest", "-movflags", "+faststart", str(tmp)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    wav.unlink(missing_ok=True)
    if res.returncode != 0 or not tmp.exists() or tmp.stat().st_size == 0:
        log(f"ERREUR ffmpeg (mixage final) : {res.stderr.strip()[-500:]}")
        return 1
    tmp.replace(video)
    log(f"Son final : ambiance {bed_file.name if bed_file else 'aucune'}, musique "
        f"{', '.join(c.split(chr(34))[1] for c in music_credits if chr(34) in c) or 'aucune'}, {len(events)} effets, -14 LUFS")

    # --- miniature ---
    thumb_t = (metrics.get("scene") or {}).get("thumb_time") or max(1.0, dur * 0.4)
    res = subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", f"{thumb_t:.2f}", "-i", str(video),
                          "-frames:v", "1", "-q:v", "2", str(work / "thumbnail_source.jpg")],
                         capture_output=True, text=True)
    if res.returncode == 0:
        log(f"Miniature extraite à {thumb_t:.1f} s")
    if a.quiz_thumbnail:
        try:
            import quiz_thumbnail
            quiz_thumbnail.make(work, work / "thumbnail_source.jpg")
            log("Miniature du quiz générée")
        except Exception as e:
            log(f"Miniature du quiz impossible ({e}) : image de la vidéo conservée")
    return 0


def music_bed(folder, dur, n, seed=""):
    """Enchaîne des pistes (ordre aléatoire, fondus enchaînés de 3 s) jusqu'à couvrir la vidéo."""
    tracks = sorted(p for p in folder.glob("*") if p.suffix.lower() in (".mp3", ".ogg", ".wav", ".m4a"))
    if not tracks:
        return None, []
    credits_file = folder / "credits.json"
    credits = json.loads(credits_file.read_text(encoding="utf-8")).get("tracks", {}) if credits_file.exists() else {}
    rng = random.Random(seed)
    rng.shuffle(tracks)
    xf = int(3.0 * RATE)
    out = np.zeros((0, 2), np.float32)
    used = []
    i = 0
    while len(out) < n + RATE and i < 50:
        tr = tracks[i % len(tracks)]
        i += 1
        x = decode(tr)
        if len(x) < xf * 2:
            continue
        # volume homogène entre pistes (RMS)
        rms = float(np.sqrt(np.mean(x ** 2))) or 1.0
        x = x * (0.12 / rms)
        if len(out) == 0:
            out = x
        else:
            fade = np.linspace(0, 1, xf, dtype=np.float32)[:, None]
            out[-xf:] = out[-xf:] * (1 - fade) + x[:xf] * fade
            out = np.vstack([out, x[xf:]])
        if credits.get(tr.name) and credits[tr.name] not in used:
            used.append(credits[tr.name])
    out = out[:n]
    if len(out) < n:
        out = np.vstack([out, np.zeros((n - len(out), 2), np.float32)])
    t = np.arange(n) / RATE
    out *= (np.clip(t / 1.5, 0, 1) * np.clip((dur - t) / 3.0, 0, 1))[:, None]
    return out.astype(np.float32), used


def add_music_credits(work, credits):
    """Crédit des musiques (licence CC BY) ajouté à la description préparée (metadata_override.json)."""
    path = work / "metadata_override.json"
    if not credits or not path.exists():
        return
    meta = json.loads(path.read_text(encoding="utf-8"))
    meta["description"] = meta.get("description", "") + "\n\nMusic:\n" + "\n".join(credits)
    path.write_text(json.dumps(meta, indent=1, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
