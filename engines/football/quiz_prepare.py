"""
quiz_prepare.py — Préparation d'une vidéo longue « Guess the Football Player » (format quiz, 16:9).

Usage : python engines/football/quiz_prepare.py <job.json> [--min-seconds 495] [--fps 30] [--no-history]

- choisit les joueurs (data/quiz/players_db.json, avec photo libre) : 4 niveaux EASY -> IMPOSSIBLE, sans
  reprendre ceux des derniers quiz (data/quiz_history.json, état du VPS) ; ajoute des joueurs jusqu'à
  dépasser --min-seconds (8 min = coupures publicitaires possibles sur YouTube) ;
- calcule tout le minutage (intro, cartes de niveau, manches, fin) -> <work_dir>/football/quiz.txt, lu par
  la scène raylib (format=quiz) ;
- écrit les chapitres YouTube, le titre, la description (crédits photo obligatoires) et les tags dans
  <work_dir>/metadata_override.json (repris par animation_manager.py à la publication).
"""
import argparse
import json
import random
import shutil
import sys
import zlib
from datetime import datetime
from pathlib import Path

ENGINE = Path(__file__).resolve().parent
BASE_DIR = ENGINE.parents[1]
sys.path.insert(0, str(ENGINE))
sys.path.insert(0, str(BASE_DIR / "app"))
import football_data  # noqa: E402
from prepare import FONTS, ICONS, _icon, display_color, make_background, prepare_logo, rgb_str, team_colors  # noqa: E402

DB_FILE = BASE_DIR / "data" / "quiz" / "players_db.json"
HISTORY_FILE = BASE_DIR / "data" / "quiz_history.json"
ASSETS = BASE_DIR / "assets" / "quiz"
LEVELS = [("Easy", (46, 204, 113)), ("Medium", (241, 196, 15)), ("Hard", (255, 140, 40)), ("Impossible", (255, 45, 85))]

MAX_CLUBS = 10          # au-delà, la carrière ne tient plus proprement à l'écran (2 lignes de 5)

# minutage (secondes)
INTRO = 6.0
LEVEL_CARD = 2.4
OUTRO = 9.0
T_NATION, T_POS, T_FIRST_CLUB, CLUB_STEP, AFTER_CLUBS, COUNTDOWN, REVEAL = 0.55, 0.95, 1.45, 0.5, 0.9, 5.0, 3.4


def log(msg):
    print(f"      [QUIZ] {msg}", flush=True)


def round_duration(p):
    return T_FIRST_CLUB + (len(p["career"]) - 1) * CLUB_STEP + AFTER_CLUBS + COUNTDOWN + REVEAL


def total_duration(levels):
    return INTRO + sum(LEVEL_CARD + sum(round_duration(p) for p in lv) for lv in levels) + OUTRO


def club_display_name(label):
    """« Paris Saint-Germain FC » -> « Paris Saint-Germain » (pour les clubs hors registre)."""
    import re
    s = re.sub(r"\b((?:[A-Z]\.)+[A-Z]?)(?=\s|$)", lambda m: m.group(1).replace(".", ""), label)   # « K.V.C. » -> KVC
    s = s.replace("A.FC", "AFC").replace("F.C.", "FC")
    s = re.sub(r"\s+(Football Club|Futebol Clube|Futbol Club|Esporte Clube|Atlético Clube|Sport Club|Clube|Calcio"
               r"|de Barcelona|de Football|19\d\d|20\d\d)$", "", s)
    s = re.sub(r"^(Club Social y Deportivo|Club de Fútbol|Club Atlético|Club Atletico|Clube de Regatas do"
               r"|Clube de Regatas|Club Deportivo|Sport Club)\s+", "", s)
    words = s.split()
    drop = {"FC", "CF", "AFC", "SC", "AC", "SK", "FK", "BK", "CD", "SD", "UD", "VV", "KVC", "KV", "KRC", "RSC", "SS",
            "SSC", "US", "AS", "RCD", "CR", "CA", "SE", "EC", "SV", "NK", "HNK", "PFC", "FBC", "KAA", "SL"}
    while len(words) > 1 and words[-1] in drop:
        words.pop()
    while len(words) > 1 and words[0] in drop:
        words.pop(0)
    if len(words) > 2 and words[0] in {"Club", "Clube"}:      # « Club Athletico Paranaense » (mais « Club Brugge » reste)
        words.pop(0)
    return " ".join(words)


