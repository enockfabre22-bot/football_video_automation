"""
quiz_build_players.py — Construit la base du quiz « Guess the Player » (vidéos longues).

Pour chaque joueur de data/quiz/players.json :
  - Wikidata (données libres CC0) : carrière en club (années, matchs, buts), nationalité sportive, poste, naissance ;
  - Wikimedia Commons : photo sous licence libre (CC0 / CC BY / CC BY-SA / domaine public) + auteur et licence
    (crédit obligatoire, ajouté automatiquement à la description des vidéos) ;
  - détourage de la photo (rembg, hors ligne) -> assets/quiz/players/<QID>.png ;
  - drapeau -> assets/quiz/flags/<iso>.png (flagcdn.com, domaine public) ;
  - écusson des clubs absents de assets/logos (logo Commons libre si disponible) -> assets/quiz/crests/<QID>.png.
Résultat : data/quiz/players_db.json. Cache des réponses brutes : data/quiz/cache/ (ignoré par git).

Usage (depuis la racine du projet, sur le PC : le détourage n'est pas installé sur le VPS) :
    python scripts/quiz_build_players.py                 # tous les joueurs (les déjà traités sont gardés)
    python scripts/quiz_build_players.py --only "Kaká"   # un seul joueur (refait)
    python scripts/quiz_build_players.py --refresh       # tout refaire (ignore le cache)
Ensuite : git add data/quiz assets/quiz && git commit && git push (le VPS n'a besoin que du résultat).
"""
import argparse
import html
import io
import json
import re
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BASE_DIR / "app"))
import football_data  # noqa: E402

QUIZ_DIR = BASE_DIR / "data" / "quiz"
CACHE = QUIZ_DIR / "cache"
DB_FILE = QUIZ_DIR / "players_db.json"
ASSETS = BASE_DIR / "assets" / "quiz"
UA = {"User-Agent": "football-quiz-builder/1.0 (educational football quiz videos; Python urllib)"}
WD = "https://www.wikidata.org/w/api.php"
COMMONS = "https://commons.wikimedia.org/w/api.php"

NATIONAL_TEAM = {"Q6979593", "Q1194951", "Q23905105", "Q4498974"}   # sélections (A, jeunes, olympiques...)
RESERVE_TEAM = {"Q2412834", "Q852446"}                               # équipes réserves / B
# équipes réserves / jeunes : suffixe « B », « C », « II »... (pas le « C » de « F.C. ») ou mots-clés
RESERVE_WORDS = re.compile(r"(\s(B|C|II|III)$|\bU-?\d{2}\b|Under-\d{2}|\bReserves?\b|Juvenil|Primavera|\bYouth\b|"
                           r"Academy|Castilla|Atl[eè]tic\b|Promesas|Mestalla|^Jong\s|Sub-\d{2})", re.I)
ISO_OVERRIDE = {"Q21": "gb-eng", "Q22": "gb-sct", "Q25": "gb-wls", "Q26": "gb-nir"}
WITH_CRESTS = False
FREE_LICENSE = re.compile(r"^(cc0|cc[ -]by(-sa)?([ -][\d.]+)?|public domain|pd\b|pd-)", re.I)


def log(msg):
    print(msg, flush=True)


def http_json(url, cache_name=None, refresh=False):
    if cache_name:
        f = CACHE / cache_name
        if f.exists() and not refresh:
            return json.loads(f.read_text(encoding="utf-8"))
    waited, attempt = 0, 0
    while True:
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=40) as r:
                data = json.loads(r.read().decode("utf-8"))
            break
        except urllib.error.HTTPError as e:
            if e.code == 429 and waited < 3600:    # quota dépassé : vraie pause, sans abandonner le joueur
                pause = max(120.0, _retry_delay(e, attempt))
                log(f"   Wikimedia (API) demande une pause : {int(pause)} s")
                time.sleep(pause)
                waited += pause
                continue
            attempt += 1
            if attempt >= 4:
                raise
            time.sleep(_retry_delay(e, attempt))
        except Exception as e:
            attempt += 1
            if attempt >= 4:
                raise
            time.sleep(_retry_delay(e, attempt))
    if isinstance(data, dict) and "error" in data and data["error"].get("code") in ("ratelimited", "maxlag"):
        log("   Wikimedia (API) limite le débit : pause 120 s")   # réponse 200 contenant une erreur
        time.sleep(120)
        return http_json(url, cache_name, refresh)
    if cache_name and not (isinstance(data, dict) and "error" in data):
        (CACHE / cache_name).write_text(json.dumps(data), encoding="utf-8")
    time.sleep(1.5)               # rythme raisonnable pour les API Wikimedia (au-delà : blocages de 2 min)
    return data


