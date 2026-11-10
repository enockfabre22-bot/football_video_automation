"""Consignes et schémas JSON des tâches du studio (concept, code, correction, revue, fiche)."""
import json

SYSTEM = """Tu es le directeur artistique ET le développeur d'un studio qui crée des vidéos d'animation \
générées par programme pour une chaîne vidéo. Tu conçois des concepts originaux, tu écris le code de \
la scène qui respecte EXACTEMENT le contrat technique fourni, tu corriges les erreurs et tu juges la \
qualité visuelle sur des images d'aperçu.

Règles permanentes :
- Le cahier des charges de la chaîne fait foi. Les exemples et images illustrent des possibilités ; \
ne les recopie pas, invente.
- Qualité d'exécution irréprochable : aucun objet ne traverse un mur, ne reste bloqué, ne tremble, \
ne sort du cadre par erreur ; mouvement fluide ; rendu net en 4K.
- Le code doit compiler du premier coup : n'utilise que les API listées dans le contrat et les \
en-têtes fournis (raylib 5.5, Box2D 2.4.2 API C++, C++17).
- Réponds uniquement avec le JSON demandé."""


# ---------------------------------------------------------------------- #
# Schémas (sorties structurées)
# ---------------------------------------------------------------------- #
def _obj(props, required=None):
    return {"type": "object", "properties": props, "required": required or list(props),
            "additionalProperties": False}


_STR = {"type": "string"}
_STRS = {"type": "array", "items": _STR}

CONCEPT_SCHEMA = _obj({
    "title": _STR,
    "pitch": _STR,
    "scenario": _STRS,
    "mechanisms": _STRS,
    "objects": _STRS,
    "interactions": _STRS,
    "environment": _STR,
    "palette": _STRS,
    "tags": _STRS,
    "estimated_bodies": {"type": "integer"},
    "event": _STR,
})

FILES_SCHEMA = {"type": "array", "items": _obj({"path": _STR, "content": _STR})}
EDITS_SCHEMA = {"type": "array", "items": _obj({"file": _STR, "old": _STR, "new": _STR})}

CODE_SCHEMA = _obj({"files": FILES_SCHEMA, "notes": _STR})
FIX_SCHEMA = _obj({"edits": EDITS_SCHEMA, "files": FILES_SCHEMA, "notes": _STR})
REVIEW_SCHEMA = _obj({
    "approved": {"type": "boolean"},
    "score": {"type": "integer"},
    "issues": _STRS,
    "edits": EDITS_SCHEMA,
    "notes": _STR,
})
def _nullable(schema):
    return {"anyOf": [schema, {"type": "null"}]}


# Schéma UNIQUE pour toutes les tâches Sonnet : le format de sortie fait partie du préfixe mis en
# cache ; un schéma différent par tâche obligeait à réécrire le cache (~95 k tokens) à chaque tâche.
SONNET_TASKS = ("concept", "code", "fix", "review")
SONNET_SCHEMA = _obj({
    "concept": _nullable(CONCEPT_SCHEMA),
    "code": _nullable(CODE_SCHEMA),
    "fix": _nullable(FIX_SCHEMA),
    "review": _nullable(REVIEW_SCHEMA),
})


def answer_instruction(task):
    return (f"FORMAT DE RÉPONSE : remplis UNIQUEMENT le champ \"{task}\" de l'objet JSON ; "
            f"mets null à tous les autres champs.")


CARD_SCHEMA = _obj({
    "title": _STR,
    "description": _STR,
    "hashtags": _STRS,
    "youtube_tags": _STRS,
    "summary": _STR,
    "mechanisms": _STRS,
    "objects": _STRS,
    "palette": _STRS,
    "tags": _STRS,
})


# ---------------------------------------------------------------------- #
# Textes variables (après le préfixe mis en cache)
# ---------------------------------------------------------------------- #
def format_brief(fmt):
    v = fmt["video"]
    return (f"FORMAT DEMANDÉ : {fmt['label']} — {v['width']}×{v['height']} à {v.get('fps', 60)} fps, "
            f"durée exacte {v['duration_s']} s. Corps dynamiques simultanés : {fmt['max_bodies']} au maximum. "
            f"Son : {'musique de fond ajoutée après le rendu, n appelle jamais ctx.sound' if fmt['sound'] == 'music' else 'événements ctx.sound autorisés'}.")


def concept_prompt(*, fmt, today, history_lines, event, rejected):
    parts = [
        "TÂCHE : imagine UN nouveau concept d'animation pour cette chaîne.",
        format_brief(fmt),
        f"Date du jour : {today}.",
    ]
    if event:
        parts.append(f"ÉVÉNEMENT : ce concept doit s'inspirer de « {event['name']} » "
                     f"(idées : {event.get('ideas', 'libre')}), en restant dans l'esprit de la chaîne, "
                     "sans logo officiel, marque ni personnage protégé. Mets le nom de l'événement dans \"event\".")
    else:
        parts.append("Concept intemporel (pas d'événement) : laisse \"event\" vide.")
    if history_lines:
        parts.append("Créations déjà faites sur cette chaîne (ne pas refaire le même MÉCANISME) :\n"
                     + "\n".join(history_lines))
    if rejected:
        parts.append("Concepts refusés car trop proches de l'existant :\n" + "\n".join(rejected))
    parts.append("Le concept doit être réalisable avec le contrat technique et sans défaut physique. "
                 "\"title\" = titre de travail court ; \"pitch\" = 2 phrases ; \"scenario\" = les étapes "
                 "dans le temps ; \"tags\" = 4 à 8 mots-clés en français décrivant le mécanisme.")
    return "\n\n".join(parts)


