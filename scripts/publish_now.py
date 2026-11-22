"""
publish_now.py — Rend UNE vidéo et la publie tout de suite, avec exactement le pipeline de production
(choix de l'animation selon le créneau, morceau de la bibliothèque, titre / description / tags, miniature).
Utile pour vérifier le résultat réel sur YouTube / TikTok sans attendre le planning.

Usage (depuis la racine du projet, sur le VPS) :
  python scripts/publish_now.py youtube_piano                               # 1er créneau du compte
  python scripts/publish_now.py youtube_piano --slot video_01_soir          # la vidéo longue 16:9
  python scripts/publish_now.py youtube_piano --animation neon_piano_short  # impose l'animation
  python scripts/publish_now.py youtube_piano --privacy unlisted            # non répertoriée (test discret)
  python scripts/publish_now.py youtube_piano --dry-run                     # rendu + aperçu, sans publier

Ne touche pas aux jobs planifiés du service (identifiant "manual_...") ; le morceau est enregistré dans
l'historique du compte (il ne repassera pas avant le délai anti-répétition).
"""
import argparse
import json
import random
import shutil
import sys
from datetime import datetime
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BASE_DIR / "app"))

import midi_library  # noqa: E402
from animation_manager import AnimationManager  # noqa: E402
from main import apply_song_metadata, setup_logging  # noqa: E402
from publication_manager import PublicationManager  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("account")
    ap.add_argument("--slot", help="id du créneau (défaut : le premier du compte)")
    ap.add_argument("--animation", help="nom de l'animation à imposer")
    ap.add_argument("--privacy", choices=["public", "unlisted", "private"],
                    help="YouTube uniquement (défaut : réglage du compte)")
    ap.add_argument("--dry-run", action="store_true", help="rendu + aperçu des métadonnées, sans publier")
    a = ap.parse_args()

    setup_logging()
    with open(BASE_DIR / "accounts" / a.account / "config.json", "r", encoding="utf-8") as f:
        account_cfg = json.load(f)
    slots = account_cfg.get("schedule_slots", [])
    slot = next((s for s in slots if s.get("id") == a.slot), None) if a.slot else (slots[0] if slots else {})
    if a.slot and not slot:
        sys.exit(f"Créneau '{a.slot}' introuvable. Créneaux : {[s.get('id') for s in slots]}")

    anim_mgr = AnimationManager()
    if a.animation:
        anim_cfg = anim_mgr.select_animation(a.account, strategy="fixed", target_animation=a.animation)
    else:
        anim_cfg = anim_mgr.select_animation(a.account, strategy=slot.get("animation_strategy", "random"),
                                             target_animation=slot.get("target_animation"),
                                             exclude_animations=slot.get("exclude_animations", []))
    if not anim_cfg:
        sys.exit("Aucune animation disponible pour ce compte / créneau.")
    print(f"Animation : {anim_cfg['name']}  (créneau {slot.get('id', '-')})")

    slot_id = slot.get("id", "manual")
    match_data = {"date": datetime.now().strftime("%Y-%m-%d"), "home": slot_id, "away": "no_match", "resources": {}}
    inputs = anim_cfg.get("inputs", [])
    if "library" in inputs:
        entry = midi_library.choose_song(a.account, anim_cfg.get("library", {}))
        if not entry:
            sys.exit("Aucun morceau disponible dans la bibliothèque.")
        match_data["resources"]["midi_file"] = entry["path"]
        match_data["song_title"] = Path(entry["path"]).stem
        match_data["song"] = {"title": entry["title"], "artist": entry["artist"],
                              "easy": entry.get("easy", False), "credit": entry.get("credit", "")}
        match_data["song_author"] = entry["artist"]
        print(f"Morceau   : {entry['title']} — {entry['artist']} [{entry['category']}, notoriété {entry['popularity']}]")
    jpg_key = "jpg_portrait" if "jpg_portrait" in inputs else "jpg" if "jpg" in inputs else None
    if jpg_key:
        match_data["resources"]["bg_image"] = str(random.choice(list((BASE_DIR / "assets" / jpg_key).glob("*.jpg"))))

    metadata = anim_mgr.generate_job_metadata(anim_cfg, account_cfg, None)
    apply_song_metadata(metadata, match_data)
    if a.privacy:
        metadata["publish_overrides"] = {"privacy_status": a.privacy}

    print("\n========== MÉTADONNÉES ==========")
    print(f"TITRE       : {metadata.get('title')}")
    print(f"DESCRIPTION :\n{metadata.get('description')}")
    print(f"HASHTAGS    : {' '.join(metadata.get('hashtags', []))}")
    if metadata.get("youtube_tags"):
        print(f"TAGS        : {', '.join(metadata['youtube_tags'])}")
    print("=================================\n")

    job_id = f"manual_{datetime.now():%Y%m%d_%H%M%S}_{anim_cfg['name']}"
    video = anim_mgr.execute_animation(match_data, anim_cfg, job_id, job_metadata=metadata)
    work_dir = BASE_DIR / "output" / "temporary" / job_id
    if not video:
        shutil.rmtree(work_dir, ignore_errors=True)
        sys.exit("Échec du rendu (voir les erreurs ci-dessus).")
    thumb = work_dir / "thumbnail_source.jpg"
    if thumb.exists():
        metadata["thumbnail_path"] = str(thumb)

    if a.dry_run:
        keep = BASE_DIR / "output" / "tests" / f"{job_id}.mp4"
        keep.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(video), keep)
        if thumb.exists():
            shutil.copy2(thumb, keep.with_name(f"{job_id}_thumbnail.jpg"))
        shutil.rmtree(work_dir, ignore_errors=True)
        print(f"\n[DRY-RUN] Rien n'a été publié. Vidéo : {keep}")
        return

    midi_library.remember(a.account, {"file": str(Path(match_data["resources"]["midi_file"])
                                                  .relative_to(midi_library.LIBRARY_DIR)).replace("\\", "/")})
    ok = PublicationManager().publish_video(str(video), metadata, a.account)
    Path(video).unlink(missing_ok=True)
    shutil.rmtree(work_dir, ignore_errors=True)
    print("\n✅ Publication réussie." if ok else "\n❌ Échec de la publication (voir les erreurs ci-dessus).")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
