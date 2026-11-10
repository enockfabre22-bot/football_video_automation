"""Chemins, configuration et petits utilitaires partagés par le studio IA."""
import json
import os
import sys
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
    os.replace(tmp, path)


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
    ai.setdefault("stock_target", 2)
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
