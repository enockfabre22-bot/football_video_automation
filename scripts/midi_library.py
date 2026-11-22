"""
midi_library.py — Gestion de la bibliothèque MIDI piano (assets/library/).

Usage (depuis la racine du projet) :
  python scripts/midi_library.py stats
  python scripts/midi_library.py list [--category film] [--search "hisaishi"]
  python scripts/midi_library.py import <fichier.mid|dossier> --category anime [--popularity 3]
         [--artist "Joe Hisaishi" --title "One Summer's Day"] [--easy] [--source "musescore"]
  python scripts/midi_library.py disable "<texte du titre ou de l'artiste>"
  python scripts/midi_library.py enable  "<texte>"
  python scripts/midi_library.py check      # vérifie que chaque entrée a son fichier (et inversement)

Import : chaque fichier est analysé (durée >= 35 s, >= 120 notes, pas de batterie ni d'autre instrument que le
piano) puis copié en "<catégorie>/<Artiste> - <Titre>[ - EASY].mid". Sans --artist/--title, le nom du fichier doit
être "Artiste - Titre.mid". Notoriété : 1 peu connu, 2 connu (défaut), 3 très recherché (passe ~2x plus souvent).
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BASE_DIR / "app"))
sys.path.insert(0, str(BASE_DIR / "engines" / "piano"))

import midi_library as lib  # noqa: E402


def analyse(path):
    import mido
    import piano_midi
    mid = mido.MidiFile(str(path), clip=True)
    drums = nonpiano = 0
    for tr in mid.tracks:
        for m in tr:
            if m.type == "program_change" and m.program > 7:
                nonpiano += 1
            if m.type == "note_on" and getattr(m, "channel", 0) == 9 and m.velocity > 0:
                drums += 1
    song = piano_midi.load_song(str(path))
    problems = []
    if song.length < 35:
        problems.append(f"trop court ({song.length:.0f} s)")
    if len(song.notes) < 120:
        problems.append(f"trop peu de notes ({len(song.notes)})")
    if drums or nonpiano:
        problems.append("batterie ou instrument autre que le piano")
    return round(song.length, 1), problems


def safe(name):
    return re.sub(r'[\\/:*?"<>|]', "", re.sub(r"\s+", " ", name)).strip()


def cmd_import(a):
    src = Path(a.path)
    files = sorted(src.glob("*.mid")) + sorted(src.glob("*.midi")) if src.is_dir() else [src]
    cat = lib.load_catalog()
    existing = {(e["artist"].lower(), e["title"].lower()) for e in cat["songs"]}
    added = 0
    for f in files:
        artist, title = a.artist, a.title
        if not (artist and title):
            parts = [p.strip() for p in f.stem.split(" - ") if p.strip()]
            easy_in_name = bool(parts) and parts[-1].upper() == "EASY"
            parts = parts[:-1] if easy_in_name else parts
            if len(parts) < 2:
                print(f"  [IGNORÉ] {f.name} : nommer le fichier 'Artiste - Titre.mid' ou utiliser --artist/--title")
                continue
            artist, title = parts[0], " - ".join(parts[1:])
        else:
            easy_in_name = False
        easy = a.easy or easy_in_name
        if (artist.lower(), title.lower()) in existing:
            print(f"  [DÉJÀ PRÉSENT] {artist} - {title}")
            continue
        try:
            duration, problems = analyse(f)
        except Exception as e:
            print(f"  [REJETÉ] {f.name} : illisible ({e})")
            continue
        if problems:
            print(f"  [REJETÉ] {f.name} : {', '.join(problems)}")
            continue
        name = safe(f"{artist} - {title}" + (" - EASY" if easy else ""))
        rel = f"{a.category}/{name}.mid"
        (lib.LIBRARY_DIR / a.category).mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, lib.LIBRARY_DIR / rel)
        cat["songs"].append({"file": rel, "title": title, "artist": artist, "category": a.category,
                             "popularity": a.popularity, "easy": easy, "duration": duration,
                             "enabled": True, "source": a.source})
        existing.add((artist.lower(), title.lower()))
        added += 1
        print(f"  [AJOUTÉ] {rel} ({duration:.0f} s, notoriété {a.popularity})")
    lib.save_catalog(cat)
    print(f"{added} morceau(x) ajouté(s).")


def cmd_stats(_):
    songs = lib.load_catalog()["songs"]
    on = [e for e in songs if e.get("enabled", True)]
    print(f"{len(on)} morceaux actifs ({len(songs) - len(on)} désactivés)")
    for c in lib.CATEGORIES:
        sub = [e for e in on if e["category"] == c]
        tiers = [sum(e["popularity"] == t for e in sub) for t in (3, 2, 1)]
        print(f"  {c:13} {len(sub):4}   très recherchés {tiers[0]:3} | connus {tiers[1]:3} | peu connus {tiers[2]:3}")


def cmd_list(a):
    for e in lib.load_catalog()["songs"]:
        if a.category and e["category"] != a.category:
            continue
        line = f"{e['artist']} - {e['title']}"
        if a.search and a.search.lower() not in line.lower():
            continue
        flag = "" if e.get("enabled", True) else "  [désactivé]"
        print(f"  [{e['category']:12}] {'*' * e['popularity']:3} {line}{' (Easy)' if e.get('easy') else ''}{flag}")


def cmd_toggle(a, value):
    cat = lib.load_catalog()
    n = 0
    for e in cat["songs"]:
        if a.query.lower() in f"{e['artist']} - {e['title']}".lower():
            e["enabled"] = value
            n += 1
            print(f"  {'activé' if value else 'désactivé'} : {e['artist']} - {e['title']}")
    lib.save_catalog(cat)
    print(f"{n} morceau(x) modifié(s).")


def cmd_check(_):
    songs = lib.load_catalog()["songs"]
    listed = {e["file"] for e in songs}
    missing = [f for f in listed if not (lib.LIBRARY_DIR / f).exists()]
    on_disk = {str(p.relative_to(lib.LIBRARY_DIR)).replace("\\", "/") for p in lib.LIBRARY_DIR.rglob("*.mid")}
    orphans = sorted(on_disk - listed)
    print(f"{len(songs)} entrées, {len(missing)} fichier(s) manquant(s), {len(orphans)} fichier(s) hors catalogue")
    for f in missing:
        print("  manquant :", f)
    for f in orphans:
        print("  hors catalogue :", f)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("import")
    p.add_argument("path")
    p.add_argument("--category", required=True, choices=lib.CATEGORIES)
    p.add_argument("--popularity", type=int, default=2, choices=[1, 2, 3])
    p.add_argument("--artist")
    p.add_argument("--title")
    p.add_argument("--easy", action="store_true")
    p.add_argument("--source", default="import manuel")
    sub.add_parser("stats")
    p = sub.add_parser("list")
    p.add_argument("--category", choices=lib.CATEGORIES)
    p.add_argument("--search")
    for name in ("disable", "enable"):
        p = sub.add_parser(name)
        p.add_argument("query")
    sub.add_parser("check")
    a = ap.parse_args()
    {"import": cmd_import, "stats": cmd_stats, "list": cmd_list, "check": cmd_check,
     "disable": lambda x: cmd_toggle(x, False), "enable": lambda x: cmd_toggle(x, True)}[a.cmd](a)


if __name__ == "__main__":
    main()
