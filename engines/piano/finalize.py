"""
finalize.py — Dernière étape du moteur piano.

Usage : python engines/piano/finalize.py <job.json> [--thumbnail]

Assemble la vidéo muette rendue par la scène raylib (output_video) avec <work_dir>/piano/audio.wav,
et, avec --thumbnail, extrait l'image la plus riche en notes et y pose le titre
-> <work_dir>/thumbnail_source.jpg (reprise automatiquement comme miniature à la publication).
"""
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

BASE_DIR = Path(__file__).resolve().parents[2]
FONT_DIR = BASE_DIR / "assets" / "fonts"


def log(msg):
    print(f"      [PIANO] {msg}", flush=True)


def mux(video, audio, work):
    tmp = work / "piano" / "final_tmp.mp4"
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
           "-i", str(video), "-i", str(audio),
           "-map", "0:v:0", "-map", "1:a:0",
           "-c:v", "copy", "-c:a", "aac", "-b:a", "256k", "-ar", "48000",
           "-shortest", "-movflags", "+faststart", str(tmp)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0 or not tmp.exists() or tmp.stat().st_size == 0:
        log(f"ERREUR ffmpeg (assemblage) : {res.stderr.strip()[-500:]}")
        return False
    tmp.replace(video)
    return True


def thumbnail(video, work, meta):
    frame = work / "piano" / "thumb_frame.png"
    res = subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
                          "-ss", f"{meta.get('thumb_time', 3.0):.2f}", "-i", str(video),
                          "-frames:v", "1", str(frame)], capture_output=True, text=True)
    if res.returncode != 0 or not frame.exists():
        log(f"Miniature impossible : {res.stderr.strip()[-300:]}")
        return
    img = Image.open(frame).convert("RGB")
    W, H = img.size
    if meta.get("format") == "portrait":
        # le titre est déjà dans l'image (affiché en permanence dans la vidéo)
        img.save(work / "thumbnail_source.jpg", quality=92)
        frame.unlink(missing_ok=True)
        log("Miniature générée (image la plus riche)")
        return
    title, artist = meta.get("title", ""), meta.get("artist", "")
    # bandeau sombre dégradé + grand titre (lisible en petit dans les suggestions YouTube)
    band = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    bd = ImageDraw.Draw(band)
    band_h = int(H * 0.42)
    for y in range(band_h):
        bd.line([(0, y), (W, y)], fill=(0, 0, 0, int(210 * (1 - y / band_h) ** 1.3)))
    size = int(H * 0.12)
    font = ImageFont.truetype(str(FONT_DIR / "Poppins-Bold.ttf"), size)
    while bd.textlength(title, font=font) > W * 0.9 and size > 30:
        size -= 4
        font = ImageFont.truetype(str(FONT_DIR / "Poppins-Bold.ttf"), size)
    sub = ImageFont.truetype(str(FONT_DIR / "Poppins-SemiBold.ttf"), int(size * 0.5))

    def text_layer(title_fill, artist_fill):
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        y = int(H * 0.05)
        d.text((W // 2, y), title, font=font, fill=title_fill, anchor="ma")
        if artist:
            d.text((W // 2, y + int(size * 1.15)), artist, font=sub, fill=artist_fill, anchor="ma")
        return layer

    shadow = text_layer((0, 0, 0, 255), (0, 0, 0, 255)).filter(ImageFilter.GaussianBlur(10))
    out = img.convert("RGBA")
    for layer in (band, shadow, text_layer((255, 255, 255, 255), (255, 255, 255, 215))):
        out = Image.alpha_composite(out, layer)
    out.convert("RGB").save(work / "thumbnail_source.jpg", quality=92)
    frame.unlink(missing_ok=True)
    log("Miniature générée (image la plus riche + titre)")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        job = json.load(f)
    work = Path(job["work_dir"])
    video = Path(job["output_video"])
    audio = work / "piano" / "audio.wav"
    if not video.exists() or not audio.exists():
        log(f"ERREUR : vidéo ({video.exists()}) ou audio ({audio.exists()}) manquant")
        return 1
    if not mux(video, audio, work):
        return 1
    log(f"Vidéo finale avec piano : {video.name}")
    meta_path = work / "piano" / "meta.json"
    if "--thumbnail" in sys.argv and meta_path.exists():
        with open(meta_path, "r", encoding="utf-8") as f:
            thumbnail(video, work, json.load(f))
    return 0


if __name__ == "__main__":
    sys.exit(main())
