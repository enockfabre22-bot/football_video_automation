"""
Rend une animation SANS la publier, pour la tester (n'importe quelle animation, même non assignée à un compte).

Usage (depuis la racine du projet) :
    python scripts/test_animation.py <nom_animation>

La vidéo et les fichiers de contrôle (metrics.json, events.jsonl) sont copiés dans output/tests/.
"""
import json
import shutil
import sys
from datetime import datetime, timedelta
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BASE_DIR / "app"))

from animation_manager import AnimationManager  # noqa: E402


def find_animation(name):
    for cfg_file in (BASE_DIR / "animations").glob("*/config.json"):
        with open(cfg_file, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        if cfg.get("name") == name:
            cfg["folder_path"] = cfg_file.parent
            return cfg
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    name = sys.argv[1]
    cfg = find_animation(name)
    if not cfg:
        print(f"[ERREUR] Animation '{name}' introuvable dans animations/*/config.json (champ \"name\").")
        sys.exit(1)
    if cfg.get("type") == "ai":
        print("[ERREUR] Une animation IA se teste avec le studio IA, pas avec ce script.")
        sys.exit(1)
    if cfg.get("requires_match_data") or cfg.get("inputs"):
        print("[AVERTISSEMENT] Cette animation attend des données (match, midi, images) : le test peut échouer.")

    job_id = f"test_{name}_{datetime.now():%Y%m%d_%H%M%S}"
    date_str = (datetime.now() + timedelta(days=1)).strftime("%Y-%m-%d")
    match_data = {"date": date_str, "home": "test", "away": "no_match", "resources": {}}

    video = AnimationManager().execute_animation(match_data, cfg, job_id, job_metadata={"title": f"Test {name}"})
    if not video:
        print(f"\n❌ Échec du rendu de '{name}'. Voir les erreurs ci-dessus.")
        sys.exit(2)

    tests_dir = BASE_DIR / "output" / "tests"
    tests_dir.mkdir(parents=True, exist_ok=True)
    final = tests_dir / f"{job_id}.mp4"
    shutil.move(str(video), final)

    sandbox_out = BASE_DIR / "output" / "temporary" / job_id / "sandbox_out"
    for extra in ("metrics.json", "events.jsonl"):
        if (sandbox_out / extra).exists():
            shutil.copy2(sandbox_out / extra, tests_dir / f"{job_id}_{extra}")
    shutil.rmtree(BASE_DIR / "output" / "temporary" / job_id, ignore_errors=True)

    print(f"\n✅ Vidéo de test : {final}")
    metrics = tests_dir / f"{job_id}_metrics.json"
    if metrics.exists():
        print(metrics.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
