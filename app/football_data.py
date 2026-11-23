"""
football_data.py — Équipes et groupes d'équipes (« packs ») des animations foot.

- data/teams.json : registre des équipes (nom anglais affiché, code court, couleurs, hashtag), clé = nom
  du logo dans assets/logos/.
- data/football_packs.json : groupes d'équipes (championnats, sélections...) triés par popularité.
- data/football_history.json : packs déjà utilisés par compte (état du VPS, ignoré par git), pour ne pas
  enchaîner le même championnat sur le même format.

Une animation à N équipes déclare "inputs": ["football_pack"] et une section :
    "football": {"format": "royale", "packs": ["premier_league", ...] | "all",
                 "team_counts": [8, 12, 16, 20], "repeat_days": 4, "popular_bias": 0.0}
"""
import colorsys
import json
import random
import re
import unicodedata
from datetime import datetime, timedelta
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
TEAMS_FILE = BASE_DIR / "data" / "teams.json"
PACKS_FILE = BASE_DIR / "data" / "football_packs.json"
HISTORY_FILE = BASE_DIR / "data" / "football_history.json"
LOGOS_DIR = BASE_DIR / "assets" / "logos"

_cache = {}


def _load(path):
    if path not in _cache:
        with open(path, "r", encoding="utf-8") as f:
            _cache[path] = json.load(f)
    return _cache[path]


def teams():
    return _load(TEAMS_FILE)["teams"]


LIVE_PACKS_FILE = BASE_DIR / "data" / "football_packs_live.json"


def packs():
    """Packs de data/football_packs.json ; la composition réelle de la saison en cours (récupérée par
    football_fixtures.py dans data/football_packs_live.json) remplace celle des championnats."""
    if "packs" not in _cache:
        base = json.loads(json.dumps(_load(PACKS_FILE)["packs"]))
        if LIVE_PACKS_FILE.exists():
            try:
                live = json.loads(LIVE_PACKS_FILE.read_text(encoding="utf-8")).get("packs", {})
                for pid, team_keys in live.items():
                    if pid in base and len(team_keys) >= 12:
                        base[pid]["teams"] = team_keys
            except (OSError, json.JSONDecodeError):
                pass
        _cache["packs"] = base
    return _cache["packs"]


def reset_cache():
    _cache.clear()


def hashtag(text):
    """« Paris Saint-Germain » -> « ParisSaintGermain » (sans accents ni ponctuation)."""
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"[^0-9A-Za-z]", "", text.title() if text.islower() else text)


