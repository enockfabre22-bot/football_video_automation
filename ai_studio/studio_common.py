"""Chemins, configuration et petits utilitaires partagés par le studio IA."""
import json
import os
import sys
import time
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
APP_DIR = BASE_DIR / "app"
if str(APP_DIR) not in sys.path:
    sys.path.insert(0, str(APP_DIR))   # sandbox_runner, ai_stock, render_lock, animation_manager...

CONFIG_DIR = BASE_DIR / "config"
CHANNELS_DIR = BASE_DIR / "ai_channels"
PROFILES_DIR = BASE_DIR / "sandbox" / "profiles"


def read_json(path, default=None):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return default


def write_json_atomic(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    for attempt in range(10):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:   # Windows : fichier ouvert un instant par l'antivirus / l'indexation
            if attempt == 9:
                raise
            time.sleep(0.2)


def now_iso():
    return datetime.now().isoformat(timespec="seconds")


def load_ai_config():
    cfg = read_json(CONFIG_DIR / "ai.json")
    if cfg is None:
        raise RuntimeError("config/ai.json introuvable ou invalide")
    return cfg


def ai_data_dir():
    paths = read_json(CONFIG_DIR / "paths.json", {})
    return BASE_DIR / paths.get("ai_data", "ai_data/")


def animations_dir():
    paths = read_json(CONFIG_DIR / "paths.json", {})
    return BASE_DIR / paths.get("animations", "animations/")


def list_ai_animations():
    """Toutes les animations de type "ai" (activées ou non), avec leur dossier."""
    result = []
    for folder in sorted(animations_dir().iterdir()):
        cfg = read_json(folder / "config.json") if folder.is_dir() else None
        if cfg and cfg.get("type") == "ai":
            cfg["folder_path"] = folder
            result.append(cfg)
    return result


def load_ai_animation(name):
    for cfg in list_ai_animations():
        if cfg.get("name") == name:
            return cfg
    raise RuntimeError(f"Animation IA '{name}' introuvable (config.json avec \"type\": \"ai\" et \"name\": \"{name}\")")


def ai_settings(anim_cfg):
    """Section "ai" de l'animation, avec les valeurs par défaut."""
    ai = dict(anim_cfg.get("ai", {}))
    ai.setdefault("profile", "raylib_box2d")
    ai.setdefault("max_bodies", 2000)
    ai.setdefault("sound", "music")          # "music" (ajoutée après coup) ou "events" (ctx.sound)
    if not ai.get("channel"):
        raise RuntimeError(f"L'animation '{anim_cfg.get('name')}' n'a pas de \"ai.channel\"")
    return ai


def merged_limits(ai_cfg, anim_cfg):
    limits = dict(ai_cfg.get("limits", {}))
    limits.update(anim_cfg.get("ai", {}).get("limits", {}))
    return limits


def channel_dir(channel):
    return CHANNELS_DIR / channel


def log(msg):
    print(f"[STUDIO IA] {msg}", flush=True)


class StudioBusy(RuntimeError):
    pass


@contextmanager
def studio_lock():
    """Un seul processus studio à la fois (service OU commande manuelle) : évite que deux processus
    reprennent la même création en même temps."""
    from render_lock import _try_lock, _unlock
    path = ai_data_dir() / ".studio.lock"
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(str(path), os.O_RDWR | os.O_CREAT)
    if not _try_lock(fd, False):
        os.close(fd)
        raise StudioBusy("Le studio IA tourne déjà dans un autre processus (service ai-studio ?). "
                         "Arrête-le d'abord : sudo systemctl stop ai-studio")
    try:
        yield
    finally:
        _unlock(fd)
        os.close(fd)


def slots_per_day(anim_cfg):
    """Nombre de créneaux quotidiens qui peuvent consommer cette animation (comptes activés ou non)."""
    name = anim_cfg["name"]
    count = 0
    for account in anim_cfg.get("accounts", []):
        acc = read_json(BASE_DIR / "accounts" / account / "config.json", {}) or {}
        for slot in acc.get("schedule_slots", []):
            strategy = slot.get("animation_strategy", "random")
            if strategy == "fixed" and slot.get("target_animation") == name:
                count += 1
            elif strategy == "random" and name not in slot.get("exclude_animations", []):
                count += 1
    return max(1, count)


def stock_target(anim_cfg):
    """Nombre de vidéos d'avance visé = jours d'avance × créneaux par jour."""
    ai = anim_cfg.get("ai", {})
    if "stock_target" in ai:
        return int(ai["stock_target"])
    return int(ai.get("stock_days", 5)) * slots_per_day(anim_cfg)
