"""
local_test.py — Teste le moteur piano en local, SANS Docker ni publication
(Windows avec w64devkit/raylib, ou Linux avec raylib installé).

Usage (depuis la racine du projet) :
    python engines/piano/local_test.py [--midi "assets/midi2/Adele - Skyfall.mid"]
        [--format portrait|landscape] [--max-seconds 58] [--excerpt auto|full] [--names artist_title]

Résultat : output/tests/piano_<horodatage>/ (vidéo finale, miniature, fichiers intermédiaires).
"""
import argparse
import json
import os
import random
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[2]
ENGINE = Path(__file__).resolve().parent
RUNTIME = BASE_DIR / "sandbox" / "profiles" / "raylib_box2d" / "runtime"
RAYLIB_SRC = Path(os.environ.get("RAYLIB_DIR", "C:/raylib/raylib/src"))


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd)[:220])
    res = subprocess.run([str(c) for c in cmd], **kw)
    if res.returncode != 0:
        sys.exit(f"Échec (code {res.returncode})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--midi")
    ap.add_argument("--bg")
    ap.add_argument("--format", default="portrait")
    ap.add_argument("--max-seconds", default="58")
    ap.add_argument("--excerpt", default="auto")
    ap.add_argument("--names", default=None)
    a = ap.parse_args()

    portrait = a.format == "portrait"
    midi = Path(a.midi) if a.midi else random.choice(list((BASE_DIR / "assets" / ("midi2" if portrait else "midi")).glob("*.mid")))
    bg_dir = BASE_DIR / "assets" / ("jpg_portrait" if portrait else "jpg")
    bg = Path(a.bg) if a.bg else random.choice(list(bg_dir.glob("*.jpg")))
    names = a.names or ("artist_title" if portrait else "title_artist")

    job_id = f"piano_{datetime.now():%Y%m%d_%H%M%S}"
    work = BASE_DIR / "output" / "tests" / job_id
    work.mkdir(parents=True)
    job = {
        "job_id": job_id,
        "metadata": {"song_title": midi.stem},
        "resources": {"midi_file": str(midi.resolve()), "bg_image": str(bg.resolve())},
        "work_dir": str(work),
        "output_video": str(work / "video.mp4"),
    }
    job_file = work / "job.json"
    job_file.write_text(json.dumps(job, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"MIDI : {midi.name}\nFond : {bg.name}\nDossier : {work}")

    py = sys.executable
    run([py, ENGINE / "prepare.py", job_file, "--format", a.format, "--max-seconds", a.max_seconds,
         "--excerpt", a.excerpt, "--names", names])

    exe = work / ("anim.exe" if os.name == "nt" else "anim")
    libs = ["-lraylib", "-lopengl32", "-lgdi32", "-lwinmm"] if os.name == "nt" else \
           ["-lraylib", "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11"]
    run(["g++", "-O2", "-std=c++17", f"-I{RUNTIME}", f"-I{RAYLIB_SRC}", RUNTIME / "anim_runtime.cpp",
         ENGINE / "scene" / "piano_scene.cpp", "-o", exe, f"-L{RAYLIB_SRC}"] + libs)

    video = json.loads((work / "piano" / "video.json").read_text(encoding="utf-8"))
    out_dir = work / "sandbox_out"
    out_dir.mkdir()
    cfg = {
        "mode": "final", "render_width": video["width"], "render_height": video["height"],
        "logical_width": video["width"], "logical_height": video["height"], "fps": video["fps"],
        "duration": video["duration_s"], "seed": 42, "crf": 18,
        "out_dir": out_dir.as_posix(), "sounds_dir": "",
    }
    (work / "sandbox_job.txt").write_text("".join(f"{k}={v}\n" for k, v in cfg.items()), encoding="utf-8")
    env = dict(os.environ, PIANO_DIR=(work / "piano").as_posix())
    run([exe, work / "sandbox_job.txt"], env=env)
    shutil.copy2(out_dir / "video.mp4", work / "video.mp4")

    run([py, ENGINE / "finalize.py", job_file, "--thumbnail"])
    metrics = json.loads((out_dir / "metrics.json").read_text(encoding="utf-8"))
    print(f"\n[OK] Vidéo : {work / 'video.mp4'}  (rendu {metrics['render_seconds']:.0f} s pour "
          f"{metrics['frames']} images)")


if __name__ == "__main__":
    main()