def _retry_delay(e, attempt):
    """Délai avant nouvel essai : celui demandé par Wikimedia (Retry-After) en cas de 429, sinon croissant."""
    retry = getattr(e, "headers", None) and e.headers.get("Retry-After")
    try:
        return min(120, float(retry)) + 1 if retry else 3 + attempt * 6
    except ValueError:
        return 3 + attempt * 6


def http_bytes(url):
    """Téléchargement d'image. Wikimedia bloque l'adresse IP (429) si l'on va trop vite : on fait alors une
    vraie pause (≥ 3 min) et on reprend, sans abandonner le joueur (jusqu'à ~2 h d'attente cumulée)."""
    waited = 0
    for attempt in range(60):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
                data = r.read()
            time.sleep(1.0)          # images : on reste raisonnable avec Wikimedia
            return data
        except urllib.error.HTTPError as e:
            if e.code != 429 or waited > 7200:
                if attempt >= 3:
                    raise
                time.sleep(_retry_delay(e, attempt))
                continue
            pause = max(180.0, _retry_delay(e, attempt))
            log(f"   Wikimedia demande une pause : {int(pause)} s")
            time.sleep(pause)
            waited += pause
        except Exception as e:
            if attempt >= 3:
                raise
            time.sleep(_retry_delay(e, attempt))
    raise RuntimeError("téléchargement impossible")


def val(snak):
    return (snak.get("datavalue") or {}).get("value")


def wd_time(q, prop):
    try:
        t = val(q[prop][0])["time"]
        return int(t[1:5])
    except (KeyError, IndexError, TypeError, ValueError):
        return None


def wd_amount(q, prop):
    try:
        return int(float(val(q[prop][0])["amount"]))
    except (KeyError, IndexError, TypeError, ValueError):
        return None


def search_player(entry, refresh):
    if entry.get("qid"):
        return entry["qid"]
    # « search » = autre nom à chercher (la recherche Wikidata ne porte que sur les noms) ;
    # « hint » = mot qui doit figurer dans la description (ex. « Uruguayan ») pour départager les homonymes
    term = entry.get("search") or entry["name"]
    hint = (entry.get("hint") or "").lower()
    key = "search_" + re.sub(r"[^a-z0-9]+", "_", unicodedata.normalize("NFKD", term).encode("ascii", "ignore").decode().lower())
    url = f"{WD}?action=wbsearchentities&search={urllib.parse.quote(term)}&language=en&format=json&limit=12&type=item"
    res = http_json(url, key + ".json", refresh).get("search", [])
    football = [r for r in res if any(w in (r.get("description") or "").lower() for w in ("football", "soccer"))]
    if hint:
        football = [r for r in football if hint in (r.get("description") or "").lower()]
    return football[0]["id"] if football else None


def entities(ids, props="labels|claims", refresh=False):
    out = {}
    ids = [i for i in dict.fromkeys(ids) if i]
    for k in range(0, len(ids), 40):
        chunk = ids[k:k + 40]
        url = f"{WD}?action=wbgetentities&ids={'|'.join(chunk)}&props={props}&languages=en&format=json"
        name = "ents_" + "_".join(sorted(chunk))[:120] + f"_{len(chunk)}.json"
        out.update(http_json(url, name, refresh).get("entities", {}))
    return out


def label(ent):
    return ((ent or {}).get("labels", {}).get("en") or {}).get("value", "")


def p31(ent):
    return {val(s["mainsnak"])["id"] for s in (ent or {}).get("claims", {}).get("P31", []) if val(s["mainsnak"])}


def simplify_position(labels):
    text = " ".join(labels).lower()
    if "goalkeeper" in text:
        return "Goalkeeper"
    if "forward" in text or "striker" in text or "winger" in text:
        return "Forward"
    if "midfield" in text:
        return "Midfielder"
    if "back" in text or "defender" in text:
        return "Defender"
    return "Footballer"


