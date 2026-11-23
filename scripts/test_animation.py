"""
Rend une animation SANS la publier, pour la tester (n'importe quelle animation, même non assignée à un compte).

Usage (depuis la racine du projet) :
    python scripts/test_animation.py <nom_animation> [fichier.mid]
    python scripts/test_animation.py fb_duel ["real madrid" barcelona]   # animations foot

Les animations qui demandent des "inputs" (midi, jpg...) reçoivent un fichier tiré au hasard,
comme en production ; un MIDI précis peut être imposé en 2e argument.

La vidéo et les fichiers de contrôle (metrics.json, events.jsonl) sont copiés dans output/tests/.
"""
import json
import random
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

    job_id = f"test_{name}_{datetime.now():%Y%m%d_%H%M%S}"
    date_str = (datetime.now() + timedelta(days=1)).strftime("%Y-%m-%d")
    match_data = {"date": date_str, "home": "test", "away": "no_match", "resources": {}}
    metadata = {"title": f"Test {name}"}

    # Ressources tirées au hasard comme le fait main.py (MIDI, fond) ; MIDI imposable en 2e argument
    inputs = cfg.get("inputs", [])
    if "library" in inputs:
        import midi_library
        if len(sys.argv) > 2:
            midi = Path(sys.argv[2])
        else:   # même tirage qu'en production (catégorie + notoriété), sans l'enregistrer dans l'historique
            midi = Path(midi_library.choose_song("test", cfg.get("library", {}))["path"])
        match_data["resources"]["midi_file"] = str(midi.resolve())
        metadata["song_title"] = midi.stem
        print(f"MIDI : {midi.name}")
    # Animations foot : composition d'équipes tirée comme en production (sans historique),
    # ou un match d'exemple pour le duel (2e et 3e arguments : équipes, ex. "real madrid" barcelona)
    if "football_pack" in inputs:
        import football_data
        lineup = football_data.choose_lineup("test", cfg)
        match_data["resources"]["football_lineup"] = lineup
        print(f"Équipes : {lineup['competition']} ({lineup['count']})")
    if cfg.get("requires_match_data"):
        import football_data
        h = football_data.team_info(sys.argv[2] if len(sys.argv) > 2 else "paris saint germain")
        a = football_data.team_info(sys.argv[3] if len(sys.argv) > 3 else "marseille")
        match_data.update({"home": h["name"], "away": a["name"]})
        match_data["resources"] = {"home_logo": h["logo"], "away_logo": a["logo"], "home_key": h["key"],
                                   "away_key": a["key"], "competition": "Ligue 1"}
        print(f"Match : {h['name']} - {a['name']}")
    jpg_key = "jpg_portrait" if "jpg_portrait" in inputs else "jpg" if "jpg" in inputs else None
    if jpg_key:
        jpg = random.choice(list((BASE_DIR / "assets" / jpg_key).glob("*.jpg")))
        match_data["resources"]["bg_image"] = str(jpg)
        print(f"Fond : {jpg.name}")

    video = AnimationManager().execute_animation(match_data, cfg, job_id, job_metadata=metadata)
    if not video:
        print(f"\n❌ Échec du rendu de '{name}'. Voir les erreurs ci-dessus.")
        sys.exit(2)

    tests_dir = BASE_DIR / "output" / "tests"
    tests_dir.mkdir(parents=True, exist_ok=True)
    final = tests_dir / f"{job_id}.mp4"
    shutil.move(str(video), final)

    work_dir = BASE_DIR / "output" / "temporary" / job_id
    sandbox_out = work_dir / "sandbox_out"
    for extra in ("metrics.json", "events.jsonl"):
        if (sandbox_out / extra).exists():
            shutil.copy2(sandbox_out / extra, tests_dir / f"{job_id}_{extra}")
    if (work_dir / "thumbnail_source.jpg").exists():
        shutil.copy2(work_dir / "thumbnail_source.jpg", tests_dir / f"{job_id}_thumbnail.jpg")
    shutil.rmtree(BASE_DIR / "output" / "temporary" / job_id, ignore_errors=True)

    print(f"\n✅ Vidéo de test : {final}")
    metrics = tests_dir / f"{job_id}_metrics.json"
    if metrics.exists():
        print(metrics.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
