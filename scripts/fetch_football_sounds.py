"""
fetch_football_sounds.py — Construit la bibliothèque de sons du moteur foot (assets/sounds/football/).

Tous les sons viennent de Freesound et sont sous licence CC0 (domaine public : aucune mention obligatoire).
Le script télécharge l'aperçu haute qualité de chaque son, le découpe, le normalise et crée des variantes
de hauteur (pour que des rebonds répétés ne sonnent pas tous pareil).

Usage (depuis la racine du projet) :
    python scripts/fetch_football_sounds.py [--cache <dossier>]

Résultat :
    assets/sounds/football/<nom>.wav        effets courts (mono 44,1 kHz), déclenchés par les scènes
    assets/sounds/football/beds/<nom>.ogg   ambiances de stade (stéréo), mixées en fond par finalize.py
    assets/sounds/football/SOURCES.md       provenance de chaque son
"""
import argparse
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]
OUT = BASE_DIR / "assets" / "sounds" / "football"

# nom -> (id Freesound, début s, fin s (None = jusqu'au bout), fondu de sortie s, variantes de hauteur)
SFX = {
    # --- Foule ---
    "crowd_roar":      (528799, 3.0, 11.0, 3.0, None),
    "crowd_goal":      (241630, 0.3, 6.0, 2.0, None),
    "crowd_cheer":     (397434, 0.0, 9.0, 3.0, None),
    "crowd_cheer_small": (365132, 0.2, 6.0, 2.0, None),
    "crowd_ooh":       (619007, 0.35, 2.6, 0.8, None),
    # --- Arbitre ---
    "whistle_short":   (538422, 0.02, 0.50, 0.08, None),
    "whistle_long":    (218318, 0.08, 1.75, 0.25, None),
    # --- Ballon / rebonds ---
    "kick":            (261267, 0.04, 0.50, 0.10, [0.88, 1.0, 1.12]),
    "kick_hard":       (555042, 0.03, 0.30, 0.05, [0.9, 1.05]),
    "bounce":          (505628, 0.03, 0.20, 0.04, [0.85, 1.0, 1.15, 1.3]),
    "bounce_soft":     (319785, 0.0, 0.30, 0.08, [0.9, 1.1]),
    # --- Coups / chocs ---
    "hit":             (348242, 0.0, 0.30, 0.06, [0.9, 1.0, 1.1]),
    "hit_heavy":       (348243, 0.0, 0.48, 0.10, [0.85, 1.0]),
    "hit_impact":      (369711, 0.0, 0.43, 0.10, None),
    "hit_soft":        (458867, 0.03, 0.30, 0.05, [0.9, 1.1]),
    "clang":           (406562, 0.0, 0.40, 0.10, None),
    # --- Explosions / éliminations ---
    "explode":         (435695, 0.05, 1.10, 0.30, [0.9, 1.05]),
    "explode_fire":    (431174, 0.0, 1.00, 0.30, None),
    "explode_big":     (587184, 0.0, 2.26, 0.80, None),
    "impact_bass":     (512389, 0.04, 2.80, 0.80, None),
    # --- Verre (anneaux) ---
    "glass":           (244238, 0.15, 1.60, 0.40, [0.95, 1.1]),
    "glass_break":     (221528, 0.30, 0.95, 0.20, [0.9, 1.1]),
    "glass_small":     (267889, 0.0, 0.80, 0.25, [1.0, 1.2]),
    # --- Pops / confettis ---
    "pop":             (202230, 0.17, 0.45, 0.05, [0.9, 1.0, 1.15, 1.3]),
    "confetti":        (458398, 0.0, 0.35, 0.08, None),
    "plop":            (447910, 0.0, 0.22, 0.05, [0.9, 1.1]),
    # --- Transitions ---
    "whoosh":          (60013, 0.0, 0.43, 0.08, [0.9, 1.1]),
    "whoosh_long":     (683096, 0.15, 1.55, 0.30, None),
    "whoosh_low":      (243400, 0.40, 1.15, 0.15, None),
    "whoosh_heavy":    (877977, 0.10, 2.20, 0.50, None),
    # --- Bonus ---
    "coin":            (146723, 0.0, None, 0.03, [1.0, 1.15]),
    "pickup":          (253172, 0.0, 0.32, 0.05, None),
    "powerup":         (368651, 0.0, 1.07, 0.30, None),
    "powerup_space":   (220173, 0.0, 0.80, 0.20, None),
    "freeze":          (683180, 0.0, 0.81, 0.25, None),
    "ice_break":       (719973, 0.02, 0.50, 0.10, None),
    # --- Course de billes ---
    "marble":          (240313, 0.0, 0.24, 0.05, [0.8, 1.0, 1.2, 1.4]),
    "marble_roll":     (334222, 0.02, 0.40, 0.12, None),
    # --- Peinture (Color War) ---
    "splat":           (445109, 0.0, 0.34, 0.06, [0.9, 1.1]),
    "splat_small":     (232135, 0.0, 0.13, 0.03, [0.9, 1.1, 1.25]),
    "splat_big":       (87535, 0.04, 0.45, 0.10, None),
    # --- Tension / victoire ---
    "drumroll":        (191718, 0.0, None, 0.30, None),
    "airhorn":         (414208, 0.0, 1.61, 0.20, None),
    "airhorn_hype":    (581334, 0.0, 2.75, 0.40, None),
    "tick":            (534094, 0.02, 0.16, 0.03, [1.0, 1.25]),
    "fanfare":         (456966, 0.0, 3.40, 0.60, None),
    "victory":         (249524, 0.0, 1.15, 0.20, None),
}