def club_key(name, qid, aliases):
    if qid in aliases:
        return aliases[qid]
    clean = re.sub(r"([A-Z])\.(?=[A-Z]\.|\s|$)", r"", name).replace(".", "")   # « F.C. » -> « FC »
    return football_data.resolve_key(name) or football_data.resolve_key(clean) or _token_match(clean)


def _token_match(name):
    from football_fixtures import _tokens
    target = " ".join(_tokens(name))
    for key, t in football_data.teams().items():
        if target in {" ".join(_tokens(key)), " ".join(_tokens(t["name"]))}:
            return key
    return None


def commons_file_info(fname, width, refresh):
    title = "File:" + fname
    url = (f"{COMMONS}?action=query&titles={urllib.parse.quote(title)}&prop=imageinfo"
           f"&iiprop=url|extmetadata|size&iiurlwidth={width}&format=json")
    data = http_json(url, "img_" + re.sub(r"[^A-Za-z0-9]+", "_", fname)[:100] + f"_{width}.json", refresh)
    page = next(iter(data.get("query", {}).get("pages", {}).values()), {})
    info = (page.get("imageinfo") or [{}])[0]
    meta = info.get("extmetadata", {})
    def m(k):
        return html.unescape(re.sub(r"<[^>]+>", "", (meta.get(k) or {}).get("value", ""))).strip()
    return {"thumb": info.get("thumburl"), "orig": info.get("url"), "size": info.get("size", 0),
            "page": info.get("descriptionurl"), "license": m("LicenseShortName"),
            "license_url": m("LicenseUrl"), "artist": m("Artist") or m("Credit"), "file": fname}


_rembg_session = None


def shrink(img_bytes, max_side=1400):
    """Réduit une photo originale (souvent 4000 px et plus) avant le détourage."""
    from PIL import Image
    im = Image.open(io.BytesIO(img_bytes))
    im = im.convert("RGB")
    if max(im.size) > max_side:
        im.thumbnail((max_side, max_side), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=95)
    return buf.getvalue()


def cutout(img_bytes):
    """Détourage : retourne une image RGBA recadrée sur la personne, ou None si le résultat est douteux."""
    global _rembg_session
    from PIL import Image
    from rembg import new_session, remove
    if _rembg_session is None:
        _rembg_session = new_session("u2net_human_seg")
    src = Image.open(io.BytesIO(img_bytes)).convert("RGB")
    out = remove(src, session=_rembg_session)
    alpha = out.getchannel("A")
    bbox = alpha.point(lambda a: 255 if a > 40 else 0).getbbox()
    if not bbox:
        return None
    w, h = src.size
    area = (bbox[2] - bbox[0]) * (bbox[3] - bbox[1]) / (w * h)
    hist = alpha.histogram()
    opaque = sum(hist[200:]) / (w * h)
    if area < 0.06 or opaque < 0.04:
        return None
    pad = 12
    bbox = (max(0, bbox[0] - pad), max(0, bbox[1] - pad), min(w, bbox[2] + pad), min(h, bbox[3] + pad))
    out = out.crop(bbox)
    # portrait : on garde le haut du corps si la photo est en pied (le visage doit rester lisible)
    ow, oh = out.size
    if oh > ow * 1.9:
        out = out.crop((0, 0, ow, int(ow * 1.55)))
    out.thumbnail((900, 900), Image.LANCZOS)
    return out


def store_cutout(img, qid):
    """Photo détourée stockée en JPEG + masque de transparence PNG (≈ 10 fois plus léger qu'un PNG RGBA)."""
    from PIL import Image
    rgb = Image.new("RGB", img.size, (0, 0, 0))
    rgb.paste(img.convert("RGB"), mask=img.getchannel("A"))
    rgb.save(ASSETS / "players" / f"{qid}.jpg", quality=92, optimize=True)
    img.getchannel("A").save(ASSETS / "players" / f"{qid}_alpha.png", optimize=True)
    old = ASSETS / "players" / f"{qid}.png"
    if old.exists():
        old.unlink()


def compact_photos(db):
    """Convertit les anciens PNG RGBA en JPEG + masque (base construite avant ce format)."""
    from PIL import Image
    for p in db["players"].values():
        if p.get("photo", "") and p["photo"].endswith(".png"):
            src = ASSETS / p["photo"]
            if src.exists():
                store_cutout(Image.open(src).convert("RGBA"), p["qid"])
            p["photo"] = f"players/{p['qid']}.jpg"


