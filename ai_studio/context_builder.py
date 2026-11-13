"""
Construit le PRÉFIXE STABLE envoyé à Claude (mis en cache 1 h) :
contrat technique du profil + en-têtes du runtime + cahier des charges de la chaîne + codes
d'exemple + images de référence. L'ordre et le contenu sont déterministes pour que le cache serve.
"""
import base64
import io
from pathlib import Path

from studio_common import PROFILES_DIR, channel_dir

IMAGE_MAX_SIDE = 1280
IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp"}
CODE_EXTS = {".cpp", ".h", ".hpp", ".c", ".pde", ".py", ".java"}


def image_block(path_or_bytes, max_side=IMAGE_MAX_SIDE, quality=85):
    """Bloc image base64 (JPEG, plus grand côté <= max_side)."""
    from PIL import Image

    if isinstance(path_or_bytes, (bytes, bytearray)):
        img = Image.open(io.BytesIO(path_or_bytes))
    else:
        img = Image.open(path_or_bytes)
    img = img.convert("RGB")
    img.thumbnail((max_side, max_side))
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=quality)
    return {
        "type": "image",
        "source": {"type": "base64", "media_type": "image/jpeg",
                   "data": base64.standard_b64encode(buf.getvalue()).decode("ascii")},
    }


def text_block(text):
    return {"type": "text", "text": text}


def _read(path):
    return Path(path).read_text(encoding="utf-8", errors="replace")


def build_stable_blocks(channel, profile):
    """Retourne (blocs, résumé) pour une chaîne et un profil technique."""
    blocks = []
    profile_dir = PROFILES_DIR / profile
    ch_dir = channel_dir(channel)
    brief_dir = ch_dir / "brief"

    # 1. Contrat technique + en-têtes exacts du runtime
    parts = [f"# CONTRAT TECHNIQUE (profil {profile})\n\n" + _read(profile_dir / "contract.md")]
    runtime_dir = profile_dir / "runtime"
    for header in sorted(runtime_dir.glob("*.h")):
        parts.append(f"## En-tête fourni : {header.name}\n```cpp\n{_read(header)}\n```")
    blocks.append(text_block("\n\n".join(parts)))

    # 2. Cahier des charges de la chaîne
    brief = brief_dir / "brief.md"
    if not brief.exists():
        raise RuntimeError(f"Cahier des charges introuvable : {brief}")
    blocks.append(text_block(f"# CAHIER DES CHARGES DE LA CHAÎNE « {channel} »\n\n" + _read(brief)))

    # 3. Codes d'exemple
    examples = sorted(p for p in (brief_dir / "examples").glob("*") if p.suffix.lower() in CODE_EXTS)
    if examples:
        parts = ["# CODES D'EXEMPLE — RÉFÉRENCE DE QUALITÉ TECHNIQUE UNIQUEMENT\n"
                 "Ces programmes autonomes montrent le niveau de soin attendu (netteté 4K, physique propre, "
                 "réglages Box2D). Leurs THÈMES (billes, usines, arc-en-ciel) ne sont PAS une consigne : chaque "
                 "création part d'une feuille blanche. Ne reprends pas non plus leur structure (main, fenêtre, ffmpeg)."]
        for p in examples:
            parts.append(f"## examples/{p.name}\n```cpp\n{_read(p)}\n```")
        blocks.append(text_block("\n\n".join(parts)))

    # 4. Images de référence
    images = sorted(p for p in (brief_dir / "images").glob("*") if p.suffix.lower() in IMAGE_EXTS)
    if images:
        blocks.append(text_block("# IMAGES DE RÉFÉRENCE — niveau de finition visé uniquement (netteté, lisibilité, "
                                 "densité). Ce sont des vidéos d'AUTRES chaînes : ne copie ni leurs thèmes, ni leurs "
                                 "couleurs, ni leurs machines."))
        for p in images:
            blocks.append(text_block(f"Image : images/{p.name}"))
            blocks.append(image_block(p))

    chars = sum(len(b.get("text", "")) for b in blocks)
    summary = {
        "examples": [p.name for p in examples],
        "images": [p.name for p in images],
        "text_chars": chars,
        "approx_tokens": chars // 3 + len(images) * 1300,
    }
    return blocks, summary