def _norm(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower()
    return re.sub(r"[^a-z0-9]", "", s)


def resolve_key(name):
    """Retrouve la clé du registre à partir d'un nom (clé, nom affiché, code court ; casse et accents ignorés)."""
    reg = teams()
    if name in reg:
        return name
    target = _norm(name)
    for key, t in reg.items():
        if _norm(key) == target or _norm(t["name"]) == target:
            return key
    for key, t in reg.items():
        if _norm(t.get("short", "")) == target:
            return key
    return None


def _auto_colors(logo_path):
    """Couleurs de secours tirées du logo (équipe absente du registre)."""
    try:
        from PIL import Image
        img = Image.open(logo_path).convert("RGBA").resize((64, 64))
        counts = {}
        for r, g, b, a in img.getdata():
            if a < 128:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if s < 0.25 or v < 0.25:
                continue
            k = (r // 32, g // 32, b // 32)
            counts[k] = counts.get(k, 0) + 1
        if counts:
            k = max(counts, key=counts.get)
            return ["#%02X%02X%02X" % (k[0] * 32 + 16, k[1] * 32 + 16, k[2] * 32 + 16), "#FFFFFF"]
    except Exception:
        pass
    return ["#3C78FF", "#FFFFFF"]


def team_info(key_or_name):
    """Infos complètes d'une équipe (même absente du registre, tant que son logo existe)."""
    key = resolve_key(key_or_name) or key_or_name.strip()
    logo = LOGOS_DIR / f"{key}.png"
    t = teams().get(key)
    if t is None:
        name = " ".join(w.capitalize() for w in key.split())
        t = {"name": name, "short": _norm(name)[:3].upper(), "colors": _auto_colors(logo), "tag": hashtag(name),
             "kind": "club", "league": ""}
    return {"key": key, "name": t["name"], "short": t.get("short", t["name"][:3].upper()),
            "tag": t.get("tag") or hashtag(t["name"]), "colors": list(t["colors"]),
            "kind": t.get("kind", "club"), "logo": str(logo.resolve()), "has_logo": logo.exists()}


# --------------------------------------------------------------------------- #
# Historique (évite de reprendre le même pack sur le même compte trop souvent)
# --------------------------------------------------------------------------- #
def _history():
    if HISTORY_FILE.exists():
        try:
            with open(HISTORY_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except (OSError, json.JSONDecodeError):
            pass
    return {}


def remember(account, animation, pack_id, date_str):
    hist = _history()
    rows = hist.setdefault(account, [])
    rows.append({"date": date_str, "animation": animation, "pack": pack_id})
    hist[account] = rows[-200:]
    HISTORY_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(HISTORY_FILE, "w", encoding="utf-8") as f:
        json.dump(hist, f, indent=1, ensure_ascii=False)


def _recent_packs(account, animation, days):
    limit = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")
    same_anim, any_anim = set(), set()
    for row in _history().get(account, []):
        if row.get("date", "") >= limit:
            any_anim.add(row["pack"])
            if row.get("animation") == animation:
                same_anim.add(row["pack"])
    return same_anim, any_anim


def _weighted_sample(items, k, bias, rng):
    """Tirage sans remise ; bias > 0 favorise le début de liste (équipes les plus populaires)."""
    pool = list(enumerate(items))
    out = []
    while pool and len(out) < k:
        weights = [1.0 / (1.0 + bias * i) for i, _ in pool]
        pick = rng.choices(range(len(pool)), weights=weights)[0]
        out.append(pool.pop(pick))
    out.sort()                                   # garde l'ordre de popularité
    return [x for _, x in out]


def choose_lineup(account, anim_config, date_str=None, rng=None, pack_id=None):
    """
    Choisit un pack et les équipes d'une vidéo. Retourne un dict sérialisable (job.json) :
    {pack, competition, competition_tag, noun, accent, count, teams: [team_info...]} ou None.
    """
    rng = rng or random.Random()
    cfg = anim_config.get("football", {})
    all_packs = packs()
    allowed = cfg.get("packs", "all")
    ids = list(all_packs) if allowed == "all" else [p for p in allowed if p in all_packs]
    counts = sorted(cfg.get("team_counts", [8]))
    min_count = counts[0]

    def usable(pid):
        return len([k for k in all_packs[pid]["teams"] if (LOGOS_DIR / f"{k}.png").exists()]) >= min_count

    ids = [p for p in ids if usable(p)]
    if pack_id:
        ids = [pack_id] if pack_id in all_packs else []
    if not ids:
        print(f"[FOOT] Aucun pack utilisable pour '{anim_config.get('name')}'")
        return None

    if not pack_id:
        same_anim, any_anim = _recent_packs(account, anim_config.get("name"), int(cfg.get("repeat_days", 4)))
        fresh = [p for p in ids if p not in same_anim and p not in any_anim] or \
                [p for p in ids if p not in same_anim] or ids
        weights = [float(all_packs[p].get("weight", 1.0)) for p in fresh]
        pid = rng.choices(fresh, weights=weights)[0]
    else:
        pid = ids[0]

    pack = all_packs[pid]
    keys = [k for k in pack["teams"] if (LOGOS_DIR / f"{k}.png").exists()]
    possible = [c for c in counts if c <= len(keys)]
    if len(keys) <= counts[-1] and len(keys) not in possible and len(keys) >= min_count:
        possible.append(len(keys))              # le pack entier (ex. 18 clubs de Ligue 1)
    # prefer_full_pack : le championnat entier (classement complet) avec la probabilité full_pack_chance,
    # sinon un nombre tiré dans team_counts
    if cfg.get("prefer_full_pack") and len(keys) in possible and rng.random() < float(cfg.get("full_pack_chance", 1.0)):
        count = len(keys)
    else:
        partial = [c for c in possible if c != len(keys)] or possible
        count = rng.choice(partial if cfg.get("prefer_full_pack") else possible)
    chosen = _weighted_sample(keys, count, float(cfg.get("popular_bias", 0.0)), rng)
    rng.shuffle(chosen) if cfg.get("shuffle", True) else None

    return {
        "pack": pid,
        "competition": pack["name"],
        "competition_tag": pack.get("tag") or hashtag(pack["name"]),
        "noun": pack.get("noun", "clubs"),
        "accent": pack.get("accent", "#2F6BFF"),
        "count": count,
        "full_pack": count == len(keys),
        "teams": [team_info(k) for k in chosen],
    }


def lineup_placeholders(lineup):
    """Repères des titres/descriptions/hashtags : {competition} {competition_tag} {count} {noun} {Noun} {single}."""
    single = "nation" if lineup["noun"] == "nations" else "club"
    return {
        "competition": lineup["competition"],
        "competition_tag": lineup["competition_tag"],
        "count": str(lineup["count"]),
        "noun": lineup["noun"],
        "Noun": lineup["noun"].capitalize(),
        "NOUN": lineup["noun"].upper(),
        "single": single,
        "Single": single.capitalize(),
    }


def match_placeholders(match):
    """Repères d'un match (duel) : {competition} {competition_tag} (vides si la compétition est inconnue)."""
    comp = match.get("competition", "") or ""
    return {"competition": comp, "competition_tag": hashtag(comp) if comp else ""}


if __name__ == "__main__":
    for fmt in ("royale", "colorwar"):
        cfg = {"name": f"test_{fmt}", "football": {"team_counts": [4, 6] if fmt == "colorwar" else [12, 16, 20],
                                                    "popular_bias": 0.8 if fmt == "colorwar" else 0}}
        lu = choose_lineup("test", cfg)
        print(fmt, lu["competition"], lu["count"], [t["name"] for t in lu["teams"]])
    missing = [k for k in teams() if not (LOGOS_DIR / f"{k}.png").exists()]
    print("Logos manquants :", missing or "aucun")
    unknown = [p.stem for p in LOGOS_DIR.glob("*.png") if p.stem not in teams()]
    print("Logos hors registre :", unknown or "aucun")
    for pid, p in packs().items():
        bad = [k for k in p["teams"] if k not in teams()]
        if bad:
            print("Pack", pid, "clés inconnues :", bad)
