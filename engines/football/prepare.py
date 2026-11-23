"""
prepare.py — Première étape du moteur foot (formats royale, rings, colorwar, marble, knockout, duel).

Usage : python engines/football/prepare.py <job.json> --format royale [--width 1080 --height 1920]

Lit les équipes du job (resources.football_lineup pour les formats à N équipes, ou le match + les logos
pour le duel) et écrit <work_dir>/football/ :
    scene.txt   réglages lus par la scène raylib (format, textes anglais, couleur du thème...)
    teams.txt   une ligne par équipe : nom|code|couleur|couleur2|halo|logo
    logos/      écussons détourés
    icons/      icônes (cœur, éclair, bouclier, carton, couronne, trophée...)
    fonts/      Anton + Poppins
    bg.png      fond « stade de nuit » à la résolution de rendu
    video.json  réglages vidéo du rendu (durée MAXIMALE : la scène s'arrête d'elle-même)
    meta.json   infos pour finalize.py (ambiance de stade, textes)
"""
import argparse
import colorsys
import json
import math
import random
import shutil
import sys
import zlib
from datetime import datetime
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

BASE_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BASE_DIR / "app"))
import football_data  # noqa: E402

FONTS = ["Anton-Regular.ttf", "Poppins-Bold.ttf", "Poppins-SemiBold.ttf"]
LEAGUES = {"premier_league", "laliga", "serie_a", "bundesliga", "ligue_1"}   # zones C1 / relégation
BEDS_DIR = BASE_DIR / "assets" / "sounds" / "football" / "beds"


def log(msg):
    print(f"      [FOOT] {msg}", flush=True)


# --------------------------------------------------------------------------- #
# Couleurs
# --------------------------------------------------------------------------- #
def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def display_color(rgb):
    """Couleur lisible sur fond sombre. None si la couleur est trop sombre / grise pour servir d'identité."""
    h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in rgb))
    if s < 0.15:
        return (242, 244, 248) if v > 0.6 else None
    v = max(v, 0.72)
    r, g, b = colorsys.hsv_to_rgb(h, min(1.0, s), v)
    return (int(r * 255), int(g * 255), int(b * 255))


def team_colors(colors):
    p, s = hex_rgb(colors[0]), hex_rgb(colors[1] if len(colors) > 1 else "#FFFFFF")
    c1 = display_color(p) or display_color(s) or (242, 244, 248)
    c2 = s
    if sum(abs(a - b) for a, b in zip(c1, c2)) < 90:
        c2 = (20, 22, 30) if sum(c1) > 380 else (245, 245, 250)
    glow = c1
    if colorsys.rgb_to_hsv(*(c / 255 for c in c1))[1] < 0.15:
        glow = display_color(s) or (255, 255, 255)
    return c1, c2, glow


def rgb_str(c):
    return f"{c[0]},{c[1]},{c[2]}"


# --------------------------------------------------------------------------- #
# Écussons
# --------------------------------------------------------------------------- #
def prepare_logo(src, dst, size=384):
    img = Image.open(src).convert("RGBA")
    alpha = img.getchannel("A").point(lambda a: 255 if a > 12 else 0)
    bbox = alpha.getbbox()
    if bbox:
        img = img.crop(bbox)
    w, h = img.size
    k = size / max(w, h)
    img = img.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    img.save(dst)


# --------------------------------------------------------------------------- #
# Fond « stade de nuit »
# --------------------------------------------------------------------------- #
def make_background(path, W, H, accent, seed):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = xx / W, yy / H
    top = np.array([13, 18, 38], np.float32)
    bot = np.array([4, 6, 13], np.float32)
    img = top[None, None, :] * (1 - v[..., None]) + bot[None, None, :] * v[..., None]
    acc = np.array(accent, np.float32)
    # halo coloré derrière l'arène et derrière le titre
    for cx, cy, rad, k in ((0.5, 0.54, 0.55, 0.22), (0.5, 0.13, 0.42, 0.16)):
        d = ((u - cx) ** 2 * (W / H) ** 2 + (v - cy) ** 2) / rad ** 2
        img += acc[None, None, :] * (k * np.exp(-d * 2.2))[..., None]
    # projecteurs (faisceaux depuis les coins hauts)
    for sx, ang in ((-0.05, 0.95), (1.05, math.pi - 0.95)):
        dx, dy = u * (W / H) - sx * (W / H), v + 0.04
        a = np.arctan2(dy, dx)
        beam = np.exp(-((a - ang) / 0.16) ** 2) * np.exp(-np.sqrt(dx ** 2 + dy ** 2) * 1.6)
        img += np.array([170, 185, 220], np.float32)[None, None, :] * (0.20 * beam)[..., None]
    img = np.clip(img, 0, 255)
    base = Image.fromarray(img.astype(np.uint8)).convert("RGBA")

    # bokeh de tribunes (flashs, écharpes) : haut et bas de l'image
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    s = W / 1080
    palette = [(255, 255, 255), (255, 214, 140), tuple(int(c) for c in accent), (140, 190, 255)]
    for _ in range(260):
        band = rng.random()
        y = rng.uniform(0, 0.30) if band < 0.7 else rng.uniform(0.80, 1.0)
        x = rng.uniform(0, 1)
        r = rng.uniform(2, 9) * s * (1.4 if y < 0.12 else 1.0)
        a = int(rng.uniform(14, 70) * (1 - abs(0.5 - x) * 0.6))
        col = palette[rng.integers(0, len(palette))]
        d.ellipse([x * W - r, y * H - r, x * W + r, y * H + r], fill=col + (a,))
    layer = layer.filter(ImageFilter.GaussianBlur(3 * s))
    base = Image.alpha_composite(base, layer)

    # vignette + grain (évite les bandes de couleur à l'encodage)
    arr = np.asarray(base.convert("RGB")).astype(np.float32)
    vig = 1 - 0.45 * np.clip(((u - 0.5) ** 2 * 1.6 + (v - 0.52) ** 2 * 0.9) * 2.2, 0, 1)
    arr *= vig[..., None]
    arr += rng.normal(0, 1.1, arr.shape[:2])[..., None]
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(path)


