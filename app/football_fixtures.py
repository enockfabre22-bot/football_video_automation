"""
football_fixtures.py — Calendrier automatique des matchs via l'API football-data.org (offre gratuite).

- sync() : télécharge les matchs des prochains jours (championnats majeurs, Ligue des champions, sélections),
  rapproche les équipes du registre data/teams.json, repère les GROS matchs et les écrit dans
  data/matches_api.json (état du VPS, ignoré par git ; data/matches.json reste le calendrier manuel).
  Met aussi à jour, une fois par semaine, la composition réelle des championnats
  (data/football_packs_live.json, lu par football_data.packs()).
- Clé : FOOTBALL_DATA_TOKEN dans .env. Limite gratuite : 10 requêtes / minute (on en fait ~6 par jour).

Usage manuel (depuis la racine) :
    python app/football_fixtures.py            # synchronise et affiche les gros matchs des 3 prochains jours
    python app/football_fixtures.py --all      # affiche aussi les matchs non retenus avec leur note
"""
import json
import os
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path

from dotenv import load_dotenv

import football_data

BASE_DIR = Path(__file__).resolve().parent.parent
API = "https://api.football-data.org/v4"
API_FILE = BASE_DIR / "data" / "matches_api.json"
ALIASES_FILE = BASE_DIR / "data" / "team_aliases.json"
LIVE_PACKS_FILE = BASE_DIR / "data" / "football_packs_live.json"
CACHE_HOURS = 3
PACKS_REFRESH_DAYS = 7

# compétitions suivies : code API -> (nom affiché, pack de popularité, ligue du registre)
COMPETITIONS = {
    "PL": ("Premier League", "premier_league", "ENG"),
    "PD": ("LaLiga", "laliga", "ESP"),
    "SA": ("Serie A", "serie_a", "ITA"),
    "BL1": ("Bundesliga", "bundesliga", "GER"),
    "FL1": ("Ligue 1", "ligue_1", "FRA"),
    "CL": ("Champions League", "champions_league", None),
    "PPL": ("Primeira Liga", None, "POR"),
    "WC": ("World Cup", "world_nations", None),
    "EC": ("Euro", "euro_nations", None),
}
LEAGUE_PACKS = ["PL", "PD", "SA", "BL1", "FL1"]

# derbys et classicos : toujours de gros matchs
DERBIES = [
    ("real madrid", "barcelona"), ("real madrid", "atletico madrid"), ("barcelona", "atletico madrid"),
    ("barcelona", "espanyol"), ("sevilla", "real betis"), ("athletic club", "real sociedad"),
    ("paris saint germain", "marseille"), ("marseille", "lyon"), ("lens", "lille"), ("paris saint germain", "lyon"),
    ("monaco", "nice"), ("inter", "ac milan"), ("juventus", "inter"), ("juventus", "ac milan"), ("as roma", "lazio"),
    ("juventus", "torino"), ("napoli", "as roma"), ("liverpool", "everton"), ("manchester united", "manchester city"),
    ("manchester united", "liverpool"), ("arsenal", "tottenham"), ("arsenal", "chelsea"), ("chelsea", "tottenham"),
    ("liverpool", "manchester city"), ("newcastle", "sunderland"), ("bayern munchen", "borussia dortmund"),
    ("borussia dortmund", "fc schalke 04"), ("hamburger sv", "werder bremen"), ("Benfica", "sporting cp"),
]
TOP_PER_LEAGUE = 7          # les 7 clubs les plus populaires de chaque championnat comptent comme « grands »
TOP_EUROPE = 16             # en Ligue des champions : les 16 plus populaires du pack


def log(msg):
    print(f"[FIXTURES] {msg}", flush=True)


def _token():
    load_dotenv(BASE_DIR / ".env")
    return os.getenv("FOOTBALL_DATA_TOKEN", "").strip()