# Ambiances de stade (fond sonore continu, en stéréo)
BEDS = {
    "stadium_norway": (206003, 0.0, None),
    "stadium_schalke": (63587, 0.0, None),
    "stadium_mexico": (241630, 7.0, None),
}


def run(cmd):
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"ffmpeg a échoué : {' '.join(cmd)[:200]}\n{res.stderr[-600:]}")


def download(sid, cache):
    dst = cache / f"{sid}.mp3"
    if dst.exists() and dst.stat().st_size > 0:
        return dst
    req = urllib.request.Request(f"https://freesound.org/s/{sid}/", headers={"User-Agent": "Mozilla/5.0"})
    html = urllib.request.urlopen(req, timeout=30).read().decode("utf-8", "replace")
    if "publicdomain/zero" not in html:
        sys.exit(f"Le son {sid} n'est plus en CC0 : abandon")
    url = re.search(r'data-mp3="([^"]+)"', html).group(1).replace("-lq.mp3", "-hq.mp3")
    urllib.request.urlretrieve(url, dst)
    return dst


def build_sfx(name, src, start, end, fade, rate):
    """Découpe + fondu + variante de hauteur + normalisation crête -1 dB -> wav mono 44,1 kHz."""
    suffix = "" if rate is None else f"_{rate_index[name].index(rate) + 1}"
    dst = OUT / f"{name}{suffix}.wav"
    dur = None if end is None else end - start
    filt = []
    if rate and rate != 1.0:
        filt.append(f"asetrate=44100*{rate},aresample=44100")
    if fade:
        # fondu calculé sur la durée réelle après changement de hauteur
        real = (dur if dur else 0) / (rate or 1.0)
        if real > 0:
            filt.append(f"afade=t=out:st={max(0.0, real - fade / (rate or 1.0)):.3f}:d={fade / (rate or 1.0):.3f}")
    filt.append("afade=t=in:d=0.004")
    tmp = OUT / f"_{dst.stem}_tmp.wav"
    cmd = ["ffmpeg", "-y", "-v", "error", "-ss", f"{start}"] + (["-t", f"{dur}"] if dur else []) + \
          ["-i", str(src), "-ac", "1", "-ar", "44100", "-af", ",".join(filt), str(tmp)]
    run(cmd)
    # normalisation crête à -1 dBFS
    vol = subprocess.run(["ffmpeg", "-v", "info", "-i", str(tmp), "-af", "volumedetect", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    peak = float(re.search(r"max_volume: (-?[\d.]+) dB", vol).group(1))
    run(["ffmpeg", "-y", "-v", "error", "-i", str(tmp), "-af", f"volume={-1.0 - peak:.2f}dB",
         "-c:a", "pcm_s16le", str(dst)])
    tmp.unlink()
    return dst


def build_bed(name, src, start, end):
    dst = OUT / "beds" / f"{name}.ogg"
    cmd = ["ffmpeg", "-y", "-v", "error", "-ss", f"{start}"] + (["-to", f"{end}"] if end else []) + \
          ["-i", str(src), "-ac", "2", "-ar", "44100",
           "-af", "highpass=f=60,loudnorm=I=-23:TP=-3:LRA=11,afade=t=in:d=1",
           "-c:a", "libvorbis", "-q:a", "4", str(dst)]
    run(cmd)
    return dst


rate_index = {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", help="dossier de téléchargement (défaut : dossier temporaire)")
    a = ap.parse_args()
    cache = Path(a.cache) if a.cache else Path(tempfile.mkdtemp(prefix="fb_sounds_"))
    cache.mkdir(parents=True, exist_ok=True)
    (OUT / "beds").mkdir(parents=True, exist_ok=True)

    sources = []
    for name, (sid, start, end, fade, rates) in SFX.items():
        src = download(sid, cache)
        rate_index[name] = rates or []
        for r in (rates or [None]):
            build_sfx(name, src, start, end, fade, r)
        sources.append((name, sid))
        print(f"  [OK] {name} ({len(rates or [None])} fichier(s))")

    # Coup de sifflet final : deux coups courts puis un long
    short, long_ = OUT / "whistle_short.wav", OUT / "whistle_long.wav"
    run(["ffmpeg", "-y", "-v", "error", "-i", str(short), "-i", str(short), "-i", str(long_), "-filter_complex",
         "[0]apad=pad_dur=0.12[a];[1]apad=pad_dur=0.12[b];[a][b][2]concat=n=3:v=0:a=1",
         "-c:a", "pcm_s16le", str(OUT / "whistle_final.wav")])
    print("  [OK] whistle_final")

    for name, (sid, start, end) in BEDS.items():
        build_bed(name, download(sid, cache), start, end)
        sources.append((f"beds/{name}", sid))
        print(f"  [OK] ambiance {name}")

    lines = ["# Sons du moteur foot", "",
             "Tous les sons proviennent de [Freesound](https://freesound.org) sous licence **CC0 1.0** "
             "(domaine public, aucune attribution requise). Générés par `scripts/fetch_football_sounds.py`.", "",
             "| Fichier | Son Freesound |", "|---|---|"]
    lines += [f"| `{n}` | https://freesound.org/s/{sid}/ |" for n, sid in sources]
    (OUT / "SOURCES.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\nBibliothèque prête : {OUT}")


if __name__ == "__main__":
    main()