# --------------------------------------------------------------------------- #
# Icônes (dessinées en 4x puis réduites : bords nets)
# --------------------------------------------------------------------------- #
def _icon(draw_fn, size=256):
    big = Image.new("RGBA", (size * 4, size * 4), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(big), size * 4)
    return big.resize((size, size), Image.LANCZOS)


def _heart(d, S):
    pts = []
    for i in range(200):
        t = i / 200 * 2 * math.pi
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((S / 2 + x * S / 36, S / 2 - y * S / 36 - S * 0.02))
    d.polygon(pts, fill=(255, 52, 92, 255))
    d.ellipse([S * 0.27, S * 0.26, S * 0.42, S * 0.38], fill=(255, 255, 255, 120))


def _bolt(d, S):
    p = [(0.58, 0.05), (0.22, 0.55), (0.47, 0.55), (0.38, 0.95), (0.80, 0.40), (0.54, 0.40), (0.66, 0.05)]
    d.polygon([(x * S, y * S) for x, y in p], fill=(255, 214, 40, 255), outline=(255, 250, 210, 255))


def _shield(d, S):
    p = [(0.5, 0.06), (0.88, 0.2), (0.84, 0.55), (0.5, 0.95), (0.16, 0.55), (0.12, 0.2)]
    d.polygon([(x * S, y * S) for x, y in p], fill=(70, 170, 255, 255))
    p2 = [(0.5, 0.16), (0.78, 0.27), (0.75, 0.53), (0.5, 0.84), (0.25, 0.53), (0.22, 0.27)]
    d.polygon([(x * S, y * S) for x, y in p2], fill=(150, 215, 255, 255))


def _card(d, S):
    d.rounded_rectangle([S * 0.26, S * 0.10, S * 0.74, S * 0.90], radius=S * 0.06, fill=(235, 30, 45, 255))
    d.rounded_rectangle([S * 0.30, S * 0.14, S * 0.48, S * 0.86], radius=S * 0.04, fill=(255, 90, 100, 90))


def _crown(d, S):
    p = [(0.10, 0.80), (0.10, 0.32), (0.32, 0.55), (0.50, 0.18), (0.68, 0.55), (0.90, 0.32), (0.90, 0.80)]
    d.polygon([(x * S, y * S) for x, y in p], fill=(255, 196, 46, 255))
    d.rectangle([S * 0.10, S * 0.74, S * 0.90, S * 0.86], fill=(235, 160, 20, 255))
    for x, y in ((0.10, 0.30), (0.50, 0.16), (0.90, 0.30)):
        d.ellipse([(x - 0.06) * S, (y - 0.06) * S, (x + 0.06) * S, (y + 0.06) * S], fill=(255, 236, 150, 255))
    for x in (0.30, 0.50, 0.70):
        d.ellipse([(x - 0.045) * S, 0.76 * S, (x + 0.045) * S, 0.84 * S], fill=(230, 40, 70, 255))


