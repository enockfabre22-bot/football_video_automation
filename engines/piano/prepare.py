"""
prepare.py — Étape 1 du moteur piano (étape "python" d'une animation).

Usage : python engines/piano/prepare.py <job.json> [--format portrait|landscape]
        [--max-seconds 58] [--excerpt auto|full] [--names artist_title|title_artist] [--fps 60]
        [--style falling|premium|neon] [--lead 0.12]

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

# Style neon : couples (main droite, main gauche) saturés, comme les vidéos de référence
NEON_PALETTES = [
    ((255, 60, 80), (100, 245, 90)),     # rouge / vert
    ((255, 70, 200), (60, 220, 255)),    # magenta / cyan
    ((255, 190, 50), (90, 140, 255)),    # or / bleu
    ((60, 220, 255), (190, 110, 255)),   # cyan / violet
]

# Style liquid : (main droite, main gauche, ligne de frappe) — matière lumineuse sur fond noir
LIQUID_PALETTES = [
    ((255, 196, 70), (60, 220, 230), (255, 196, 70)),     # or / cyan
    ((255, 205, 85), (95, 225, 95), (215, 235, 95)),      # or / vert
    ((45, 115, 255), (40, 215, 205), (45, 130, 255)),     # bleu / turquoise
    ((40, 215, 205), (70, 95, 255), (40, 215, 205)),      # turquoise / indigo
    ((255, 95, 185), (255, 200, 90), (255, 95, 185)),     # rose / or
    ((175, 115, 255), (60, 220, 230), (175, 115, 255)),   # violet / cyan
    ((255, 125, 60), (255, 215, 95), (255, 150, 60)),     # orange / or (feu)
]

# Style premium : couleurs plus douces et élégantes (verre lumineux, pas néon criard)
PREMIUM_PALETTES = [
    ((110, 210, 255), (255, 150, 120)),   # ciel / corail
    ((140, 235, 200), (185, 150, 255)),   # menthe / lilas
    ((255, 200, 120), (120, 170, 255)),   # ambre / bleu
    ((120, 225, 255), (255, 130, 190)),   # glace / rose
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


def detect_key_fifths(notes):
    """
    Armure la plus probable : la gamme majeure (de 6 bémols à 7 dièses) qui couvre le plus de
    « poids » (durée x vélocité) des notes. Retourne le nombre de quintes (+ dièses / - bémols).
    """
    w = np.zeros(12)
    for n in notes:
        w[n.pitch % 12] += (n.end - n.start) * n.velocity
    major = [0, 2, 4, 5, 7, 9, 11]
    best, best_score = 0, -1.0
    for k in range(-6, 8):
        tonic = (7 * k) % 12
        score = sum(w[(tonic + d) % 12] for d in major) - 1e-6 * abs(k)
        if score > best_score:
            best, best_score = k, score
    return best


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
    ap.add_argument("--style", default="falling", choices=["falling", "premium", "neon", "liquid"])
    ap.add_argument("--lead", type=float, default=LEAD, help="secondes avant la 1re note")
    a = ap.parse_args()
    lead = a.lead

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
    # les notes à venir continuent de tomber jusqu'à la dernière image (pas d'écran vide)
    visual_notes = [n for n in song.notes if s0 - 0.01 <= n.start < s1 + fmt["lookahead"] + 0.5]
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
    pals = {"premium": PREMIUM_PALETTES, "neon": NEON_PALETTES, "liquid": LIQUID_PALETTES}.get(a.style, PALETTES)
    palette = pals[int(rng.integers(len(pals)))]           # couleurs tirées au hasard à chaque vidéo
    lo, hi = key_range(visual_notes, fmt["min_whites"])
    title, artist = clean_names(meta.get("song_title") or Path(midi_path).stem, a.names)
    make_title_png(out / "title.png", title, artist, a.format, W)
    style_extra = {}
    if a.style == "premium":
        # fond propre (pas de photo floue) + police pour les noms des notes et des touches
        make_background(out / "background.png", None, (W, H), palette)
        shutil.copy2(FONT_DIR / "Poppins-SemiBold.ttf", out / "font.ttf")
        style_extra = {"lookahead": 2.4}
    elif a.style == "neon":
        # fond étoilé dessiné par la scène ; police des noms de notes ; dièses ou bémols selon la tonalité
        shutil.copy2(FONT_DIR / "Poppins-Bold.ttf", out / "font.ttf")
        style_extra = {"lookahead": 2.2, "title_hold": 3.5, "key_fifths": detect_key_fifths(audio_notes)}
    elif a.style == "liquid":
        # fond noir pur (dessiné par la scène) ; titre seul au centre ; niveau selon la densité de notes
        shutil.copy2(FONT_DIR / "Poppins-Bold.ttf", out / "font.ttf")
        shutil.copy2(FONT_DIR / "LibreBaskerville-Regular.ttf", out / "font_serif.ttf")
        nps = len(audio_notes) / max(1.0, s1 - s0)
        easy = bool(re.search(r"(^|\s-\s)EASY$", Path(midi_path).stem, re.I))
        level = "Beginner" if easy or nps < 3.5 else "Intermediate" if nps < 7.0 else "Advanced"
        clean = lambda v: re.sub(r"[\r\n=]", " ", v).strip()
        style_extra = {"lookahead": 2.3 if a.format == "portrait" else 2.2, "key_fifths": detect_key_fifths(audio_notes),
                       "title": clean(title), "level": f"{level} Piano Tutorial",
                       "line_color": ",".join(map(str, palette[2]))}
        log(f"Niveau : {level} ({nps:.1f} notes/s)")
    else:
        make_background(out / "background.png", res.get("bg_image") or meta.get("bg_image"), (W, H), palette)

    with open(out / "notes.txt", "w", encoding="utf-8") as f:
        for n in visual_notes:
            f.write(f"{n.start + offset:.4f} {n.end + offset:.4f} {n.pitch} {n.velocity} {n.hand}\n")

    scene = {
        "style": a.style,
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
        **style_extra,
    }
    with open(out / "scene.txt", "w", encoding="utf-8") as f:
        for k, v in scene.items():
            f.write(f"{k}={v}\n")

    with open(out / "video.json", "w", encoding="utf-8") as f:
        json.dump({"width": W, "height": H, "fps": a.fps, "duration_s": duration}, f)

    # moment le plus dense (pour la miniature)
    starts = np.array([n.start + offset for n in audio_notes])
    t_min = 1.0 if a.format == "portrait" else min(10.0, duration / 2)   # après la disparition du titre
    grid = np.arange(t_min, max(t_min + 0.5, duration - 1.0), 0.25)
    dens = [np.sum((starts > t) & (starts < t + fmt["lookahead"])) for t in grid]
    thumb_t = float(grid[int(np.argmax(dens))]) if len(grid) else duration / 2

    with open(out / "meta.json", "w", encoding="utf-8") as f:
        json.dump({"title": title, "artist": artist, "format": a.format, "style": a.style,
                   "excerpt": [s0, s1], "reason": reason,
                   "duration": duration, "thumb_time": thumb_t, "palette": palette,
                   "keys": [lo, hi], "soundfont": soundfont.name, **song.info}, f, ensure_ascii=False, indent=2)

    log(f"Préparation terminée en {time.time() - t_start:.1f} s (clavier {lo}-{hi}, {len(visual_notes)} notes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