def _get(path, token):
    req = urllib.request.Request(API + path, headers={"X-Auth-Token": token, "User-Agent": "football-automation"})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                left = r.headers.get("X-Requests-Available-Minute")
                if left is not None and int(left) <= 1:
                    time.sleep(int(r.headers.get("X-RequestCounter-Reset", "60")) + 1)
                return json.loads(r.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt < 2:      # quota de la minute atteint : on attend
                time.sleep(62)
                continue
            raise
    return {}


# --------------------------------------------------------------------------- #
# Rapprochement des équipes de l'API avec le registre (assets/logos)
# --------------------------------------------------------------------------- #
_NOISE = {"fc", "cf", "afc", "sc", "ssc", "ac", "acf", "as", "us", "ss", "sv", "vfl", "tsg", "rc", "rcd", "ca", "cd",
          "ud", "sd", "club", "de", "del", "la", "calcio", "balompie", "futbol", "fk", "sk", "bc", "cfc", "osc",
          "olympique", "stade", "hotspur", "albion", "and", "hove", "1907", "1909", "1913", "1901", "1846", "04"}


def _tokens(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower()
    return [w for w in re.split(r"[^a-z0-9]+", s) if w and w not in _NOISE]


def _aliases():
    if ALIASES_FILE.exists():
        with open(ALIASES_FILE, "r", encoding="utf-8") as f:
            return json.load(f).get("football_data", {})
    return {}


def match_team(team, comp_code):
    """Clé du registre pour une équipe de l'API (ou None)."""
    if not team or team.get("id") is None:
        return None
    alias = _aliases().get(str(team["id"]))
    if alias:
        return alias
    for field in ("name", "shortName"):
        k = football_data.resolve_key(team.get(field) or "")
        if k:
            return k
    reg = football_data.teams()
    target = " ".join(_tokens(team.get("name", "")))
    short = " ".join(_tokens(team.get("shortName", "")))
    for key, t in reg.items():
        cands = {" ".join(_tokens(key)), " ".join(_tokens(t["name"]))}
        if target in cands or (short and short in cands):
            return key
    league = COMPETITIONS.get(comp_code, (None, None, None))[2]
    if league and team.get("tla"):
        for key, t in reg.items():
            if t.get("league") == league and t.get("short", "").upper() == team["tla"].upper():
                return key
    return None


# --------------------------------------------------------------------------- #
# Gros matchs
# --------------------------------------------------------------------------- #
def _rank(key, pack_id):
    teams = football_data.packs().get(pack_id, {}).get("teams", []) if pack_id else []
    return teams.index(key) if key in teams else None


def big_match_score(home, away, comp_code):
    """Note d'intérêt d'une affiche (None = pas un gros match)."""
    pair = {home, away}
    for a, b in DERBIES:
        if pair == {a, b}:
            return 100 - min(_rank(home, COMPETITIONS[comp_code][1]) or 0, 20)
    pack = COMPETITIONS.get(comp_code, (None, None, None))[1]
    if comp_code == "CL":
        # un géant européen (pack « Top European ») contre n'importe quel club connu = gros match ;
        # la note favorise les affiches entre deux grands (le classement garde les meilleures du jour)
        ga, gb = _rank(home, "europe_giants"), _rank(away, "europe_giants")
        ra, rb = _rank(home, "champions_league"), _rank(away, "champions_league")
        if ga is None and gb is None:
            if ra is not None and rb is not None and ra < TOP_EUROPE and rb < TOP_EUROPE:
                return 50 - (ra + rb)
            return None
        best = min(g for g in (ga, gb) if g is not None)
        other = max(ga if ga is not None else 20 + (ra if ra is not None else 10),
                    gb if gb is not None else 20 + (rb if rb is not None else 10))
        return 85 - 2 * best - other
    if comp_code in ("WC", "EC"):
        ra, rb = _rank(home, "world_nations"), _rank(away, "world_nations")
        if ra is not None and rb is not None and ra < 12 and rb < 12:
            return 80 - (ra + rb)
        return None
    ra, rb = _rank(home, pack), _rank(away, pack)
    if ra is not None and rb is not None and ra < TOP_PER_LEAGUE and rb < TOP_PER_LEAGUE:
        return 60 - (ra + rb)
    return None


# --------------------------------------------------------------------------- #
# Composition réelle des championnats (une fois par semaine)
# --------------------------------------------------------------------------- #
def refresh_league_packs(token, force=False):
    if LIVE_PACKS_FILE.exists() and not force:
        age = time.time() - LIVE_PACKS_FILE.stat().st_mtime
        if age < PACKS_REFRESH_DAYS * 86400:
            return
    base = json.loads((BASE_DIR / "data" / "football_packs.json").read_text(encoding="utf-8"))["packs"]
    live = {}
    for code in LEAGUE_PACKS:
        name, pack_id, _ = COMPETITIONS[code]
        try:
            data = _get(f"/competitions/{code}/teams", token)
        except Exception as e:
            log(f"Composition {name} indisponible : {e}")
            continue
        keys, unknown = [], []
        for t in data.get("teams", []):
            k = match_team(t, code)
            (keys if k else unknown).append(k or t.get("name"))
        if len(keys) < 12:
            log(f"Composition {name} ignorée ({len(keys)} équipes reconnues)")
            continue
        order = base[pack_id]["teams"]
        # ordre de popularité du pack de base, puis les nouveaux venus
        live[pack_id] = sorted(keys, key=lambda k: order.index(k) if k in order else 100 + keys.index(k))
        log(f"Composition {name} {data.get('season', {}).get('startDate', '')[:4]} : {len(keys)} clubs"
            + (f" (sans logo/inconnus : {', '.join(unknown)})" if unknown else ""))
    if live:
        LIVE_PACKS_FILE.write_text(json.dumps({"updated": datetime.now().isoformat(timespec="minutes"),
                                               "packs": live}, indent=1, ensure_ascii=False), encoding="utf-8")
        football_data.reset_cache()


# --------------------------------------------------------------------------- #
# Synchronisation des matchs
# --------------------------------------------------------------------------- #
def sync(days=3, force=False, verbose=False):
    """Met à jour data/matches_api.json. Retourne la liste des gros matchs (ou [] sans clé / en cas d'erreur)."""
    token = _token()
    if not token:
        log("FOOTBALL_DATA_TOKEN absent du .env : calendrier automatique désactivé")
        return []
    if API_FILE.exists() and not force and not verbose:
        age = time.time() - API_FILE.stat().st_mtime
        if age < CACHE_HOURS * 3600:
            return load_api_matches()
    try:
        refresh_league_packs(token)
        today = datetime.now().date()
        # dateTo est exclusive côté API
        data = _get(f"/matches?dateFrom={today}&dateTo={today + timedelta(days=days)}", token)
    except Exception as e:
        log(f"API football-data.org indisponible ({e}) : on garde le dernier calendrier connu")
        return load_api_matches()

    kept, skipped = [], []
    for m in data.get("matches", []):
        code = m.get("competition", {}).get("code")
        if code not in COMPETITIONS or m.get("status") not in ("SCHEDULED", "TIMED"):
            continue
        h, a = match_team(m.get("homeTeam"), code), match_team(m.get("awayTeam"), code)
        label = f"{m['homeTeam'].get('shortName')} - {m['awayTeam'].get('shortName')} ({code})"
        if not h or not a or not (football_data.LOGOS_DIR / f"{h}.png").exists() \
                or not (football_data.LOGOS_DIR / f"{a}.png").exists():
            skipped.append(f"{label} : équipe sans logo / inconnue")
            continue
        score = big_match_score(h, a, code)
        kick = datetime.fromisoformat(m["utcDate"].replace("Z", "+00:00")).astimezone()   # heure locale du serveur
        row = {"date": kick.strftime("%Y-%m-%d"), "time": kick.strftime("%H:%M"), "kickoff": kick.isoformat(),
               "home": h, "away": a, "competition": COMPETITIONS[code][0], "big_match": score is not None,
               "score": score or 0, "source": "football-data.org", "api_id": m.get("id")}
        if score is not None:
            kept.append(row)
        elif verbose:
            skipped.append(f"{label} : pas un gros match")
    kept.sort(key=lambda r: (r["date"], -r["score"], r["time"]))
    API_FILE.write_text(json.dumps({"updated": datetime.now().isoformat(timespec="minutes"), "matches": kept},
                                   indent=1, ensure_ascii=False), encoding="utf-8")
    log(f"{len(kept)} gros match(s) sur {days} jours ({len(data.get('matches', []))} matchs lus)")
    if verbose:
        for s in skipped:
            log(f"  ignoré : {s}")
    return kept


def load_api_matches():
    if not API_FILE.exists():
        return []
    try:
        return json.loads(API_FILE.read_text(encoding="utf-8")).get("matches", [])
    except (OSError, json.JSONDecodeError):
        return []


if __name__ == "__main__":
    for r in sync(force=True, verbose="--all" in sys.argv):
        print(f"  ⭐ {r['date']} {r['time']}  {r['competition']:16} {r['home']} vs {r['away']}  (note {r['score']})")
