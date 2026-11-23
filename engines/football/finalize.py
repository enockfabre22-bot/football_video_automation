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
    if not bed_file or not bed_file.exists():
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
    log(f"Son final : ambiance {bed_file.name if bed_file else 'aucune'}, {len(events)} effets, -14 LUFS")

    # --- miniature ---
    thumb_t = (metrics.get("scene") or {}).get("thumb_time") or max(1.0, dur * 0.4)
    res = subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", f"{thumb_t:.2f}", "-i", str(video),
                          "-frames:v", "1", "-q:v", "2", str(work / "thumbnail_source.jpg")],
                         capture_output=True, text=True)
    if res.returncode == 0:
        log(f"Miniature extraite à {thumb_t:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