def initials(name):
    w = [x for x in name.replace("-", " ").split() if x[0].isalnum()]
    return (w[0][:3] if len(w) == 1 else "".join(x[0] for x in w[:3])).upper()


def years(c):
    if c["from"] is None:                     # période sans date dans Wikidata
        return f"- {c['to']}" if c["to"] else ""
    if c["to"] is None:
        return f"{c['from']} - NOW" if c["from"] and c["from"] >= 2016 else f"{c['from']} -"
    return f"{c['from']}" if c["to"] == c["from"] else f"{c['from']} - {c['to']}"


def resolve_club(label, qid):
    """Clé du registre d'un club Wikidata : alias par identifiant, nom exact, puis nom sans « FC », « F.C. »..."""
    import re
    aliases = json.loads((BASE_DIR / "data" / "team_aliases.json").read_text(encoding="utf-8")).get("wikidata", {})
    if qid in aliases:
        return aliases[qid]
    from football_fixtures import _tokens
    clean = re.sub(r"\b([A-Z])\.(?=[A-Z]\.|\s|$)", r"\1", label).replace(".", "")
    key = football_data.resolve_key(label) or football_data.resolve_key(clean)
    if key:
        return key
    target = " ".join(_tokens(clean))
    if target in NAME_ALIASES:
        return NAME_ALIASES[target]
    for k, t in football_data.teams().items():
        if target in {" ".join(_tokens(k)), " ".join(_tokens(t["name"]))}:
            return k
    return None


def load_cutout(path):
    """Photo détourée RGBA : JPEG + masque <QID>_alpha.png (format compact), ou ancien PNG RGBA."""
    from PIL import Image
    path = Path(path)
    if path.suffix.lower() == ".png":
        return Image.open(path).convert("RGBA")
    img = Image.open(path).convert("RGBA")
    alpha = path.with_name(path.stem + "_alpha.png")
    if alpha.exists():
        img.putalpha(Image.open(alpha).convert("L"))
    return img


def crest_readable(path):
    """Un écusson presque blanc disparaît sur le disque clair du jeton : on préfère alors les initiales."""
    from PIL import Image
    try:
        im = Image.open(path).convert("RGBA").resize((64, 64))
    except Exception:
        return False
    px = [(r, g, b) for r, g, b, a in im.getdata() if a > 128]
    if len(px) < 200:
        return False
    lum = sum(0.299 * r + 0.587 * g + 0.114 * b for r, g, b in px) / len(px) / 255
    return lum < 0.78


def make_mask(photo, out):
    """Masque de la silhouette (blanc + transparence de la photo détourée) : contour lumineux sans détails."""
    from PIL import Image
    img = Image.open(photo).convert("RGBA")
    mask = Image.new("RGBA", img.size, (255, 255, 255, 0))
    mask.putalpha(img.getchannel("A"))
    mask.save(out)


def fmt_ts(s):
    s = int(s)
    return f"{s // 60}:{s % 60:02d}"


# noms Wikidata (sans « FC ») -> clé du registre quand le nom court du registre diffère
NAME_ALIASES = {"west ham united": "west ham", "newcastle united": "newcastle", "wolverhampton wanderers": "wolves",
                "brighton albion": "brighton", "brighton": "brighton", "inter milan": "inter",
                "internazionale milano": "inter", "lyonnais": "lyon", "deportivo a coruna": "deportivo la coruna", "sl benfica": "Benfica", "celtic": "Celtic FC", "rennais": "rennes", "olympique de marseille": "marseille", "de marseille": "marseille",
                "1 fc koln": "1 fc koln", "borussia monchengladbach": "borussia monchengladbach"}
DISPLAY_NAMES = {"Los Angeles FC": "LAFC", "Los Angeles": "LAFC", "DijonO": "Dijon",   # noms ambigus une fois « FC » retiré
                 "Corinthians Paulista": "Corinthians", "Montpellier Hérault": "Montpellier",
                 "Deportivo Independiente Medellín": "Independiente Medellín",
                 "Instituto Atlético Central Córdoba": "Instituto", "Hokkaido Consadole Sapporo": "Consadole Sapporo",
                 "K Patro Eisden Maasmechelen": "Patro Eisden", "Shabab AlAhli Dubai Club": "Shabab Al Ahli",
                 "Zenit Saint Petersburg": "Zenit", "Athlitiki Enosi Larissa": "AEL Larissa",
                 "Nacional de Football": "Nacional"}


