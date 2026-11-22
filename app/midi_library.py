"""
midi_library.py — Bibliothèque MIDI piano (assets/library/) : catalogue, choix du morceau, historique.

assets/library/library.json : {"songs": [{file, title, artist, category, popularity (1..3), easy,
                                           duration, enabled, source}]}
Catégories : pop, film, anime, game, tv, classical, modern_piano, holiday.

Choix d'un morceau (choose_song) :
  1. filtres : enabled, catégories autorisées, durée minimale, Noël seulement en saison (15/11 -> 06/01) ;
  2. pas de répétition : un morceau publié sur le compte depuis moins de `repeat_days` jours est écarté ;
  3. tirage d'une catégorie selon `category_weights` (variété), puis d'un morceau pondéré par sa
     notoriété (3 -> x9, 2 -> x4, 1 -> x1) : les morceaux très recherchés passent plus souvent.
Réglages par animation (config.json) : "library": {"categories": [...], "min_duration": 90,
"category_weights": {...}, "repeat_days": 45}.
"""
import json
import random
from datetime import date, datetime, timedelta
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
LIBRARY_DIR = BASE_DIR / "assets" / "library"
CATALOG = LIBRARY_DIR / "library.json"
HISTORY = BASE_DIR / "data" / "song_history.json"

CATEGORIES = ["pop", "film", "anime", "game", "tv", "classical", "modern_piano", "holiday"]
DEFAULT_WEIGHTS = {"pop": 0.40, "film": 0.16, "classical": 0.14, "modern_piano": 0.08, "anime": 0.08,
                   "game": 0.07, "tv": 0.07, "holiday": 0.12}
POPULARITY_WEIGHT = {1: 1.0, 2: 4.0, 3: 9.0}
CREDITS = {"piano-midi.de": "MIDI: Bernd Krueger, piano-midi.de (CC BY-SA 3.0 DE)"}


def load_catalog():
    with open(CATALOG, "r", encoding="utf-8") as f:
        return json.load(f)


def save_catalog(cat):
    cat["songs"].sort(key=lambda e: (e["category"], -e["popularity"], e["artist"].lower(), e["title"].lower()))
    with open(CATALOG, "w", encoding="utf-8") as f:
        json.dump(cat, f, indent=1, ensure_ascii=False)


def holiday_season(d=None):
    d = d or date.today()
    return (d.month == 11 and d.day >= 15) or d.month == 12 or (d.month == 1 and d.day <= 6)


def _load_history():
    try:
        with open(HISTORY, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def recently_used(account, days):
    limit = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")
    return {h["file"] for h in _load_history().get(account, []) if h.get("date", "") >= limit}


def remember(account, entry, day=None):
    hist = _load_history()
    items = hist.setdefault(account, [])
    items.append({"file": entry["file"], "date": day or datetime.now().strftime("%Y-%m-%d")})
    hist[account] = items[-400:]
    HISTORY.parent.mkdir(parents=True, exist_ok=True)
    with open(HISTORY, "w", encoding="utf-8") as f:
        json.dump(hist, f, indent=1, ensure_ascii=False)


def choose_song(account, settings=None, rng=None):
    """Retourne une entrée du catalogue (+ "path" absolu), ou None si rien ne convient."""
    settings = settings or {}
    rng = rng or random
    allowed = settings.get("categories") or CATEGORIES
    min_dur = float(settings.get("min_duration", 35))
    weights = dict(DEFAULT_WEIGHTS, **settings.get("category_weights", {}))
    in_season = holiday_season()
    pool = [e for e in load_catalog()["songs"]
            if e.get("enabled", True) and e["category"] in allowed and e.get("duration", 0) >= min_dur
            and (e["category"] != "holiday" or in_season)
            and (LIBRARY_DIR / e["file"]).exists()]
    if not pool:
        return None
    used = recently_used(account, int(settings.get("repeat_days", 45)))
    fresh = [e for e in pool if e["file"] not in used] or pool      # tout a servi : on recycle
    by_cat = {}
    for e in fresh:
        by_cat.setdefault(e["category"], []).append(e)
    cats = list(by_cat)
    cat = rng.choices(cats, weights=[weights.get(c, 0.05) for c in cats])[0]
    songs = by_cat[cat]
    entry = dict(rng.choices(songs, weights=[POPULARITY_WEIGHT.get(e.get("popularity", 2), 4.0) for e in songs])[0])
    entry["path"] = str(LIBRARY_DIR / entry["file"])
    entry["credit"] = next((c for k, c in CREDITS.items() if k in entry.get("source", "")), "")
    return entry
