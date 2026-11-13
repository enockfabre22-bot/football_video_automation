"""Outils vidéo du studio (ffprobe / ffmpeg)."""
import json
import subprocess
from pathlib import Path


def probe(video):
    res = subprocess.run(
        ["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(video)],
        capture_output=True, text=True,
    )
    if res.returncode != 0:
        return None
    return json.loads(res.stdout)


def check_final_video(video, expected, audio_required):
    """Retourne la liste des problèmes (vide si la vidéo est conforme)."""
    video = Path(video)
    if not video.exists() or video.stat().st_size == 0:
        return ["vidéo finale absente ou vide"]
    info = probe(video)
    if not info:
        return ["ffprobe ne peut pas lire la vidéo finale"]
    problems = []
    vstreams = [s for s in info.get("streams", []) if s.get("codec_type") == "video"]
    astreams = [s for s in info.get("streams", []) if s.get("codec_type") == "audio"]
    if not vstreams:
        return ["aucun flux vidéo"]
    v = vstreams[0]
    if (v.get("width"), v.get("height")) != (expected["width"], expected["height"]):
        problems.append(f"résolution {v.get('width')}x{v.get('height')} au lieu de {expected['width']}x{expected['height']}")
    duration = float(info.get("format", {}).get("duration", 0))
    if abs(duration - float(expected["duration_s"])) > 1.0:
        problems.append(f"durée {duration:.1f} s au lieu de {expected['duration_s']} s")
    if audio_required and not astreams:
        problems.append("aucune piste audio")
    return problems


def frame_appeal(path):
    """Note d'attrait d'une image pour une miniature : couleurs vives + contraste, sans être trop sombre."""
    from PIL import Image, ImageStat
    with Image.open(path) as img:
        rgb = img.convert("RGB").resize((320, 180))
        pixels = list(rgb.getdata())
        light = ImageStat.Stat(rgb.convert("L"))
    n = len(pixels)
    rg = [r - g for r, g, b in pixels]
    yb = [0.5 * (r + g) - b for r, g, b in pixels]

    def mean_std(values):
        m = sum(values) / n
        return m, (sum((v - m) ** 2 for v in values) / n) ** 0.5

    m_rg, s_rg = mean_std(rg)
    m_yb, s_yb = mean_std(yb)
    colorfulness = (s_rg ** 2 + s_yb ** 2) ** 0.5 + 0.3 * (m_rg ** 2 + m_yb ** 2) ** 0.5   # Hasler & Süsstrunk
    brightness, contrast = light.mean[0], light.stddev[0]
    penalty = max(0.0, 60 - brightness) * 1.5 + max(0.0, brightness - 225) * 1.5
    return colorfulness + 0.6 * contrast - penalty


def best_thumbnail(video, out_path, work_dir, width=1280, candidates=12):
    """Choisit parmi plusieurs images de la vidéo la plus attrayante (couleurs, contraste, lumière)."""
    fractions = [0.25 + 0.72 * i / (candidates - 1) for i in range(candidates)]
    frames = extract_frames(video, fractions, work_dir, prefix="thumbcand", width=width)
    if not frames:
        return None
    scored = sorted(((frame_appeal(p), p, t) for p, t in frames), key=lambda x: x[0], reverse=True)
    best = scored[0]
    Path(best[1]).replace(out_path)
    for _, p, _ in scored[1:]:
        Path(p).unlink(missing_ok=True)
    return best[2]


def extract_frames(video, fractions, out_dir, prefix="final", width=1280):
    """Extrait des images JPEG aux fractions de durée demandées. Retourne [(chemin, secondes)]."""
    info = probe(video) or {}
    duration = float(info.get("format", {}).get("duration", 0) or 0)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    result = []
    for i, frac in enumerate(fractions):
        t = max(0.0, min(duration - 0.1, duration * frac))
        out = out_dir / f"{prefix}_{i}.jpg"
        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error", "-ss", f"{t:.2f}", "-i", str(video),
             "-frames:v", "1", "-vf", f"scale='min({width},iw)':-2", "-q:v", "3", str(out)],
            capture_output=True,
        )
        if out.exists():
            result.append((out, t))
    return result
