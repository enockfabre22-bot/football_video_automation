"""
prepare.py — Étape 1 du moteur piano (étape "python" d'une animation).

Usage : python engines/piano/prepare.py <job.json> [--format portrait|landscape]
        [--max-seconds 58] [--excerpt auto|full] [--names artist_title|title_artist] [--fps 60]

Écrit dans <work_dir>/piano/ tout ce dont la scène raylib (étape sandbox) a besoin :
  scene.txt (réglages), notes.txt (notes en temps vidéo), background.png, title.png,
  audio.wav (piano final), video.json (taille/fps/durée, lu par l'étape sandbox), meta.json.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageOps

import piano_audio
import piano_midi

BASE_DIR = Path(__file__).resolve().parents[2]
FONT_DIR = BASE_DIR / "assets" / "fonts"

FORMATS = {
    # largeur, hauteur, secondes de notes visibles au-dessus du clavier, touches blanches minimum
    "portrait": {"size": (1080, 1920), "lookahead": 2.6, "min_whites": 22},
    "landscape": {"size": (1920, 1080), "lookahead": 2.3, "min_whites": 36},
}

# Couples de couleurs (main droite, main gauche) : vives, contrastées, lisibles sur fond sombre
PALETTES = [
    ((0, 225, 255), (255, 70, 170)),     # cyan / magenta
    ((255, 196, 60), (150, 95, 255)),    # or / violet
    ((60, 240, 170), (70, 150, 255)),    # émeraude / bleu
    ((255, 110, 95), (40, 210, 205)),    # corail / turquoise
    ((255, 100, 200), (120, 205, 255)),  # rose / glace
    ((170, 255, 90), (0, 190, 255)),     # citron vert / azur
]

LEAD = 0.12          # la première note frappe 0,12 s après le début : aucun temps mort


def log(msg):
    print(f"      [PIANO] {msg}", flush=True)


# --------------------------------------------------------------------------- #
# Textes
# --------------------------------------------------------------------------- #
def clean_names(stem, names):
    stem = re.sub(r"\s*\(\d+\)\s*$", "", stem)                 # "(2)" des doublons
    stem = re.sub(r"\\[a-z]+(\[[^\]]*\])?(\{[^}]*\})?", "", stem)  # restes LaTeX
    stem = re.sub(r"\s+", " ", stem).strip()
    parts = [p.strip() for p in stem.split(" - ") if p.strip()]
    parts = [p for p in parts if p.upper() not in ("EASY", "PIANO EDITION")]
    if len(parts) < 2:
        return (parts[0] if parts else stem), ""
    if names == "artist_title":
        return " - ".join(parts[1:]), parts[0]
    return parts[0], " - ".join(parts[1:])


def _font(name, size):
    path = FONT_DIR / name
    try:
        return ImageFont.truetype(str(path), size)
    except OSError:
        return ImageFont.load_default(size)


def _fit_font(draw, text, name, size, max_w, min_size):
    while size > min_size:
        f = _font(name, size)
        if draw.textlength(text, font=f) <= max_w:
            return f
        size -= 2
    return _font(name, min_size)


def make_title_png(path, title, artist, fmt, width):
    """Titre + artiste, blanc avec ombre douce, sur fond transparent (dessiné par la scène)."""
    portrait = fmt == "portrait"
    max_w = int(width * (0.86 if portrait else 0.5))
    t_size, a_size = (66, 38) if portrait else (46, 28)
    tmp = ImageDraw.Draw(Image.new("RGBA", (10, 10)))
    tf = _fit_font(tmp, title, "Poppins-Bold.ttf", t_size, max_w, 34 if portrait else 26)
    af = _fit_font(tmp, artist, "Poppins-Medium.ttf", a_size, max_w, 22)
    tw = int(tmp.textlength(title, font=tf))
    aw = int(tmp.textlength(artist, font=af)) if artist else 0
    th = tf.getbbox("Hg")[3]
    ah = af.getbbox("Hg")[3] if artist else 0
    pad = 30
    W = max(tw, aw) + 2 * pad
    H = th + (ah + 10 if artist else 0) + 2 * pad
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    anchor_x = W // 2 if portrait else pad
    align = "ma" if portrait else "la"
    d.text((anchor_x, pad), title, font=tf, fill=(255, 255, 255, 255), anchor=align)
    if artist:
        d.text((anchor_x, pad + th + 10), artist, font=af, fill=(255, 255, 255, 185), anchor=align)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.putalpha(img.getchannel("A").filter(ImageFilter.GaussianBlur(9)).point(lambda a: int(a * 0.85)))
    out = Image.alpha_composite(shadow, img)
    out.save(path)


def _with_shadow(img, blur=9, strength=0.85):
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    shadow.putalpha(img.getchannel("A").filter(ImageFilter.GaussianBlur(blur)).point(lambda a: int(a * strength)))
    return Image.alpha_composite(shadow, img)


def make_guess_png(path, width):
    """Bandeau du mode devinette : la question + la promesse de la réponse à la fin."""
    tmp = ImageDraw.Draw(Image.new("RGBA", (10, 10)))
    q, sub = "Can you guess the song?", "Answer at the end"
    qf = _fit_font(tmp, q, "Poppins-Bold.ttf", 64, int(width * 0.9), 36)
    sf = _font("Poppins-Medium.ttf", 36)
    qh, sh = qf.getbbox("Hg")[3], sf.getbbox("Hg")[3]
    pad = 30
    W = int(max(tmp.textlength(q, font=qf), tmp.textlength(sub, font=sf))) + 2 * pad
    H = qh + sh + 12 + 2 * pad
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.text((W // 2, pad), q, font=qf, fill=(255, 255, 255, 255), anchor="ma")
    d.text((W // 2, pad + qh + 12), sub, font=sf, fill=(255, 255, 255, 190), anchor="ma")
    _with_shadow(img).save(path)


def make_answer_png(path, title, artist, width, palette):
    """Carte de la réponse : étiquette, grand titre, artiste, sur un panneau sombre arrondi."""
    tmp = ImageDraw.Draw(Image.new("RGBA", (10, 10)))
    max_w = int(width * 0.78)
    lf = _font("Poppins-SemiBold.ttf", 30)
    tf = _fit_font(tmp, title, "Poppins-Bold.ttf", 82, max_w, 40)
    af = _fit_font(tmp, artist, "Poppins-Medium.ttf", 46, max_w, 26) if artist else None
    label = "T H E   A N S W E R"
    lh, th = lf.getbbox("Hg")[3], tf.getbbox("Hg")[3]
    ah = af.getbbox("Hg")[3] if af else 0
    inner_w = max(tmp.textlength(label, font=lf), tmp.textlength(title, font=tf),
                  tmp.textlength(artist, font=af) if af else 0)
    padx, pady, m = 56, 40, 24
    W = int(inner_w) + 2 * padx + 2 * m
    H = lh + 18 + th + (ah + 14 if af else 0) + 2 * pady + 2 * m
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    accent = tuple(palette[0]) + (255,)
    d.rounded_rectangle([m, m, W - m, H - m], radius=36, fill=(10, 10, 18, 215), outline=accent, width=3)
    y = m + pady
    d.text((W // 2, y), label, font=lf, fill=accent, anchor="ma")
    y += lh + 18
    d.text((W // 2, y), title, font=tf, fill=(255, 255, 255, 255), anchor="ma")
    if af:
        d.text((W // 2, y + th + 14), artist, font=af, fill=(255, 255, 255, 200), anchor="ma")
    _with_shadow(img, blur=14, strength=0.9).save(path)


def _layout_title_lines(title, width, region_h):
    """Découpe le titre en 1 à 3 lignes avec la plus grande police qui tient dans la zone."""
    tmp = ImageDraw.Draw(Image.new("L", (10, 10)))
    words = title.split()
    for size in range(230, 70, -6):
        f = _font("Poppins-Bold.ttf", size)
        lines, cur = [], ""
        for w in words:
            cand = (cur + " " + w).strip()
            if tmp.textlength(cand, font=f) <= width * 0.9 or not cur:
                cur = cand
            else:
                lines.append(cur)
                cur = w
        lines.append(cur)
        line_h = int(size * 1.08)
        if len(lines) <= 3 and all(tmp.textlength(l, font=f) <= width * 0.92 for l in lines) \
                and line_h * len(lines) <= region_h:
            return f, lines, line_h
    f = _font("Poppins-Bold.ttf", 70)
    return f, [title], int(70 * 1.08)


def make_reveal_assets(out, title, artist, W, region_top, region_bottom, palette, n_points, rng):
    """
    Style "reveal" : le titre est construit par les particules lancées par chaque note.
    Écrit reveal_title.png (titre net, révélé à la fin), artist.png, et retourne les points
    cibles (coordonnées vidéo) dans un ordre aléatoire + la position de l'artiste.
    """
    region_h = region_bottom - region_top
    font, lines, line_h = _layout_title_lines(title, W, region_h)
    text_h = line_h * len(lines)
    y0 = (region_h - text_h) // 2
    mask = Image.new("L", (W, region_h), 0)
    md = ImageDraw.Draw(mask)
    for i, line in enumerate(lines):
        md.text((W // 2, y0 + i * line_h), line, font=font, fill=255, anchor="ma")

    # titre net : blanc + halo aux couleurs des mains
    a = mask.filter(ImageFilter.GaussianBlur(16))
    halo = Image.new("RGBA", (W, region_h), tuple(palette[0]) + (0,))
    halo.putalpha(a.point(lambda v: int(v * 0.9)))
    crisp = Image.new("RGBA", (W, region_h), (255, 255, 255, 0))
    crisp.putalpha(mask)
    Image.alpha_composite(halo, crisp).save(out / "reveal_title.png")

    # points cibles régulièrement répartis dans les lettres (grille + léger décalage)
    arr = np.asarray(mask) > 128
    lo, hi = 3.0, 60.0
    for _ in range(30):
        s = (lo + hi) / 2
        gy, gx = np.mgrid[s / 2:region_h:s, s / 2:W:s]
        inside = arr[gy.astype(int).clip(0, region_h - 1), gx.astype(int).clip(0, W - 1)]
        if inside.sum() > n_points:
            lo = s
        else:
            hi = s
    gy, gx = np.mgrid[s / 2:region_h:s, s / 2:W:s]
    inside = arr[gy.astype(int).clip(0, region_h - 1), gx.astype(int).clip(0, W - 1)]
    pts = np.stack([gx[inside], gy[inside]], 1) + rng.uniform(-0.3, 0.3, (int(inside.sum()), 2)) * s
    pts[:, 1] += region_top
    pts = pts[rng.permutation(len(pts))]

    artist_y = region_top + y0 + text_h + 24
    if artist:
        af = _fit_font(ImageDraw.Draw(Image.new("RGBA", (10, 10))), artist, "Poppins-Medium.ttf", 56, int(W * 0.85), 28)
        ah = af.getbbox("Hg")[3]
        img = Image.new("RGBA", (W, ah + 40), (0, 0, 0, 0))
        ImageDraw.Draw(img).text((W // 2, 20), artist, font=af, fill=(255, 255, 255, 225), anchor="ma")
        _with_shadow(img).save(out / "artist.png")
    return pts, artist_y


def make_background(path, src, size, palette):
    """Image de fond floutée et assombrie (les notes lumineuses ressortent), ou dégradé si absente."""
    W, H = size
    if src and Path(src).exists():
        img = Image.open(src).convert("RGB")
        img = ImageOps.fit(img, (W // 2, H // 2), Image.LANCZOS)
        img = img.filter(ImageFilter.GaussianBlur(18))
        img = ImageEnhance.Color(img).enhance(1.2)
        img = ImageEnhance.Brightness(img).enhance(0.22)
        img = img.resize((W, H), Image.BICUBIC)
    else:
        img = Image.new("RGB", (W, H), (8, 8, 16))
    arr = np.asarray(img).astype(np.float32)
    # teinte légère des couleurs de mains + vignette + bas plus sombre (sous le clavier)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    tint = (np.array(palette[0]) * (1 - yy / H)[..., None] + np.array(palette[1]) * (yy / H)[..., None]) * 0.06
    r = np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H * 0.45) / (H / 2)) ** 2)
    vign = np.clip(1.15 - 0.65 * r ** 1.6, 0.2, 1.0)[..., None]
    arr = (arr + tint) * vign
    noise = np.random.default_rng(3).normal(0, 1.6, arr.shape)      # léger grain : évite le banding
    Image.fromarray(np.clip(arr + noise, 0, 255).astype(np.uint8)).save(path)


# --------------------------------------------------------------------------- #
# Clavier
# --------------------------------------------------------------------------- #
def is_white(p):
    return p % 12 in (0, 2, 4, 5, 7, 9, 11)


def key_range(notes, min_whites):
    lo, hi = min(n.pitch for n in notes), max(n.pitch for n in notes)
    while not is_white(lo):
        lo -= 1
    while not is_white(hi):
        hi += 1
    lo, hi = max(21, lo - 1), min(108, hi + 1)          # une touche de marge de chaque côté
    while not is_white(lo):
        lo -= 1
    while not is_white(hi):
        hi += 1
    whites = lambda a, b: sum(1 for p in range(a, b + 1) if is_white(p))
    side = 0
    while whites(lo, hi) < min_whites and (lo > 21 or hi < 108):
        if (side % 2 == 0 and hi < 108) or lo <= 21:
            hi += 1
            while not is_white(hi):
                hi += 1
        else:
            lo -= 1
            while not is_white(lo):
                lo -= 1
        side += 1
    return lo, hi


# --------------------------------------------------------------------------- #
# Programme
# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("job")
    ap.add_argument("--format", default="portrait", choices=FORMATS.keys())
    ap.add_argument("--max-seconds", type=float, default=58.0)
    ap.add_argument("--excerpt", default="auto", choices=["auto", "full"])
    ap.add_argument("--names", default="artist_title", choices=["artist_title", "title_artist"])
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--auto-pedal", default="on", choices=["on", "off"])
    ap.add_argument("--style", default="falling", choices=["falling", "bounce", "reveal"])
    ap.add_argument("--guess", default="off", choices=["on", "off"],
                    help="titre caché + 'Can you guess the song?' + réponse révélée à la fin")
    ap.add_argument("--lead", type=float, default=None,
                    help="secondes avant la 1re note (défaut 0.12 ; 0.6 en bounce pour voir la balle tomber)")
    a = ap.parse_args()
    lead = a.lead if a.lead is not None else (0.6 if a.style == "bounce" else LEAD)
    if a.style == "reveal":
        a.guess = "on"                       # le style reveal EST une devinette
    guess = a.guess == "on"

    t_start = time.time()
    with open(a.job, "r", encoding="utf-8") as f:
        job = json.load(f)
    work = Path(job["work_dir"])
    out = work / "piano"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    res = job.get("resources", {}) or {}
    meta = job.get("metadata", {}) or {}
    midi_path = res.get("midi_file") or meta.get("midi_file")
    if not midi_path or not Path(midi_path).exists():
        log(f"ERREUR : fichier MIDI introuvable ({midi_path})")
        return 1
    soundfont = piano_audio.find_soundfont()
    if not soundfont:
        log("ERREUR : aucun SoundFont dans assets/soundfonts/ (voir scripts/setup_piano.sh)")
        return 1

    fmt = FORMATS[a.format]
    W, H = fmt["size"]
    seed_text = job.get("job_id", midi_path)
    rng = np.random.default_rng(int(hashlib.md5(seed_text.encode()).hexdigest()[:8], 16))

    # --- Lecture + interprétation ---
    song = piano_midi.load_song(midi_path)
    log(f"MIDI : {len(song.notes)} notes, {song.length:.1f} s, pédale={'oui' if song.has_pedal else 'non'}, "
        f"nuances={'plates' if song.flat_velocity else 'présentes'}")
    if song.flat_velocity:
        piano_midi.humanize_velocities(song, seed_text)
        log("Nuances recréées (mélodie en avant, accents, phrasé)")
    if not song.has_pedal and a.auto_pedal == "on":
        piano_midi.auto_pedal(song)
        log("Pédale de sustain automatique (suit les changements de basse)")

    # --- Extrait ---
    if a.excerpt == "full":
        s0 = song.notes[0].start
        s1 = min(song.length, s0 + a.max_seconds)
        reason = "morceau complet"
        tail = 2.5 if s1 >= song.length - 0.01 else 0.0
    else:
        s0, s1, reason = piano_midi.choose_excerpt(song, a.max_seconds)
        tail = 0.0
    duration = round((s1 - s0) + lead + tail, 3)
    log(f"Extrait {s0:.1f}s -> {s1:.1f}s ({duration:.1f} s vidéo) : {reason}")

    offset = lead - s0                       # temps vidéo = temps morceau + offset
    audio_notes = [n for n in song.notes if s0 - 0.01 <= n.start < s1]
    # falling : les notes à venir continuent de tomber jusqu'à la dernière image (pas d'écran vide)
    extra = fmt["lookahead"] + 0.5 if a.style == "falling" else 0.0
    visual_notes = [n for n in song.notes if s0 - 0.01 <= n.start < s1 + extra]
    if not audio_notes:
        log("ERREUR : extrait sans notes")
        return 1

    # --- Audio ---
    pedal = [(t, v) for t, v in song.pedal if s0 - 0.01 <= t < s1 + 3]
    if piano_midi.pedal_state_at(song.pedal, s0 - 0.01) >= 64:
        pedal.insert(0, (s0, 100))
    render_mid = out / "render.mid"
    piano_audio.write_render_midi(render_mid, audio_notes, pedal, offset)
    dry = out / "dry.wav"
    piano_audio.render_fluidsynth(render_mid, dry, soundfont)
    fade_out = 0.0 if a.excerpt == "full" and tail > 0 else min(2.5, duration * 0.08)
    piano_audio.process(dry, out / "audio.wav", duration, fade_out=fade_out, work_dir=out)
    dry.unlink(missing_ok=True)
    log(f"Audio piano rendu ({soundfont.name}, réverbération de salle, -14 LUFS)")

    # --- Visuel ---
    palette = PALETTES[int(rng.integers(len(PALETTES)))]
    lo, hi = key_range(visual_notes, fmt["min_whites"])
    title, artist = clean_names(meta.get("song_title") or Path(midi_path).stem, a.names)
    make_title_png(out / "title.png", title, artist, a.format, W)
    reveal_time = round(max(duration * 0.6, duration - 3.2), 3)
    if guess:
        make_guess_png(out / "guess.png", W)
        make_answer_png(out / "answer.png", title, artist, W, palette)
    reveal_extra = {}
    if a.style == "reveal":
        flight = 0.9
        emitters = [n for n in audio_notes if n.start + offset < reveal_time - flight]
        if not emitters:
            log("ERREUR : aucune note avant la révélation")
            return 1
        kb_top = H * 0.655
        pts, artist_y = make_reveal_assets(out, title, artist, W, int(H * 0.215), int(kb_top - 130),
                                           palette, 950, rng)
        per_note = max(1, int(np.ceil(len(pts) / len(emitters))))
        noise = np.clip(rng.normal(0, 1, (len(pts), 2)), -2.5, 2.5)
        with open(out / "particles.txt", "w", encoding="utf-8") as f:
            for i, (x, y) in enumerate(pts):
                n = emitters[min(len(emitters) - 1, i // per_note)]
                f.write(f"{n.start + offset:.4f} {n.hand} {n.pitch} {x:.1f} {y:.1f} "
                        f"{noise[i, 0]:.3f} {noise[i, 1]:.3f}\n")
        reveal_extra = {"flight": flight, "region_top": int(H * 0.215), "artist_y": int(artist_y),
                        "particles": len(pts)}
        log(f"Révélation : {len(pts)} particules lancées par {len(emitters)} notes ({per_note}/note)")
    make_background(out / "background.png", res.get("bg_image") or meta.get("bg_image"), (W, H), palette)

    with open(out / "notes.txt", "w", encoding="utf-8") as f:
        for n in visual_notes:
            f.write(f"{n.start + offset:.4f} {n.end + offset:.4f} {n.pitch} {n.velocity} {n.hand}\n")

    scene = {
        "style": a.style,
        "guess": 1 if guess else 0,
        "reveal_time": reveal_time,
        "layout": a.format,
        "duration": duration,
        "lookahead": fmt["lookahead"],
        "key_lo": lo,
        "key_hi": hi,
        "color_right": ",".join(map(str, palette[0])),
        "color_left": ",".join(map(str, palette[1])),
        "fade_out": 0.35 if a.excerpt == "auto" else 1.2,
        # portrait : titre permanent (on veut savoir quelle chanson c'est) ;
        # paysage : titre au début seulement (la miniature porte le grand titre)
        "title_hold": 0 if a.format == "portrait" else 8,
        "notes": len(visual_notes),
        **reveal_extra,
    }
    with open(out / "scene.txt", "w", encoding="utf-8") as f:
        for k, v in scene.items():
            f.write(f"{k}={v}\n")

    with open(out / "video.json", "w", encoding="utf-8") as f:
        json.dump({"width": W, "height": H, "fps": a.fps, "duration_s": duration}, f)

    # moment le plus dense (pour la miniature)
    starts = np.array([n.start + offset for n in audio_notes])
    t_min = 1.0 if a.format == "portrait" else min(10.0, duration / 2)   # après la disparition du titre
    t_max = (reveal_time - 0.6) if guess else duration - 1.0      # jamais la réponse en miniature
    grid = np.arange(t_min, max(t_min + 0.5, t_max), 0.25)
    dens = [np.sum((starts > t) & (starts < t + fmt["lookahead"])) for t in grid]
    thumb_t = float(grid[int(np.argmax(dens))]) if len(grid) else duration / 2

    with open(out / "meta.json", "w", encoding="utf-8") as f:
        json.dump({"title": title, "artist": artist, "format": a.format, "style": a.style, "guess": guess,
                   "excerpt": [s0, s1], "reason": reason,
                   "duration": duration, "thumb_time": thumb_t, "palette": palette,
                   "keys": [lo, hi], "soundfont": soundfont.name, **song.info}, f, ensure_ascii=False, indent=2)

    log(f"Préparation terminée en {time.time() - t_start:.1f} s (clavier {lo}-{hi}, {len(visual_notes)} notes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