def ensure_flag(iso):
    f = ASSETS / "flags" / f"{iso}.png"
    if not f.exists():
        f.write_bytes(http_bytes(f"https://flagcdn.com/w320/{iso}.png"))
    return f"flags/{iso}.png"


def ensure_crest(club_qid, club_ent, refresh):
    """Écusson libre (Commons, P154) d'un club absent de assets/logos ; None sinon."""
    f = ASSETS / "crests" / f"{club_qid}.png"
    if f.exists() and not refresh:
        return f"crests/{club_qid}.png"
    logos = club_ent.get("claims", {}).get("P154", [])
    if not logos:
        return None
    fname = val(logos[0]["mainsnak"])
    if not fname:
        return None
    info = commons_file_info(fname, 330, refresh)      # tailles standard Wikimedia
    if not info.get("thumb"):
        return None
    try:
        f.write_bytes(http_bytes(info["thumb"]))
    except Exception:
        return None
    return f"crests/{club_qid}.png"


def build_player(entry, aliases, refresh, with_photo=True):
    qid = search_player(entry, refresh)
    if not qid:
        return None, "introuvable sur Wikidata"
    ent = entities([qid], refresh=refresh)[qid]
    claims = ent.get("claims", {})
    name = entry["name"]
    # carrière en club
    stints = []
    last_from = 0
    for order, s in enumerate(claims.get("P54", [])):
        v = val(s["mainsnak"])
        if not v:
            continue
        q = s.get("qualifiers", {})
        start = wd_time(q, "P580")
        stints.append({"qid": v["id"], "from": start, "to": wd_time(q, "P582"), "order": order,
                       "sort": start if start is not None else last_from,
                       "apps": wd_amount(q, "P1350"), "goals": wd_amount(q, "P1351")})
        if start is not None:
            last_from = start
    clubs_ent = entities([s["qid"] for s in stints], refresh=refresh)
    career = []
    for s in sorted(stints, key=lambda s: (s["sort"], s["to"] or 9999, s["order"])):
        ce = clubs_ent.get(s["qid"], {})
        lab = label(ce)
        kinds = p31(ce)
        if not lab or kinds & NATIONAL_TEAM or "national" in lab.lower() or kinds & RESERVE_TEAM or RESERVE_WORDS.search(lab):
            continue
        if s["apps"] == 0:
            continue
        if career and career[-1]["qid"] == s["qid"]:            # même club enchaîné (prêt / retour)
            career[-1]["to"] = s["to"]
            for k in ("apps", "goals"):
                if s[k] is not None:
                    career[-1][k] = (career[-1][k] or 0) + s[k]
            continue
        key = club_key(lab, s["qid"], aliases.get("wikidata", {}))
        crest = None
        if not key and WITH_CRESTS:
            crest = ensure_crest(s["qid"], ce, refresh)
        career.append({"qid": s["qid"], "club": lab, "key": key, "crest": crest, "from": s["from"], "to": s["to"],
                       "apps": s["apps"], "goals": s["goals"]})
    if len(career) < 2:
        return None, f"carrière trop courte ou incomplète ({len(career)} club)"

    # nationalité sportive, poste, naissance
    nat = [val(s["mainsnak"])["id"] for s in (claims.get("P1532") or claims.get("P27") or []) if val(s["mainsnak"])]
    pos = [val(s["mainsnak"])["id"] for s in claims.get("P413", []) if val(s["mainsnak"])]
    extra = entities(nat[:1] + pos[:3], refresh=refresh)
    nation, iso = None, None
    if nat:
        ne = extra.get(nat[0], {})
        nation = label(ne)
        iso = ISO_OVERRIDE.get(nat[0])
        if not iso:
            codes = [val(s["mainsnak"]) for s in ne.get("claims", {}).get("P297", []) if val(s["mainsnak"])]
            iso = codes[0].lower() if codes else None
    flag = None
    if iso:
        try:
            flag = ensure_flag(iso)
        except Exception:
            flag = None
    position = simplify_position([label(extra.get(p, {})) for p in pos])
    birth = None
    if claims.get("P569"):
        try:
            birth = int(val(claims["P569"][0]["mainsnak"])["time"][1:5])
        except (TypeError, KeyError, ValueError):
            pass

    # photo libre + détourage
    photo, credit = None, None
    if with_photo:
        for s in claims.get("P18", [])[:3]:
            fname = val(s["mainsnak"])
            if not fname:
                continue
            info = commons_file_info(fname, 960, refresh)
            if not info.get("thumb") or not FREE_LICENSE.search(info.get("license", "")):
                continue
            out_file = ASSETS / "players" / f"{qid}.jpg"
            if not out_file.exists() or refresh:
                try:
                    # original (redimensionné ici) : Wikimedia refuse aux robots les grandes miniatures
                    # générées à la volée (429), pas les fichiers originaux
                    src = info["orig"] if info.get("orig") and info.get("size", 0) < 25_000_000 else info["thumb"]
                    img = cutout(shrink(http_bytes(src)))
                except Exception as e:
                    log(f"   détourage impossible ({e})")
                    img = None
                if img is None:
                    continue
                store_cutout(img, qid)
            photo = f"players/{qid}.jpg"
            credit = {"author": info["artist"][:120], "license": info["license"], "url": info["page"], "file": fname}
            break
    return {"qid": qid, "name": name, "difficulty": entry["difficulty"], "nation": nation, "iso": iso, "flag": flag,
            "position": position, "birth": birth, "career": career, "photo": photo, "credit": credit}, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--no-photo", action="store_true")
    ap.add_argument("--all", action="store_true", help="recalcule tous les joueurs (cache et photos réutilisés)")
    ap.add_argument("--crests", action="store_true",
                    help="cherche aussi les écussons libres des petits clubs (beaucoup de requêtes : à faire à part)")
    a = ap.parse_args()
    global WITH_CRESTS
    WITH_CRESTS = a.crests
    for d in (CACHE, ASSETS / "players", ASSETS / "flags", ASSETS / "crests"):
        d.mkdir(parents=True, exist_ok=True)
    players = json.loads((QUIZ_DIR / "players.json").read_text(encoding="utf-8"))["players"]
    aliases = json.loads((BASE_DIR / "data" / "team_aliases.json").read_text(encoding="utf-8"))
    db = json.loads(DB_FILE.read_text(encoding="utf-8")) if DB_FILE.exists() else {"players": {}}
    done = {p["name"]: q for q, p in db["players"].items()}
    ok = skipped = 0
    for entry in players:
        if a.only and entry["name"] != a.only:
            continue
        if entry["name"] in done and not a.refresh and not a.only and not a.all \
                and (a.no_photo or db["players"][done[entry["name"]]].get("photo")):
            ok += 1
            continue
        try:
            rec, why = build_player(entry, aliases, a.refresh or bool(a.only), with_photo=not a.no_photo)
        except Exception as e:
            rec, why = None, f"erreur : {e}"
        if not rec:
            log(f"✗ {entry['name']} : {why}")
            skipped += 1
            continue
        if entry["name"] in done:
            db["players"].pop(done[entry["name"]], None)
        db["players"][rec["qid"]] = rec
        ok += 1
        clubs = " > ".join(f"{c['club']}{'' if c['key'] or c['crest'] else '*'}" for c in rec["career"])
        log(f"✓ {rec['name']:24} [{rec['difficulty']}] {rec['nation']} {rec['position']:10} "
            f"photo={'oui' if rec['photo'] else 'NON'} | {clubs}")
        DB_FILE.write_text(json.dumps(db, indent=1, ensure_ascii=False), encoding="utf-8")
    compact_photos(db)
    # rapprochement des clubs refait à la fin (alias / registre enrichis entre deux passages)
    for p in db["players"].values():
        for c in p["career"]:
            if not c.get("key"):
                c["key"] = club_key(c["club"], c["qid"], aliases.get("wikidata", {}))
    DB_FILE.write_text(json.dumps(db, indent=1, ensure_ascii=False), encoding="utf-8")
    with_photo = sum(1 for p in db["players"].values() if p["photo"])
    missing = {}
    for p in db["players"].values():
        for c in p["career"]:
            if not c["key"] and not c["crest"]:
                missing[(c["qid"], c["club"])] = missing.get((c["qid"], c["club"]), 0) + 1
    if missing:
        log("\nClubs sans écusson (ajouter un logo dans assets/logos + data/teams.json, ou un alias wikidata) :")
        for (q, n), k in sorted(missing.items(), key=lambda x: -x[1])[:80]:
            log(f"   {q:10} {n} ({k} joueur(s))")
    log(f"\n{ok} joueurs dans la base ({with_photo} avec photo libre), {skipped} écartés. * = club sans écusson")


if __name__ == "__main__":
    main()