def _trophy(d, S):
    gold = (255, 196, 46, 255)
    d.pieslice([S * 0.22, S * -0.05, S * 0.78, S * 0.62], 0, 180, fill=gold)
    d.rectangle([S * 0.22, S * 0.06, S * 0.78, S * 0.30], fill=gold)
    d.ellipse([S * 0.08, S * 0.10, S * 0.32, S * 0.36], outline=gold, width=int(S * 0.05))
    d.ellipse([S * 0.68, S * 0.10, S * 0.92, S * 0.36], outline=gold, width=int(S * 0.05))
    d.rectangle([S * 0.45, S * 0.55, S * 0.55, S * 0.75], fill=(235, 160, 20, 255))
    d.rounded_rectangle([S * 0.28, S * 0.75, S * 0.72, S * 0.92], radius=S * 0.03, fill=(235, 160, 20, 255))
    d.ellipse([S * 0.32, S * 0.10, S * 0.42, S * 0.30], fill=(255, 240, 180, 150))


def _star(d, S):
    pts = []
    for i in range(10):
        r = 0.46 if i % 2 == 0 else 0.2
        a = -math.pi / 2 + i * math.pi / 5
        pts.append((S / 2 + math.cos(a) * r * S, S / 2 + math.sin(a) * r * S + S * 0.03))
    d.polygon(pts, fill=(255, 210, 60, 255))


def _skull(d, S):
    d.ellipse([S * 0.18, S * 0.08, S * 0.82, S * 0.70], fill=(245, 245, 250, 255))
    d.rounded_rectangle([S * 0.32, S * 0.55, S * 0.68, S * 0.88], radius=S * 0.05, fill=(245, 245, 250, 255))
    for x in (0.37, 0.63):
        d.ellipse([(x - 0.09) * S, 0.32 * S, (x + 0.09) * S, 0.52 * S], fill=(20, 20, 28, 255))
    d.polygon([(0.5 * S, 0.55 * S), (0.45 * S, 0.65 * S), (0.55 * S, 0.65 * S)], fill=(20, 20, 28, 255))


def _ball(d, S):
    d.ellipse([S * 0.05, S * 0.05, S * 0.95, S * 0.95], fill=(250, 250, 252, 255), outline=(30, 30, 36, 255),
              width=int(S * 0.03))
    c = S / 2

    def pent(cx, cy, r, rot):
        return [(cx + r * math.cos(rot + i * 2 * math.pi / 5), cy + r * math.sin(rot + i * 2 * math.pi / 5))
                for i in range(5)]
    d.polygon(pent(c, c, S * 0.13, -math.pi / 2), fill=(30, 30, 36, 255))
    for k in range(5):
        a = -math.pi / 2 + k * 2 * math.pi / 5
        d.polygon(pent(c + math.cos(a) * S * 0.36, c + math.sin(a) * S * 0.36, S * 0.1, a + math.pi / 2),
                  fill=(30, 30, 36, 255))


def _bomb(d, S):
    d.ellipse([S * 0.12, S * 0.22, S * 0.82, S * 0.92], fill=(40, 40, 52, 255))
    d.ellipse([S * 0.24, S * 0.34, S * 0.40, S * 0.48], fill=(255, 255, 255, 90))
    d.rectangle([S * 0.52, S * 0.14, S * 0.66, S * 0.30], fill=(90, 90, 100, 255))
    d.line([(S * 0.6, S * 0.14), (S * 0.72, S * 0.04)], fill=(200, 160, 90, 255), width=int(S * 0.03))
    d.ellipse([S * 0.68, S * 0.0, S * 0.80, S * 0.10], fill=(255, 210, 60, 255))


def _arrow(d, S):
    d.polygon([(0.5 * S, 0.9 * S), (0.12 * S, 0.45 * S), (0.36 * S, 0.45 * S), (0.36 * S, 0.1 * S),
               (0.64 * S, 0.1 * S), (0.64 * S, 0.45 * S), (0.88 * S, 0.45 * S)], fill=(255, 255, 255, 255))


ICONS = {"heart": _heart, "bolt": _bolt, "shield": _shield, "card": _card, "crown": _crown, "trophy": _trophy,
         "star": _star, "skull": _skull, "ball": _ball, "bomb": _bomb, "arrow": _arrow}


