"""
local_test.py — Teste le moteur foot en local, SANS Docker ni publication
(Windows avec w64devkit/raylib, ou Linux avec raylib installé).

Usage (depuis la racine du projet) :
    python engines/football/local_test.py --format royale [--pack premier_league] [--count 16]
    python engines/football/local_test.py --format duel --home "paris saint germain" --away marseille
    python engines/football/local_test.py --format rings --frames 0,90,600,1200   # images seulement (rapide)
Options : --seed N (même scénario), --width 1440 --height 2560 (rendu 1440p), --frames (aperçu PNG).

Résultat : output/tests/foot_<format>_<horodatage>/ (video.mp4, thumbnail_source.jpg, fichiers intermédiaires).
"""
import argparse
import json
import os
import random
import shutil
import subprocess
import sys
from datetime import datetime, timedelta
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[2]
ENGINE = Path(__file__).resolve().parent
RUNTIME = BASE_DIR / "sandbox" / "profiles" / "raylib_box2d" / "runtime"
RAYLIB_SRC = Path(os.environ.get("RAYLIB_DIR", "C:/raylib/raylib/src"))
SOUNDS = BASE_DIR / "assets" / "sounds" / "football"
sys.path.insert(0, str(BASE_DIR / "app"))
import football_data  # noqa: E402

COUNTS = {"royale": [12, 16, 20], "rings": [8, 10], "colorwar": [4, 6], "marble": [16, 18, 20],
          "knockout": [8], "duel": [2], "quiz": [0]}
BIAS = {"colorwar": 1.2, "knockout": 0.9, "rings": 0.5}


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd)[:240])
    res = subprocess.run([str(c) for c in cmd], **kw)
    if res.returncode != 0:
        sys.exit(f"Échec (code {res.returncode})")


def build(work):
    exe = work / ("anim.exe" if os.name == "nt" else "anim")
    libs = ["-lraylib", "-lopengl32", "-lgdi32", "-lwinmm"] if os.name == "nt" else \
           ["-lraylib", "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11"]
    run(["g++", "-O2", "-std=c++17", f"-I{RUNTIME}", f"-I{RAYLIB_SRC}", RUNTIME / "anim_runtime.cpp",
         *sorted((ENGINE / "scene").glob("*.cpp")), "-o", exe, f"-L{RAYLIB_SRC}"] + libs)
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--format", default="royale", choices=list(COUNTS))
    ap.add_argument("--pack")
    ap.add_argument("--count", type=int)
    ap.add_argument("--home", default="paris saint germain")
    ap.add_argument("--away", default="marseille")
    ap.add_argument("--competition", default="Ligue 1")
    ap.add_argument("--seed", type=int)
    ap.add_argument("--width", type=int, default=1080)
    ap.add_argument("--height", type=int, default=1920)
    ap.add_argument("--frames", help="liste d'images à exporter en PNG (aperçu rapide, pas de vidéo)")
    ap.add_argument("--exe", help="binaire déjà compilé (évite la recompilation)")
    ap.add_argument("--max-rounds", type=int, help="quiz : nombre de joueurs (test rapide)")
    a = ap.parse_args()

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    job_id = f"foot_{a.format}_{stamp}"
    work = BASE_DIR / "output" / "tests" / job_id
    work.mkdir(parents=True)

    resources, match = {}, {"date": (datetime.now() + timedelta(days=1)).strftime("%Y-%m-%d"), "home": "", "away": ""}
    if a.format == "quiz":
        pass
    elif a.format == "duel":
        h, w = football_data.team_info(a.home), football_data.team_info(a.away)
        match.update({"home": h["name"], "away": w["name"]})
        resources = {"home_logo": h["logo"], "away_logo": w["logo"], "home_key": h["key"], "away_key": w["key"],
                     "competition": a.competition}
    else:
        cfg = {"name": f"test_{a.format}", "football": {
            "team_counts": [a.count] if a.count else COUNTS[a.format], "popular_bias": BIAS.get(a.format, 0.0),
            "prefer_full_pack": a.format == "marble"}}
        lineup = football_data.choose_lineup("local_test", cfg, pack_id=a.pack)
        if not lineup:
            sys.exit("Aucune composition possible")
        resources = {"football_lineup": lineup}
        print(f"Pack : {lineup['competition']} ({lineup['count']}) : {', '.join(t['name'] for t in lineup['teams'])}")

    job = {"job_id": job_id, "match": match, "metadata": {}, "resources": resources,
           "work_dir": str(work), "output_video": str(work / "video.mp4")}
    job_file = work / "job.json"
    job_file.write_text(json.dumps(job, indent=2, ensure_ascii=False), encoding="utf-8")

    py = sys.executable
    if a.format == "quiz":
        run([py, ENGINE / "quiz_prepare.py", job_file, "--no-history"]
            + (["--max-rounds", a.max_rounds, "--min-seconds", "0"] if a.max_rounds else []))
    else:
        run([py, ENGINE / "prepare.py", job_file, "--format", a.format, "--width", a.width, "--height", a.height])
    exe = Path(a.exe) if a.exe else build(work)

    video = json.loads((work / "football" / "video.json").read_text(encoding="utf-8"))
    out_dir = work / "sandbox_out"
    out_dir.mkdir()
    preview = bool(a.frames)
    cfg = {
        "mode": "preview" if preview else "final",
        "render_width": video["width"] // (2 if preview else 1), "render_height": video["height"] // (2 if preview else 1),
        "logical_width": video["width"], "logical_height": video["height"], "fps": video["fps"],
        "duration": video["duration_s"], "seed": a.seed if a.seed is not None else random.randint(1, 2 ** 31 - 1),
        "crf": video.get("crf", 17), "out_dir": out_dir.as_posix(), "sounds_dir": SOUNDS.as_posix(),
        "capture_frames": a.frames or "",
    }
    (work / "sandbox_job.txt").write_text("".join(f"{k}={v}\n" for k, v in cfg.items()), encoding="utf-8")
    env = dict(os.environ, FOOTBALL_DIR=(work / "football").as_posix())
    run([exe, work / "sandbox_job.txt"], env=env)
    metrics = json.loads((out_dir / "metrics.json").read_text(encoding="utf-8"))
    if preview:
        print(f"\n[OK] Images : {out_dir}  (seed {cfg['seed']}, durée {metrics['duration']:.1f} s)")
        return
    shutil.copy2(out_dir / "video.mp4", work / "video.mp4")
    run([py, ENGINE / "finalize.py", job_file]
        + (["--no-bed", "--music-dir", "assets/music/quiz", "--quiz-thumbnail"] if a.format == "quiz" else []))
    print(f"\n[OK] Vidéo : {work / 'video.mp4'}  ({metrics['duration']:.1f} s, rendu {metrics['render_seconds']:.0f} s, "
          f"seed {cfg['seed']}, scène {metrics.get('scene')})")


if __name__ == "__main__":
    main()
