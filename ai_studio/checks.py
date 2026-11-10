"""
Contrôles automatiques de l'aperçu (sans IA) à partir de metrics.json et des images capturées.
- échecs bloquants -> correction du code (Claude) sans analyse visuelle ;
- avertissements -> transmis à la revue visuelle.
"""
from pathlib import Path

from studio_common import read_json

FROZEN_MOVING_FRACTION = 0.01   # moins de 1 % des corps en mouvement...
FROZEN_MOVING_COUNT = 10        # ...et moins de 10 corps en mouvement = image figée
BLANK_STDDEV = 3.0              # écart-type des pixels sous lequel une image est considérée vide


def capture_frames(total_frames, fps):
    """Images capturées pour l'aperçu : début, 3 s, 10/25/50/75/90 %, fin."""
    frames = {0, min(3 * fps, total_frames - 1)}
    for pct in (0.10, 0.25, 0.50, 0.75, 0.90):
        frames.add(int(total_frames * pct))
    frames.add(total_frames - 1)
    return sorted(f for f in frames if 0 <= f < total_frames)


def _image_stddev(path):
    from PIL import Image, ImageStat
    with Image.open(path) as img:
        stat = ImageStat.Stat(img.convert("L"))
        return stat.stddev[0]


def analyze_preview(out_dir, max_bodies):
    out_dir = Path(out_dir)
    failures, warnings = [], []
    m = read_json(out_dir / "metrics.json")
    if not m:
        return ["metrics.json absent ou illisible : le programme ne s'est pas terminé normalement."], [], {}

    w = m.get("watch", {})
    if not w.get("enabled"):
        failures.append("La surveillance n'est pas activée : appelle anim_watch_world(ctx, world, toScreen) "
                        "dans init() juste après la création du monde.")
    else:
        if w.get("max_invalid", 0) > 0:
            failures.append(f"Positions/vitesses invalides (NaN) détectées dès t={w.get('first_invalid_time')} s : "
                            "simulation instable (corps superposés, forces énormes, division par zéro).")
        if w.get("max_lost", 0) > 0:
            failures.append(f"{w['max_lost']} corps perdus (très loin de l'écran) dès t={w.get('first_lost_time')} s : "
                            "des objets sortent du circuit ou traversent les murs.")
        if w.get("max_bodies", 0) > max_bodies:
            failures.append(f"{w['max_bodies']} corps dynamiques simultanés, limite {max_bodies}.")
        if w.get("max_bodies", 0) == 0:
            failures.append("Aucun corps dynamique : rien ne bouge physiquement.")
        moving = w.get("moving_fraction_by_decile", [])
        moving_max = w.get("moving_max_by_decile", [0] * 10)
        bodies_dec = w.get("bodies_by_decile", [0] * 10)
        frozen = [i for i, v in enumerate(moving[:8])
                  if v < FROZEN_MOVING_FRACTION and moving_max[i] < FROZEN_MOVING_COUNT and bodies_dec[i] > 0]
        if frozen:
            failures.append("Animation figée (moins de 1 % des corps en mouvement) pendant les tranches "
                            + ", ".join(f"{i * 10}-{i * 10 + 10} %" for i in frozen)
                            + " de la vidéo : machine bloquée ou simulation terminée trop tôt.")
        elif len(moving) == 10 and max(moving[8:]) < FROZEN_MOVING_FRACTION:
            warnings.append("Plus rien ne bouge dans les 20 derniers % de la vidéo (fin statique).")
        bodies = w.get("bodies_by_decile", [])
        off = w.get("offscreen_by_decile", [])
        if bodies and off and bodies[-1] and off[-1] > 0.25 * bodies[-1]:
            warnings.append(f"{off[-1]} corps sur {bodies[-1]} sont hors écran à la fin.")

    frames = sorted(out_dir.glob("frame_*.png"))
    if not frames:
        failures.append("Aucune image d'aperçu capturée.")
    for p in frames[1:]:
        if _image_stddev(p) < BLANK_STDDEV:
            failures.append(f"Image {p.name} presque uniforme (écran vide ?).")
            break
    return failures, warnings, m


def metrics_summary(m):
    if not m:
        return "(aucune mesure)"
    w = m.get("watch", {})
    lines = [
        f"durée simulée {m.get('duration')} s à {m.get('fps')} fps, calcul de l'aperçu {m.get('render_seconds', 0):.1f} s",
        f"corps dynamiques max {w.get('max_bodies')}, hors écran max {w.get('max_offscreen')}, perdus {w.get('max_lost')}, invalides {w.get('max_invalid')}",
        f"corps par dixième : {w.get('bodies_by_decile')}",
        f"part en mouvement par dixième : {w.get('moving_fraction_by_decile')}",
        f"nombre max en mouvement par dixième : {w.get('moving_max_by_decile')}",
        f"hors écran par dixième : {w.get('offscreen_by_decile')}",
    ]
    if m.get("scene"):
        lines.append(f"mesures de la scène : {m['scene']}")
    return "\n".join(lines)