# --------------------------------------------------------------------------- #
# Textes par format (anglais)
# --------------------------------------------------------------------------- #
def texts_for(fmt, lineup, match):
    if fmt == "duel":
        home, away = match["home"], match["away"]
        comp = match.get("competition") or "Match Day"
        date = match.get("date", "")
        try:
            date_txt = datetime.strptime(date, "%Y-%m-%d").strftime("%a %d %b").upper()
        except ValueError:
            date_txt = ""
        return {"title1": f"{home} vs {away}", "title2": "Who wins?", "chip": comp, "date": date_txt,
                "win_line": "wins the battle", "cta": "Comment your score", "noun": "clubs"}
    noun = lineup.get("noun", "clubs")
    single = "nation" if noun == "nations" else "club"
    n, comp = lineup["count"], lineup["competition"]
    if fmt == "royale":
        return {"title1": f"{n} {noun}", "title2": "Only 1 survives", "chip": comp,
                "win_line": f"Last {single} standing", "cta": f"Comment your {single}", "noun": noun}
    if fmt == "rings":
        return {"title1": "Escape the rings", "title2": f"First {single} out wins", "chip": f"{comp} · {n} {noun}",
                "win_line": "escapes first!", "cta": f"Comment your {single}", "noun": noun}
    if fmt == "colorwar":
        return {"title1": "Color war", "title2": "Who conquers the map?", "chip": comp,
                "win_line": "conquers the map", "cta": f"Comment your {single}", "noun": noun}
    if fmt == "marble":
        league = lineup.get("full_pack") and lineup.get("pack") in LEAGUES
        return {"title1": "Marble race", "title2": f"{n} {comp} {noun}", "chip": "Who finishes first?",
                "win_line": "wins the race", "cta": "Where did yours finish?", "noun": noun,
                "table_title": f"{comp} final table" if league else "Final standings", "zones": 1 if league else 0}
    if fmt == "knockout":
        return {"title1": f"{n}-{single} knockout", "title2": "Only 1 lifts the trophy", "chip": comp,
                "win_line": "Champions!", "cta": f"Comment your {single}", "noun": noun}
    raise SystemExit(f"Format inconnu : {fmt}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("job")
    ap.add_argument("--format", required=True, choices=["royale", "rings", "colorwar", "marble", "knockout", "duel"])
    ap.add_argument("--width", type=int, default=1080)
    ap.add_argument("--height", type=int, default=1920)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--max-seconds", type=float, default=75)
    ap.add_argument("--crf", type=int, default=17)
    a = ap.parse_args()

    with open(a.job, "r", encoding="utf-8") as f:
        job = json.load(f)
    work = Path(job["work_dir"])
    out = work / "football"
    if out.exists():
        shutil.rmtree(out)
    for sub in ("logos", "icons", "fonts"):
        (out / sub).mkdir(parents=True)
    seed = zlib.crc32(job.get("job_id", "").encode("utf-8"))
    rnd = random.Random(seed)

    res = job.get("resources", {})
    lineup, match = None, None
    if a.format == "duel":
        m = job.get("match", {})
        home = football_data.team_info(res.get("home_key") or m.get("home", ""))
        away = football_data.team_info(res.get("away_key") or m.get("away", ""))
        if res.get("home_logo"):
            home["logo"] = res["home_logo"]
        if res.get("away_logo"):
            away["logo"] = res["away_logo"]
        team_list = [home, away]
        match = {"home": home["name"], "away": away["name"], "date": m.get("date", ""),
                 "competition": res.get("competition", "")}
        accent = (47, 107, 255)
    else:
        lineup = res.get("football_lineup")
        if not lineup:
            log("ERREUR : aucune composition d'équipes (resources.football_lineup) dans le job")
            return 1
        team_list = lineup["teams"]
        accent = hex_rgb(lineup.get("accent", "#2F6BFF"))
    accent = display_color(accent) or (47, 107, 255)

    lines = []
    for i, t in enumerate(team_list):
        c1, c2, glow = team_colors(t["colors"])
        logo_rel = f"logos/{i:02d}.png"
        if Path(t["logo"]).exists():
            prepare_logo(t["logo"], out / logo_rel)
        else:
            log(f"Logo absent pour {t['name']} : {t['logo']}")
        name = t["name"].replace("|", "/")
        lines.append(f"{name}|{t['short']}|{rgb_str(c1)}|{rgb_str(c2)}|{rgb_str(glow)}|{logo_rel}")
    (out / "teams.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    for name in FONTS:
        shutil.copy2(BASE_DIR / "assets" / "fonts" / name, out / "fonts" / name)
    for name, fn in ICONS.items():
        _icon(fn).save(out / "icons" / f"{name}.png")

    make_background(out / "bg.png", a.width, a.height, accent, seed)

    texts = texts_for(a.format, lineup, match)
    scene = {"format": a.format, "accent": rgb_str(accent), "start_time": 1.0, "candidates": 40, **texts}
    (out / "scene.txt").write_text("".join(f"{k}={v}\n" for k, v in scene.items()), encoding="utf-8")

    video = {"width": a.width, "height": a.height, "fps": a.fps, "duration_s": a.max_seconds, "crf": a.crf}
    (out / "video.json").write_text(json.dumps(video, indent=2), encoding="utf-8")

    beds = sorted(p.name for p in BEDS_DIR.glob("*.ogg"))
    meta = {"format": a.format, "bed": rnd.choice(beds) if beds else "", "teams": [t["name"] for t in team_list],
            **texts}
    (out / "meta.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False), encoding="utf-8")
    log(f"Préparation {a.format} : {len(team_list)} équipes, rendu {a.width}x{a.height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
