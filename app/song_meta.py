"""
song_meta.py — Titre / artiste d'un morceau à partir du nom du fichier MIDI, et remplacement des
repères dans les titres, descriptions, hashtags et tags (référencement YouTube / TikTok).

Avec la bibliothèque (assets/library, app/midi_library.py), titre et artiste viennent du catalogue ;
parse_song_name() sert de secours pour un fichier "Artiste - Titre[ - EASY].mid" (convention de la bibliothèque).

Repères disponibles dans les configs d'animation (metadata) :
  {midi}       "Titre - Artiste" (nom propre, sans EASY ni (2))
  {title}      titre seul
  {artist}     artiste seul
  {artist_tag} artiste sans espaces ni ponctuation (pour un hashtag : "#{artist_tag}")
  {easy}       "Easy " si le fichier est une version facile, sinon vide
"""
import re


def parse_song_name(stem, midi_key="midi2"):
    s = re.sub(r"\s*\(\d+\)\s*$", "", stem)                       # "(2)" des doublons
    s = re.sub(r"\\[a-z]+(\[[^\]]*\])?(\{[^}]*\})?", "", s)        # restes LaTeX
    s = re.sub(r"\s+", " ", s).strip()
    parts = [p.strip() for p in s.split(" - ") if p.strip()]
    easy = any(p.upper() == "EASY" for p in parts)
    parts = [p for p in parts if p.upper() not in ("EASY", "PIANO EDITION")]
    if not parts:
        return {"title": s, "artist": "", "easy": easy}
    if len(parts) == 1:
        return {"title": parts[0], "artist": "", "easy": easy}
    if midi_key == "midi2":
        return {"title": " - ".join(parts[1:]), "artist": parts[0], "easy": easy}
    return {"title": parts[0], "artist": " - ".join(parts[1:]), "easy": easy}


def placeholders(song):
    title, artist = song.get("title", ""), song.get("artist", "")
    return {
        "midi": f"{title} - {artist}" if artist else title,
        "title": title,
        "artist": artist,
        "artist_tag": re.sub(r"[^0-9A-Za-zÀ-ÖØ-öø-ÿ]", "", artist),
        "easy": "Easy " if song.get("easy") else "",
    }


def _tidy(text):
    """Nettoie les restes quand un repère est vide (artiste inconnu, version non facile...)."""
    text = re.sub(r"\(\s*\)", "", text)
    text = re.sub(r"[ \t]+-[ \t]+(?=\||🎹|$|\n)", " ", text, flags=re.M)   # "Titre -  | Piano" -> "Titre | Piano"
    text = re.sub(r"\bby\s+(?=[—\-|,.\n])", "", text)                     # "by  —" sans artiste
    text = re.sub(r"(?<!\S)#(?=\s|$)", "", text)                          # hashtag vide ("#{artist_tag}" sans artiste)
    text = re.sub(r"[ \t]{2,}", " ", text)
    return "\n".join(line.strip() for line in text.split("\n")).strip()


def fill(text, song):
    for k, v in placeholders(song).items():
        text = text.replace("{" + k + "}", v)
    return _tidy(text)


def fill_list(items, song, prefix=""):
    """Remplace dans une liste (tags/hashtags), retire les vides et les doublons (sans tenir compte de la casse)."""
    out, seen = [], set()
    for it in items:
        v = _tidy(fill(it, song))
        if not v or v == prefix or v.lower() in seen:
            continue
        seen.add(v.lower())
        out.append(v)
    return out