def clean_career(p):
    """Règles automatiques : clubs de jeunes (fini avant 17 ans) et dates impossibles retirés."""
    now = datetime.now().year
    birth = p.get("birth")
    out = []
    for c in p["career"]:
        f, t = c.get("from"), c.get("to")
        if f is None:            # période sans date : sa place dans la carrière est incertaine -> retirée
            continue
        if f > now + 1 or f < 1900:
            continue
        low = c["club"].lower()
        if "national" in low or "olympic" in low or "under-" in low:   # sélections oubliées par Wikidata
            continue
        if t is not None and f is not None and t < f:
            continue
        if birth and t is not None and t <= birth + 17 and (f is None or f < birth + 16):
            continue
        # même club deux fois de suite (doublon Wikidata : « AS Excelsior » / « A.S. Excelsior ») -> fusion
        norm = "".join(ch for ch in club_display_name(c["club"]).lower() if ch.isalnum())
        if out and (out[-1]["qid"] == c["qid"] or out[-1].get("_norm") == norm):
            out[-1]["to"] = t if t is not None else out[-1]["to"]
            continue
        c = dict(c, _norm=norm)
        out.append(c)
    p["career"] = out
    return p


def apply_overrides(db):
    """
    Corrections manuelles (data/quiz/overrides.json), par nom de joueur :
      "exclude": true                 -> jamais utilisé
      "difficulty": 3                 -> niveau corrigé
      "name": "Ronaldo Nazário"       -> nom affiché
      "remove_clubs": ["FC Argeș"]    -> clubs retirés (libellé Wikidata ou identifiant Q...)
      "career": [{"club": "KRC Genk", "from": 2008, "to": 2012}, ...]  -> carrière remplacée entièrement
    """
    path = BASE_DIR / "data" / "quiz" / "overrides.json"
    fixes = json.loads(path.read_text(encoding="utf-8")).get("players", {}) if path.exists() else {}
    out = {}
    for qid, p in db.items():
        p = clean_career(json.loads(json.dumps(p)))
        fx = fixes.get(p["name"])
        if not fx:
            out[qid] = p
            continue
        if fx.get("exclude"):
            continue
        p = json.loads(json.dumps(p))
        for k in ("difficulty", "name", "position", "birth", "nation"):
            if k in fx:
                p[k] = fx[k]
        if fx.get("iso") and (ASSETS / "flags" / f"{fx['iso']}.png").exists():
            p["iso"], p["flag"] = fx["iso"], f"flags/{fx['iso']}.png"
        if fx.get("remove_clubs"):
            drop = set(fx["remove_clubs"])
            p["career"] = [c for c in p["career"] if c["club"] not in drop and c["qid"] not in drop]
        if fx.get("career"):
            p["career"] = [{"qid": c.get("qid") or f"manual:{c['club']}", "club": c["club"], "key": c.get("key"),
                            "crest": c.get("crest"), "from": c.get("from"), "to": c.get("to")} for c in fx["career"]]
        out[qid] = p
    return out