def code_prompt(*, fmt, concept):
    return "\n\n".join([
        "TÂCHE : écris le code complet de la scène pour ce concept.",
        format_brief(fmt),
        "CONCEPT VALIDÉ :\n" + json.dumps(concept, ensure_ascii=False, indent=2),
        "Exigences : respecte le contrat (Scene, create_scene, anim_watch_world obligatoire, pas de "
        "main/fenêtre/fichiers), utilise ctx.width/ctx.height (pas de taille en dur), toute la logique dans "
        "update(), draw() sans effet de bord, simulation stable du début à la fin de la durée demandée, "
        "pas de corps qui se chevauchent à la création. Code clair et commenté brièvement. "
        "Réponds avec \"files\" (au moins scene.cpp, contenu complet) et \"notes\" (1 à 3 phrases).",
    ])


def files_dump(files):
    return "\n\n".join(f"=== {name} ===\n{content}" for name, content in sorted(files.items()))


def fix_prompt(*, fmt, concept, files, problem, previous_notes):
    parts = [
        "TÂCHE : corrige la scène.",
        format_brief(fmt),
        f"Concept : {concept.get('title')} — {concept.get('pitch')}",
        "PROBLÈME CONSTATÉ :\n" + problem,
    ]
    if previous_notes:
        parts.append("Corrections déjà tentées (ne pas les répéter) :\n" + "\n".join(f"- {n}" for n in previous_notes))
    parts += [
        "CODE ACTUEL :\n" + files_dump(files),
        "Réponds de préférence avec des \"edits\" : remplacements exacts {file, old, new} où \"old\" est un "
        "extrait copié À L'IDENTIQUE du code actuel et présent une seule fois. Si la correction est trop "
        "large, renvoie le fichier complet dans \"files\" (sinon \"files\" vide). \"notes\" : ce que tu as "
        "corrigé, en une phrase.",
    ]
    return "\n\n".join(parts)


def review_prompt(*, fmt, concept, files, frame_times, metrics_summary, warnings, attempt, max_attempts):
    return "\n\n".join([
        "TÂCHE : juge la qualité de l'animation sur les images d'aperçu ci-dessous (rendu réduit, la "
        "vidéo finale est identique en pleine résolution).",
        format_brief(fmt),
        "CONCEPT :\n" + json.dumps(concept, ensure_ascii=False),
        f"Images fournies (dans l'ordre) aux instants : {', '.join(frame_times)}.",
        "MESURES AUTOMATIQUES :\n" + metrics_summary,
        ("AVERTISSEMENTS :\n" + "\n".join(f"- {w}" for w in warnings)) if warnings else "Aucun avertissement automatique.",
        "Vérifie : l'idée se comprend, l'écran est bien rempli, rien d'important hors cadre, pas d'objet "
        "bloqué / traversant / empilé de façon anormale, couleurs et netteté conformes au cahier des "
        "charges, fin soignée, différence avec un simple changement de couleurs.",
        f"Revue {attempt}/{max_attempts}. \"approved\" = true seulement si la vidéo est publiable telle quelle. "
        "\"score\" de 0 à 10. Si non approuvée, donne les \"issues\" et des \"edits\" précis {file, old, new} "
        "(\"old\" copié à l'identique du code) qui les corrigent.",
        "CODE ACTUEL :\n" + files_dump(files),
    ])


def card_prompt(*, fmt, concept, metrics_summary, music, event):
    return "\n\n".join([
        "TÂCHE : à partir des images de la VIDÉO FINALE ci-dessous, rédige sa fiche et ses métadonnées de "
        "publication en respectant la section « Titres, descriptions et tags » du cahier des charges.",
        f"Format : {fmt['label']} ({fmt['video']['width']}×{fmt['video']['height']}, {fmt['video']['duration_s']} s).",
        "Concept prévu :\n" + json.dumps(concept, ensure_ascii=False),
        "Mesures :\n" + metrics_summary,
        f"Événement : {event['name']}" if event else "Pas d'événement.",
        f"Musique : {music}" if music else "",
        "Décris ce qui est RÉELLEMENT visible (pas ce qui était prévu). title / description / hashtags / "
        "youtube_tags dans la langue indiquée par le cahier des charges. \"summary\" = une ligne en français "
        "pour l'historique interne ; mechanisms / objects / palette / tags en français (mots-clés courts).",
    ])
