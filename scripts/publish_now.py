"""
publish_now.py — Rend UNE vidéo et la publie tout de suite, avec exactement le pipeline de production
(choix de l'animation selon le créneau, morceau de la bibliothèque ou équipes foot, titre / description /
tags, miniature). Utile pour vérifier le résultat réel sur YouTube / TikTok sans attendre le planning.

Plusieurs comptes : la vidéo n'est rendue qu'UNE fois et publiée sur chacun (titre propre à chaque compte).
L'animation doit alors être disponible pour tous ces comptes.

Usage (depuis la racine du projet, sur le VPS) :
  python scripts/publish_now.py youtube_piano                               # 1er créneau du compte
  python scripts/publish_now.py youtube_piano --slot video_01_soir          # la vidéo longue 16:9
  python scripts/publish_now.py youtube_piano --animation neon_piano_short  # impose l'animation
  python scripts/publish_now.py youtube_piano --privacy unlisted            # non répertoriée (test discret)
  python scripts/publish_now.py youtube_piano --dry-run                     # rendu + aperçu, sans publier
  python scripts/publish_now.py youtube_avantfoot tiktok_main --animation fb_marble          # foot, 2 comptes
  python scripts/publish_now.py youtube_avantfoot --animation fb_marble --pack premier_league --count 20
  python scripts/publish_now.py youtube_avantfoot tiktok_main --animation fb_duel --home "real madrid" --away barcelona

Ne touche pas aux jobs planifiés du service (identifiant "manual_...") ; le morceau / le championnat est
enregistré dans l'historique du compte (anti-répétition).
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

import football_data  # noqa: E402
import midi_library  # noqa: E402
from animation_manager import AnimationManager  # noqa: E402
from main import apply_song_metadata, metadata_config, setup_logging  # noqa: E402
from match_manager import MatchManager  # noqa: E402
from publication_manager import PublicationManager  # noqa: E402


def load_account(name):
    with open(BASE_DIR / "accounts" / name / "config.json", "r", encoding="utf-8") as f:
        return json.load(f)


def duel_match(a):
    """Match du duel : --home/--away, sinon le 1er gros match du jour (calendrier manuel + automatique)."""
    if a.home and a.away:
        h, w = football_data.team_info(a.home), football_data.team_info(a.away)
        if not (h["has_logo"] and w["has_logo"]):
            sys.exit(f"Logo introuvable : {h['logo'] if not h['has_logo'] else w['logo']}")
        comp = a.competition or ""
        return {"date": datetime.now().strftime("%Y-%m-%d"), "home": h["name"], "away": w["name"],
                "home_tag": h["tag"], "away_tag": w["tag"], "competition": comp,
                "resources": {"home_logo": h["logo"], "away_logo": w["logo"], "home_key": h["key"],
                              "away_key": w["key"], "competition": comp}}
    import football_fixtures
    football_fixtures.sync()
    matches = MatchManager().get_valid_matches(datetime.now().strftime("%Y-%m-%d"))
    if not matches:
        sys.exit("Aucun gros match aujourd'hui : précise --home et --away.")
    return matches[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("accounts", nargs="+", help="un ou plusieurs comptes (ex. youtube_avantfoot tiktok_main)")
    ap.add_argument("--slot", help="id du créneau (défaut : le premier du 1er compte)")
    ap.add_argument("--animation", help="nom de l'animation à imposer")
    ap.add_argument("--pack", help="foot : groupe d'équipes imposé (premier_league, laliga, champions_league...)")
    ap.add_argument("--count", type=int, help="foot : nombre d'équipes imposé")
    ap.add_argument("--home", help="duel : équipe à domicile (nom du logo ou nom anglais)")
    ap.add_argument("--away", help="duel : équipe à l'extérieur")
    ap.add_argument("--competition", help="duel : compétition affichée (ex. LaLiga)")
    ap.add_argument("--privacy", choices=["public", "unlisted", "private"],
                    help="YouTube uniquement (défaut : réglage du compte)")
    ap.add_argument("--dry-run", action="store_true", help="rendu + aperçu des métadonnées, sans publier")
    a = ap.parse_args()

    setup_logging()
    accounts = {name: load_account(name) for name in a.accounts}
    first = a.accounts[0]
    slots = accounts[first].get("schedule_slots", [])
    slot = next((s for s in slots if s.get("id") == a.slot), None) if a.slot else (slots[0] if slots else {})
    if a.slot and not slot:
        sys.exit(f"Créneau '{a.slot}' introuvable. Créneaux : {[s.get('id') for s in slots]}")

    anim_mgr = AnimationManager()
    if a.animation:
        anim_cfg = anim_mgr.select_animation(first, strategy="fixed", target_animation=a.animation)
    else:
        anim_cfg = anim_mgr.select_animation(first, strategy=slot.get("animation_strategy", "random"),
                                             target_animation=slot.get("target_animation"),
                                             exclude_animations=slot.get("exclude_animations", []))
    if not anim_cfg:
        sys.exit("Aucune animation disponible pour ce compte / créneau.")
    for name in a.accounts[1:]:
        if name not in anim_cfg.get("accounts", []):
            sys.exit(f"L'animation '{anim_cfg['name']}' n'est pas assignée au compte '{name}'.")
    print(f"Animation : {anim_cfg['name']}  (comptes : {', '.join(a.accounts)})")

    slot_id = slot.get("id", "manual")
    match_data = {"date": datetime.now().strftime("%Y-%m-%d"), "home": slot_id, "away": "no_match", "resources": {}}
    extra = None
    inputs = anim_cfg.get("inputs", [])
    if "library" in inputs:
        entry = midi_library.choose_song(first, anim_cfg.get("library", {}))
        if not entry:
            sys.exit("Aucun morceau disponible dans la bibliothèque.")
        match_data["resources"]["midi_file"] = entry["path"]
        match_data["song_title"] = Path(entry["path"]).stem
        match_data["song"] = {"title": entry["title"], "artist": entry["artist"],
                              "easy": entry.get("easy", False), "credit": entry.get("credit", "")}
        match_data["song_author"] = entry["artist"]
        print(f"Morceau   : {entry['title']} — {entry['artist']} [{entry['category']}, notoriété {entry['popularity']}]")
    if "football_pack" in inputs:
        cfg = anim_cfg
        if a.count:
            cfg = dict(anim_cfg, football=dict(anim_cfg.get("football", {}), team_counts=[a.count], prefer_full_pack=False))
        lineup = football_data.choose_lineup(first, cfg, pack_id=a.pack)
        if not lineup:
            sys.exit("Aucune composition d'équipes possible (pack / nombre d'équipes ?).")
        match_data["resources"]["football_lineup"] = lineup
        extra = football_data.lineup_placeholders(lineup)
        print(f"Équipes   : {lineup['competition']} ({lineup['count']}) : {', '.join(t['name'] for t in lineup['teams'])}")
    if anim_cfg.get("requires_match_data"):
        match_data = duel_match(a)
        extra = football_data.match_placeholders(match_data)
        print(f"Match     : {match_data['home']} - {match_data['away']} {match_data.get('competition', '')}")
    jpg_key = "jpg_portrait" if "jpg_portrait" in inputs else "jpg" if "jpg" in inputs else None
    if jpg_key:
        match_data["resources"]["bg_image"] = str(random.choice(list((BASE_DIR / "assets" / jpg_key).glob("*.jpg"))))

    # métadonnées propres à chaque compte (hashtags dans le titre sur TikTok, tags YouTube...)
    metas = {}
    for name, account_cfg in accounts.items():
        md = anim_mgr.generate_job_metadata(metadata_config(anim_cfg, match_data), account_cfg,
                                            match_data if anim_cfg.get("requires_match_data") else None,
                                            extra_placeholders=extra)
        apply_song_metadata(md, match_data)
        if a.privacy:
            md["publish_overrides"] = {"privacy_status": a.privacy}
        metas[name] = md
        print(f"\n========== MÉTADONNÉES ({name}) ==========")
        print(f"TITRE       : {md.get('title')}")
        print(f"DESCRIPTION :\n{md.get('description')}")
        print(f"HASHTAGS    : {' '.join(md.get('hashtags', []))}")
        if md.get("youtube_tags"):
            print(f"TAGS        : {', '.join(md['youtube_tags'])}")
    print("==========================================\n")

    job_id = f"manual_{datetime.now():%Y%m%d_%H%M%S}_{anim_cfg['name']}"
    video = anim_mgr.execute_animation(match_data, anim_cfg, job_id, job_metadata=metas[first])
    work_dir = BASE_DIR / "output" / "temporary" / job_id
    if not video:
        shutil.rmtree(work_dir, ignore_errors=True)
        sys.exit("Échec du rendu (voir les erreurs ci-dessus).")
    thumb = work_dir / "thumbnail_source.jpg"
    if thumb.exists():
        for md in metas.values():
            md["thumbnail_path"] = str(thumb)

    if a.dry_run:
        keep = BASE_DIR / "output" / "tests" / f"{job_id}.mp4"
        keep.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(video), keep)
        if thumb.exists():
            shutil.copy2(thumb, keep.with_name(f"{job_id}_thumbnail.jpg"))
        shutil.rmtree(work_dir, ignore_errors=True)
        print(f"\n[DRY-RUN] Rien n'a été publié. Vidéo : {keep}")
        return

    # historiques anti-répétition
    for name in a.accounts:
        if "library" in inputs:
            midi_library.remember(name, {"file": str(Path(match_data["resources"]["midi_file"])
                                                     .relative_to(midi_library.LIBRARY_DIR)).replace("\\", "/")})
        lineup = match_data["resources"].get("football_lineup")
        if lineup:
            football_data.remember(name, anim_cfg["name"], lineup["pack"], match_data["date"])

    results = {}
    for name in a.accounts:
        print(f"\n--- Publication sur {name} ---")
        results[name] = PublicationManager().publish_video(str(video), metas[name], name)
    Path(video).unlink(missing_ok=True)
    shutil.rmtree(work_dir, ignore_errors=True)
    print()
    for name, ok in results.items():
        print(f"{'✅' if ok else '❌'} {name} : {'publiée' if ok else 'échec (voir les erreurs ci-dessus)'}")
    sys.exit(0 if all(results.values()) else 1)


if __name__ == "__main__":
    main()
