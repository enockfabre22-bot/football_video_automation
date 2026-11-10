"""
Anti-répétition locale (sans IA) : compare un concept aux fiches existantes de la chaîne.
score = 0,6 × Jaccard pondéré (mécanismes ×3, objets ×1, tags ×2) + 0,4 × cosinus TF-IDF du texte.
"""
import math
import re
import unicodedata
from collections import Counter

STOPWORDS = set("""le la les un une des de du d l et ou en dans sur sous par pour avec sans qui que
se sa son ses leur leurs au aux a est sont puis plus tres très ce cet cette ces il elle ils elles
the a an of and or in on to with for by from into""".split())


def _norm(word):
    word = unicodedata.normalize("NFKD", word.lower())
    word = "".join(c for c in word if not unicodedata.combining(c))
    return word.strip()


def _tokens(text):
    words = re.findall(r"[a-zA-ZÀ-ÿ0-9]+", text or "")
    out = []
    for w in words:
        w = _norm(w)
        if len(w) < 3 or w in STOPWORDS:
            continue
        out.append(w[:-1] if w.endswith("s") and len(w) > 4 else w)   # pluriel simple
    return out


def _keyset(items):
    s = set()
    for it in items or []:
        s.update(_tokens(it))
    return s


def _jaccard(a, b):
    if not a and not b:
        return 0.0
    return len(a & b) / len(a | b)


def _text(item):
    return " ".join([item.get("title", ""), item.get("pitch", ""), item.get("summary", ""),
                     " ".join(item.get("scenario", []) or [])])


def _structural(a, b):
    weights = {"mechanisms": 3.0, "objects": 1.0, "tags": 2.0}
    total = sum(weights.values())
    return sum(w * _jaccard(_keyset(a.get(k)), _keyset(b.get(k))) for k, w in weights.items()) / total


def _tfidf_cosines(query_text, docs_texts):
    docs = [Counter(_tokens(t)) for t in docs_texts]
    q = Counter(_tokens(query_text))
    n = len(docs) + 1
    df = Counter()
    for d in docs + [q]:
        df.update(d.keys())
    idf = {w: math.log((1 + n) / (1 + df[w])) + 1 for w in df}

    def vec(c):
        return {w: tf * idf[w] for w, tf in c.items()}

    def cos(u, v):
        dot = sum(u[w] * v.get(w, 0.0) for w in u)
        nu = math.sqrt(sum(x * x for x in u.values()))
        nv = math.sqrt(sum(x * x for x in v.values()))
        return dot / (nu * nv) if nu and nv else 0.0

    qv = vec(q)
    return [cos(qv, vec(d)) for d in docs]


def rank_similar(concept, cards, top=3):
    """Retourne [(score, carte)] triés du plus proche au moins proche."""
    if not cards:
        return []
    cosines = _tfidf_cosines(_text(concept), [_text(c) for c in cards])
    scored = []
    for card, cos in zip(cards, cosines):
        score = 0.6 * _structural(concept, card) + 0.4 * cos
        scored.append((round(score, 3), card))
    scored.sort(key=lambda x: x[0], reverse=True)
    return scored[:top]
