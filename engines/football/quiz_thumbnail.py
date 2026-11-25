"""
quiz_thumbnail.py — Miniature YouTube (1280 x 720) du quiz « Guess the Football Player ».

Composition : grand titre à gauche, deux silhouettes noires à contour lumineux avec un « ? » et un
joueur révélé en couleur (niveau EASY), badge du nombre de joueurs. Appelé par finalize.py --quiz-thumbnail.
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageFont

W, H = 1280, 720
LEVEL_COLORS = [(46, 204, 113), (241, 196, 15), (255, 140, 40), (255, 45, 85)]


def _rounds(fb):
    rounds = []
    for line in (fb / "quiz.txt").read_text(encoding="utf-8").splitlines():
        v = line.split("|")
        if v[0] == "R" and len(v) >= 14:
            rounds.append({"level": int(v[2]), "name": v[9], "photo": fb / v[13]})
    return rounds


def _fit(img, h):
    k = h / img.height
    return img.resize((max(1, int(img.width * k)), h), Image.LANCZOS)


def _glow(alpha, color, radius):
    a = alpha.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(radius))
    layer = Image.new("RGBA", alpha.size, color + (0,))
    layer.putalpha(a)
    return layer


def _text(draw, xy, text, font, fill, stroke=10, stroke_fill=(8, 10, 18), anchor="la"):
    draw.text((xy[0] + 6, xy[1] + 8), text, font=font, fill=(0, 0, 0, 170), anchor=anchor, stroke_width=stroke,
              stroke_fill=(0, 0, 0, 170))
    draw.text(xy, text, font=font, fill=fill, anchor=anchor, stroke_width=stroke, stroke_fill=stroke_fill)


def make(work, out_path):
    work = Path(work)
    fb = work / "football"
    rounds = [r for r in _rounds(fb) if r["photo"].exists()]
    if len(rounds) < 3:
        raise RuntimeError("pas assez de photos")
    fonts = fb / "fonts"
    anton = str(fonts / "Anton-Regular.ttf")
    bold = str(fonts / "Poppins-Bold.ttf")

    bg = Image.open(fb / "bg.png").convert("RGB").resize((W, H), Image.LANCZOS)
    bg = ImageEnhance.Brightness(bg).enhance(1.15)
    canvas = bg.convert("RGBA")
    # halo derrière les joueurs
    halo = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(halo).ellipse([620, 60, 1320, 820], fill=(60, 110, 255, 120))
    canvas = Image.alpha_composite(canvas, halo.filter(ImageFilter.GaussianBlur(90)))

    easy = [r for r in rounds if r["level"] == 0] or rounds
    hard = [r for r in rounds if r["level"] >= 2] or rounds[::-1]
    reveal = easy[0]
    mystery = [r for r in hard if r is not reveal][:2]

    # deux silhouettes derrière, un joueur révélé devant
    slots = [(690, 120, 540, LEVEL_COLORS[2]), (1150, 95, 560, LEVEL_COLORS[3])]
    for r, (cx, top, h, col) in zip(mystery, slots):
        p = _fit(Image.open(r["photo"]).convert("RGBA"), h)
        alpha = p.getchannel("A")
        x, y = cx - p.width // 2, top
        canvas.alpha_composite(_glow(alpha, col, 10), (x, y))
        sil = Image.new("RGBA", p.size, (8, 10, 16, 255))
        sil.putalpha(alpha)
        canvas.alpha_composite(sil, (x, y))
        d = ImageDraw.Draw(canvas)
        qx = cx - 60 if cx < 905 else cx + 50          # « ? » décalé vers l'extérieur (pas caché par le joueur révélé)
        _text(d, (qx, top + h * 0.30), "?", ImageFont.truetype(anton, 190), (255, 255, 255), 9, col, anchor="mm")
    p = _fit(Image.open(reveal["photo"]).convert("RGBA"), 600)
    x, y = 905 - p.width // 2, H - 600 + 10
    canvas.alpha_composite(_glow(p.getchannel("A"), (255, 255, 255), 8), (x, y))
    canvas.alpha_composite(p, (x, y))
    # bas de l'image assombri pour la lisibilité
    shade = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shade)
    for i in range(260):
        sd.line([(0, H - i), (W, H - i)], fill=(5, 7, 14, int(170 * (1 - i / 260) ** 1.5)))
    for i in range(560):
        sd.line([(i, 0), (i, H)], fill=(5, 7, 14, int(150 * (1 - i / 560) ** 1.2)))
    canvas = Image.alpha_composite(canvas, shade)

    d = ImageDraw.Draw(canvas)
    _text(d, (48, 40), "GUESS", ImageFont.truetype(anton, 190), (255, 255, 255))
    _text(d, (48, 255), "THE", ImageFont.truetype(anton, 120), (255, 255, 255))
    _text(d, (48, 390), "PLAYER", ImageFont.truetype(anton, 190), (255, 200, 60))
    n = len(rounds)
    badge = f"{n} PLAYERS"
    f = ImageFont.truetype(bold, 46)
    tw = d.textlength(badge, font=f)
    d.rounded_rectangle([52, 622, 52 + tw + 56, 690], radius=34, fill=(255, 45, 85))
    d.text((52 + 28, 656), badge, font=f, fill=(255, 255, 255), anchor="lm")
    canvas.convert("RGB").save(out_path, quality=93)
    return out_path