def pick_players(db, rng, min_seconds, use_history):
    hist = {}
    if use_history and HISTORY_FILE.exists():
        hist = json.loads(HISTORY_FILE.read_text(encoding="utf-8"))
    recent = set(q for row in hist.get("quizzes", [])[-6:] for q in row.get("players", []))
    pool = {d: [] for d in range(1, 5)}
    for p in db.values():
        if p.get("photo") and p.get("flag") and 2 <= len(p["career"]) <= MAX_CLUBS and (ASSETS / p["photo"]).exists():
            pool[p["difficulty"]].append(p)
    for d in pool:
        fresh = [p for p in pool[d] if p["qid"] not in recent]
        old = [p for p in pool[d] if p["qid"] in recent]
        rng.shuffle(fresh)
        rng.shuffle(old)
        pool[d] = fresh + old          # les joueurs récents seulement si nécessaire
    levels = [[], [], [], []]
    for d in range(4):                 # 7 par niveau pour commencer
        levels[d] = pool[d + 1][:7]
    k = 0
    while total_duration(levels) < min_seconds:       # puis on complète niveau par niveau
        d = k % 4
        k += 1
        if len(levels[d]) < len(pool[d + 1]):
            levels[d].append(pool[d + 1][len(levels[d])])
        if k > 200:
            break
    # dans chaque niveau : du plus long au plus court n'a pas d'intérêt, on garde l'aléatoire
    return levels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("job")
    ap.add_argument("--min-seconds", type=float, default=495)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--no-history", action="store_true", help="ne pas enregistrer les joueurs utilisés (tests)")
    ap.add_argument("--max-rounds", type=int, help="tests : nombre maximum de joueurs")
    a = ap.parse_args()

    job = json.loads(Path(a.job).read_text(encoding="utf-8"))
    work = Path(job["work_dir"])
    out = work / "football"
    if out.exists():
        shutil.rmtree(out)
    for sub in ("logos", "icons", "fonts", "players", "flags"):
        (out / sub).mkdir(parents=True)
    seed = zlib.crc32(job.get("job_id", "").encode("utf-8"))
    rng = random.Random(seed)

    db = apply_overrides(json.loads(DB_FILE.read_text(encoding="utf-8"))["players"])
    levels = pick_players(db, rng, a.min_seconds, not a.no_history)
    if a.max_rounds:
        per = max(1, a.max_rounds // 4)
        levels = [lv[:per] for lv in levels]
    n_rounds = sum(len(lv) for lv in levels)
    if n_rounds < (4 if a.max_rounds else 12):
        log(f"ERREUR : seulement {n_rounds} joueurs utilisables (photos ?) : lance scripts/quiz_build_players.py")
        return 1

    # clubs -> teams.txt (un écusson par club, réutilisé d'une manche à l'autre)
    team_index, team_lines = {}, []

    def club_team(c):
        if c["qid"] in team_index:
            return team_index[c["qid"]]
        if not c.get("key"):           # rapprochement refait (registre / alias enrichis depuis la construction)
            c["key"] = resolve_club(c["club"], c["qid"])
        if c.get("key"):
            info = football_data.team_info(c["key"])
            name, short, colors, logo_src = info["name"], info["short"], info["colors"], Path(info["logo"])
        else:
            name = club_display_name(c["club"])
            name = DISPLAY_NAMES.get(name, DISPLAY_NAMES.get(c["club"], name))
            short, colors = initials(name), ["#3C5A9A", "#FFFFFF"]
            logo_src = ASSETS / c["crest"] if c.get("crest") and crest_readable(ASSETS / c["crest"]) else None
        c1, c2, glow = team_colors(colors)
        rel = ""
        if logo_src and logo_src.exists():
            rel = f"logos/{len(team_lines):03d}.png"
            try:
                prepare_logo(logo_src, out / rel)
            except Exception:
                rel = ""
        team_lines.append(f"{name.replace('|', '/')}|{short}|{rgb_str(c1)}|{rgb_str(c2)}|{rgb_str(glow)}|{rel}")
        team_index[c["qid"]] = len(team_lines) - 1
        return team_index[c["qid"]]

    lines = []
    t = INTRO
    lines.append(f"T|intro|0|{INTRO:.3f}")
    chapters = [("0:00", "Intro")]
    idx = 0
    used = []
    for d, lv in enumerate(levels):
        if not lv:
            continue
        label, col = LEVELS[d]
        first, last = idx + 1, idx + len(lv)
        lines.append(f"L|{d}|{t:.3f}|{t + LEVEL_CARD:.3f}|{label}|{rgb_str(col)}|{first}|{last}")
        chapters.append((fmt_ts(t), f"Level {d + 1} - {label} (players {first}-{last})"))
        t += LEVEL_CARD
        for p in lv:
            idx += 1
            used.append(p["qid"])
            t0 = t
            t_clubs = [t0 + T_FIRST_CLUB + k * CLUB_STEP for k in range(len(p["career"]))]
            t_count = t_clubs[-1] + AFTER_CLUBS
            t_reveal = t_count + COUNTDOWN
            t1 = t_reveal + REVEAL
            photo_rel = f"players/{p['qid']}.png"
            load_cutout(ASSETS / p["photo"]).save(out / photo_rel)
            make_mask(out / photo_rel, out / f"players/{p['qid']}_mask.png")
            flag_rel = f"flags/{Path(p['flag']).name}"
            if not (out / flag_rel).exists():
                shutil.copy2(ASSETS / p["flag"], out / flag_rel)
            stat = f"BORN {p['birth']}  ·  {len(p['career'])} CLUBS" if p.get("birth") else f"{len(p['career'])} CLUBS"
            name = p["name"].replace("|", "/")
            lines.append("|".join(["R", str(idx), str(d), f"{t0:.3f}", f"{t0 + T_NATION:.3f}", f"{t0 + T_POS:.3f}",
                                   f"{t_count:.3f}", f"{t_reveal:.3f}", f"{t1:.3f}", name, p.get("nation") or "",
                                   flag_rel, "" if p.get("position") in (None, "Footballer") else p["position"],
                                   photo_rel, stat, str(p.get("birth") or "")]))
            for k, c in enumerate(p["career"]):
                lines.append(f"C|{idx}|{club_team(c)}|{years(c)}|{t_clubs[k]:.3f}")
            t = t1
    lines.append(f"T|outro|{t:.3f}|{t + OUTRO:.3f}")
    total = t + OUTRO
    chapters.append((fmt_ts(t), "How many did you get?"))
    (out / "quiz.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "teams.txt").write_text("\n".join(team_lines) + "\n", encoding="utf-8")

    accent = (47, 107, 255)
    for name in FONTS:
        shutil.copy2(BASE_DIR / "assets" / "fonts" / name, out / "fonts" / name)
    for name, fn in ICONS.items():
        _icon(fn).save(out / "icons" / f"{name}.png")
    make_background(out / "bg.png", a.width, a.height, accent, seed)
    scene = {"format": "quiz", "accent": rgb_str(display_color(accent)), "glow": 0.9,
             "title1": "Guess the football player", "title2": "from their career path"}
    (out / "scene.txt").write_text("".join(f"{k}={v}\n" for k, v in scene.items()), encoding="utf-8")
    (out / "video.json").write_text(json.dumps({"width": a.width, "height": a.height, "fps": a.fps,
                                                "duration_s": round(total + 1.0, 2), "crf": 18}), encoding="utf-8")

    # --- métadonnées YouTube : titre, description avec chapitres + crédits, tags ---
    titles = [f"Guess the Football Player from Their Career Path ⚽ ({n_rounds} Players, Easy to Impossible)",
              f"Can You Guess These {n_rounds} Footballers From Their Transfers? 🤔⚽",
              f"Guess the Player by Career Path ⚽ Football Quiz (Easy → Impossible)",
              f"Only Real Fans Can Guess All {n_rounds} Players ⚽ Career Path Quiz"]
    credits = []
    for lv in levels:
        for p in lv:
            c = p.get("credit") or {}
            author = " ".join((c.get("author") or "unknown").split())          # auteur sur une seule ligne
            author = author.replace("Original: ", "").split(" Derivative work")[0][:80]
            credits.append(f"• {p['name']} : photo by {author}, {c.get('license', '')} "
                           f"via Wikimedia Commons ({c.get('url', '')})")
    desc = (f"Can you guess all {n_rounds} football players from their career path? ⚽\n"
            f"Each player: nationality, position and every club of their career. You have 5 seconds!\n"
            f"Comment your score 👇\n\n"
            + "\n".join(f"{ts} {label}" for ts, label in chapters) +
            "\n\n🔔 Subscribe for a new football quiz every week!\n\n"
            "Player data: Wikidata (CC0). Player photos (cropped, background removed):\n" + "\n".join(credits))
    override = {
        "title": rng.choice(titles)[:100],
        "description": desc,
        "hashtags": ["#footballquiz", "#guesstheplayer", "#football", "#quiz", "#soccer"],
        "youtube_tags": ["guess the player", "football quiz", "guess the football player", "career path quiz",
                         "guess the player by career", "football quiz 2026", "soccer quiz", "guess the footballer",
                         "football trivia", "transfer quiz", "who am i football", "football", "quiz"],
        "quiz_players": used,
    }
    (work / "metadata_override.json").write_text(json.dumps(override, indent=1, ensure_ascii=False), encoding="utf-8")
    (out / "meta.json").write_text(json.dumps({"format": "quiz", "bed": "", "duration": total,
                                               "players": [p["name"] for lv in levels for p in lv]},
                                              ensure_ascii=False, indent=1), encoding="utf-8")
    if not a.no_history:
        hist = json.loads(HISTORY_FILE.read_text(encoding="utf-8")) if HISTORY_FILE.exists() else {"quizzes": []}
        hist["quizzes"].append({"date": datetime.now().strftime("%Y-%m-%d"), "players": used})
        hist["quizzes"] = hist["quizzes"][-30:]
        HISTORY_FILE.write_text(json.dumps(hist, indent=1), encoding="utf-8")
    log(f"{n_rounds} joueurs ({', '.join(str(len(lv)) for lv in levels)} par niveau), {len(team_lines)} clubs, "
        f"durée {fmt_ts(total)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
