"""
Pistes d'inspiration tirées au hasard pour chaque concept : elles poussent Claude hors de ses
habitudes (billes + usine + arc-en-ciel) sans lui imposer une idée précise. Il peut les combiner,
les détourner ou proposer mieux. Les pistes déjà très utilisées récemment sont évitées.

Pour adapter la chaîne, une liste peut être remplacée par ai_channels/<chaîne>/inspiration.json :
  {"phenomenes": [...], "palettes": [...], "compositions": [...], "ambiances": [...]}
"""
import random
import unicodedata

from studio_common import channel_dir, read_json

PHENOMENES = [
    "dominos et réactions en chaîne géantes",
    "pendules (vague de pendules synchronisés, pendules doubles)",
    "cordes, chaînes et ponts suspendus (joints Box2D)",
    "corps mous / gelée (particules reliées par des ressorts)",
    "empilement et construction d'une structure pièce par pièce",
    "effondrement ou démolition au ralenti, très satisfaisant",
    "mécanismes d'horlogerie (engrenages, cames, échappements)",
    "sable ou grains qui sculptent une forme ou un dessin",
    "« liquide » imité par des milliers de petites particules qui coulent",
    "champs de force : aimants, attraction vers un centre, tourbillons",
    "orbites et trajectoires (gravité planétaire 2D)",
    "rebonds sur des formes géométriques qui créent des motifs",
    "tri et rangement jusqu'à l'ordre parfait",
    "objets qui tombent et composent une mosaïque / un pixel art",
    "ressorts, trampolines et catapultes",
    "tissu ou filet de particules reliées qui ondule",
    "mélange puis séparation de couleurs",
    "motifs géométriques / fractales construits par la physique",
    "roues, rouleaux et convoyeurs",
    "labyrinthe ou parcours parcouru par des objets",
    "remplissage parfait d'un contenant ou d'une silhouette",
    "balançoires, bascules et équilibres fragiles",
]

PALETTES = [
    "pastels doux sur fond crème",
    "néons vifs sur fond noir profond",
    "monochrome bleu (du marine au bleu ciel)",
    "coucher de soleil (orange, corail, rose, violet)",
    "noir et blanc avec un seul accent de couleur",
    "couleurs bonbon (rose, menthe, lavande, citron)",
    "tons terre et bois (sable, terracotta, olive, brun)",
    "océan (turquoise, écume, sable, bleu profond)",
    "or et noir élégant",
    "verts forêt et mousse avec touches de jaune",
    "couleurs primaires franches façon Bauhaus (rouge, jaune, bleu, noir)",
    "glacier (blanc, cyan pâle, bleu acier)",
    "rouge et noir très contrasté",
    "dégradé automnal (bordeaux, orange brûlé, moutarde)",
    "arc-en-ciel",
]

COMPOSITIONS = [
    "symétrie parfaite autour d'un axe central",
    "une seule grande forme centrale qui évolue",
    "plusieurs petits ateliers indépendants qui travaillent en parallèle",
    "un parcours qui serpente sur tout l'écran",
    "minimaliste, beaucoup d'espace vide, peu d'objets très soignés",
    "l'écran se remplit progressivement jusqu'à être plein",
    "progression de gauche à droite comme une frise",
    "construction de bas en haut",
    "vue en coupe d'un mécanisme complexe",
]

AMBIANCES = [
    "hypnotique et lent",
    "chaos joyeux qui finit en ordre parfait",
    "précision mécanique, tout s'emboîte",
    "tension puis soulagement (va-t-il réussir ?)",
    "contemplatif, presque méditatif",
    "énergique et rebondissant",
]


def _norm(text):
    text = unicodedata.normalize("NFKD", str(text).lower())
    return "".join(c for c in text if not unicodedata.combining(c))


def _recently_used(item, recent_texts):
    """Vrai si les mots principaux de la piste apparaissent dans les fiches récentes."""
    words = [w for w in _norm(item).replace("(", " ").replace(")", " ").replace(",", " ").split() if len(w) > 4]
    return any(sum(1 for w in words[:3] if w in t) >= 2 for t in recent_texts)


def pick(channel, recent_cards, rng=None):
    rng = rng or random.Random()
    custom = read_json(channel_dir(channel) / "inspiration.json", {}) or {}
    lists = {
        "phénomène physique": custom.get("phenomenes") or PHENOMENES,
        "palette": custom.get("palettes") or PALETTES,
        "composition": custom.get("compositions") or COMPOSITIONS,
        "ambiance": custom.get("ambiances") or AMBIANCES,
    }
    recent_texts = [_norm(" ".join([c.get("title", ""), c.get("summary", ""), " ".join(c.get("mechanisms", [])),
                                    " ".join(c.get("palette", []))])) for c in recent_cards[-8:]]
    result = {}
    for label, items in lists.items():
        fresh = [i for i in items if not _recently_used(i, recent_texts)] or list(items)
        if label == "phénomène physique":
            result["phénomènes physiques (un ou les deux)"] = " + ".join(rng.sample(fresh, min(2, len(fresh))))
        else:
            result[label] = rng.choice(fresh)
    return result
