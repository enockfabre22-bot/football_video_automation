"""
Calendrier des événements (Noël, Halloween, Nouvel An…) pour les créations à thème.

Claude ne connaît pas la date du jour : c'est l'application qui décide qu'un événement approche
et qui le lui indique. Une création à thème reçoit une fenêtre de publication (publish_after /
publish_before) respectée par le stock : une vidéo de Noël ne sort jamais en janvier.

Événements fixes ci-dessous + événements de la chaîne dans ai_channels/<chaîne>/events.json :
  [{"name": "Finale de la Coupe du monde", "publish_after": "2026-07-10", "publish_before": "2026-07-20",
    "ideas": "ballons, couleurs des drapeaux, tournoi de billes"}]
"""
import random
from datetime import date, timedelta

from studio_common import channel_dir, read_json


def _easter(year):
    a, b, c = year % 19, year // 100, year % 100
    d, e = b // 4, b % 4
    f = (b + 8) // 25
    g = (b - f + 1) // 3
    h = (19 * a + b - d - g + 15) % 30
    i, k = c // 4, c % 4
    l = (32 + 2 * e + 2 * i - h - k) % 7
    m = (a + 11 * h + 22 * l) // 451
    month = (h + l - 7 * m + 114) // 31
    day = ((h + l - 7 * m + 114) % 31) + 1
    return date(year, month, day)


def _fixed_events(year):
    easter = _easter(year)
    return [
        {"name": "Saint-Valentin", "publish_after": date(year, 2, 7), "publish_before": date(year, 2, 14),
         "ideas": "cœurs, rose / rouge / blanc, deux flux de billes qui se rejoignent"},
        {"name": "Pâques", "publish_after": easter - timedelta(days=7), "publish_before": easter + timedelta(days=1),
         "ideas": "œufs colorés, pastels, œufs qui se remplissent de billes"},
        {"name": "Halloween", "publish_after": date(year, 10, 20), "publish_before": date(year, 10, 31),
         "ideas": "citrouille qui se remplit, orange / violet / noir, toiles d'araignée en clous"},
        {"name": "Noël", "publish_after": date(year, 12, 1), "publish_before": date(year, 12, 25),
         "ideas": "sapin qui se remplit de billes, boules de Noël, flocons, rouge / vert / or / blanc"},
        {"name": "Nouvel An", "publish_after": date(year, 12, 26), "publish_before": date(year + 1, 1, 1),
         "ideas": f"chiffres {year + 1} qui se remplissent, feux d'artifice de billes, or / argent"},
    ]


def _parse(d):
    return d if isinstance(d, date) else date.fromisoformat(str(d))


def all_events(channel, today=None):
    today = today or date.today()
    events = []
    for y in (today.year, today.year + 1):
        events += _fixed_events(y)
    for e in read_json(channel_dir(channel) / "events.json", []) or []:
        try:
            events.append({**e, "publish_after": _parse(e["publish_after"]),
                           "publish_before": _parse(e["publish_before"])})
        except (KeyError, ValueError):
            print(f"[STUDIO IA] Événement ignoré (dates invalides) : {e}")
    return events


def upcoming_events(channel, today=None, lead_days=21):
    """Événements dont la fenêtre de publication est ouverte ou commence dans moins de lead_days."""
    today = today or date.today()
    out = []
    for e in all_events(channel, today):
        if e["publish_before"] < today:
            continue
        if e["publish_after"] - today <= timedelta(days=lead_days):
            out.append(e)
    out.sort(key=lambda e: e["publish_after"])
    return out


def choose_event(channel, animation, events_cfg, history, today=None):
    """Décide si la prochaine création sera à thème. Retourne l'événement (dict) ou None."""
    if not events_cfg.get("enabled", True):
        return None
    candidates = [e for e in upcoming_events(channel, today, int(events_cfg.get("lead_days", 21)))
                  if history.event_count(animation, e["name"]) < int(events_cfg.get("max_per_event", 2))]
    if not candidates or random.random() > float(events_cfg.get("probability", 0.5)):
        return None
    return candidates[0]


def event_to_state(e):
    if not e:
        return None
    return {"name": e["name"], "ideas": e.get("ideas", ""),
            "publish_after": e["publish_after"].isoformat(), "publish_before": e["publish_before"].isoformat()}
