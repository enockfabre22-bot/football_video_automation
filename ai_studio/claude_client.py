"""
Unique point d'appel à l'API Claude du studio.

- Préfixe stable (contrat + cahier des charges + exemples + images) mis en cache 1 h :
  il est identique pour toutes les étapes d'une création et entre créations d'une même chaîne.
- Sorties JSON structurées (output_config.format) : l'application ne lit jamais du texte libre.
- Budgets vérifiés AVANT chaque appel, coût réel enregistré APRÈS (ai_data/usage.jsonl).
- MockClaude : mêmes entrées / sorties, sans réseau ni clé, pour tester tout le pipeline.
"""
import json
import os
import random
from pathlib import Path

from ledger import BudgetExceeded, UsageLedger  # noqa: F401 (réexporté)
from studio_common import BASE_DIR


class ClaudeError(Exception):
    pass


def _with_cache_marker(stable_blocks):
    """Copie des blocs stables avec un point de cache (TTL 1 h) sur le dernier."""
    blocks = [dict(b) for b in stable_blocks]
    if blocks:
        blocks[-1]["cache_control"] = {"type": "ephemeral", "ttl": "1h"}
    return blocks


class StudioClaude:
    def __init__(self, ai_cfg):
        self.ai_cfg = ai_cfg
        self.ledger = UsageLedger(ai_cfg)
        self.mock = False
        self._client = None

    def _sdk(self):
        if self._client is None:
            try:
                import anthropic
            except ImportError as e:
                raise ClaudeError("Le paquet 'anthropic' n'est pas installé (pip install -r requirements.txt)") from e
            from dotenv import load_dotenv
            load_dotenv(BASE_DIR / ".env")
            if not os.getenv("ANTHROPIC_API_KEY"):
                raise ClaudeError("ANTHROPIC_API_KEY absente de .env")
            limits = self.ai_cfg.get("limits", {})
            self._client = anthropic.Anthropic(
                timeout=float(limits.get("api_timeout_seconds", 600)),
                max_retries=int(limits.get("api_max_retries", 2)),
            )
        return self._client

    def call(self, *, task, model, system, stable_blocks, variable_blocks, schema,
             max_tokens, effort, animation, creation_id, budget_per_creation=None):
        """Retourne le JSON (dict) produit par Claude pour cette tâche."""
        self.ledger.check_budgets(animation, creation_id, budget_per_creation)

        params = {
            "model": model,
            "max_tokens": max_tokens,
            "system": [{"type": "text", "text": system}],
            "messages": [{"role": "user", "content": _with_cache_marker(stable_blocks) + list(variable_blocks)}],
            "output_config": {"effort": effort, "format": {"type": "json_schema", "schema": schema}},
        }
        client = self._sdk()
        try:
            # Streaming : évite les délais d'attente HTTP sur les longues réponses (code)
            with client.messages.stream(**params) as stream:
                msg = stream.get_final_message()
        except Exception as e:  # erreurs API / réseau après les tentatives automatiques du SDK
            raise ClaudeError(f"Appel API '{task}' en échec : {type(e).__name__}: {e}") from e

        u = msg.usage
        usage = {
            "input_tokens": u.input_tokens or 0,
            "output_tokens": u.output_tokens or 0,
            "cache_read_input_tokens": getattr(u, "cache_read_input_tokens", 0) or 0,
            "cache_creation_input_tokens": getattr(u, "cache_creation_input_tokens", 0) or 0,
        }
        entry = self.ledger.record(model=model, task=task, usage=usage, animation=animation,
                                   creation_id=creation_id, stop_reason=msg.stop_reason)
        print(f"      [CLAUDE] {task} ({model}) : {usage['input_tokens']} in, "
              f"{usage['cache_read_input_tokens']} cache lu, {usage['cache_creation_input_tokens']} cache écrit, "
              f"{usage['output_tokens']} out -> {entry['cost_usd']:.4f} $")

        if msg.stop_reason == "refusal":
            raise ClaudeError(f"Claude a refusé la tâche '{task}'")
        if msg.stop_reason == "max_tokens":
            raise ClaudeError(f"Réponse tronquée pour '{task}' (max_tokens={max_tokens} atteint)")
        text = next((b.text for b in msg.content if b.type == "text"), None)
        if not text:
            raise ClaudeError(f"Réponse vide pour '{task}'")
        try:
            return json.loads(text)
        except json.JSONDecodeError as e:
            raise ClaudeError(f"JSON invalide pour '{task}' : {e}") from e


# ---------------------------------------------------------------------- #
# Faux client (tests hors ligne)
# ---------------------------------------------------------------------- #
MOCK_CONCEPTS = [
    {
        "title": "Rainbow Galton Cascade",
        "pitch": "Des centaines de billes arc-en-ciel tombent à travers une forêt de clous et se rangent dans des colonnes.",
        "mechanisms": ["planche de galton", "entonnoir", "colonnes de tri"],
        "objects": ["billes", "clous"],
        "palette": ["arc-en-ciel", "fond violet nuit"],
        "tags": ["galton", "billes", "arc-en-ciel", "tri"],
    },
    {
        "title": "Gear Sorting Factory",
        "pitch": "Une usine d'engrenages trie des billes par couleur avant de les renvoyer vers un grand réservoir.",
        "mechanisms": ["engrenages", "tri par couleur", "ascenseur à godets", "boucle fermée"],
        "objects": ["billes", "engrenages", "godets"],
        "palette": ["rouge", "bleu", "jaune", "fond anthracite"],
        "tags": ["usine", "engrenages", "tri", "couleurs"],
    },
    {
        "title": "Spiral Sand Clock",
        "pitch": "Des grains colorés descendent une spirale géante et remplissent un sablier par strates.",
        "mechanisms": ["spirale", "sablier", "strates colorées"],
        "objects": ["grains", "spirale"],
        "palette": ["pastel", "fond crème"],
        "tags": ["spirale", "sablier", "grains", "strates"],
    },
]


class MockClaude(StudioClaude):
    """Répond sans réseau avec des réponses prévisibles ; enregistre l'usage avec un coût nul."""

    def __init__(self, ai_cfg):
        super().__init__(ai_cfg)
        self.mock = True

    def call(self, *, task, model, system, stable_blocks, variable_blocks, schema,
             max_tokens, effort, animation, creation_id, budget_per_creation=None):
        self.ledger.check_budgets(animation, creation_id, budget_per_creation)
        chars = sum(len(b.get("text", "")) for b in list(stable_blocks) + list(variable_blocks))
        data = self._answer(task)
        self.ledger.record(model=model, task=task, animation=animation, creation_id=creation_id, mock=True,
                           usage={"input_tokens": chars // 4, "output_tokens": len(json.dumps(data)) // 4})
        print(f"      [CLAUDE SIMULÉ] {task} ({model})")
        return data

    def _answer(self, task):
        if task == "concept":
            c = dict(random.choice(MOCK_CONCEPTS))
            c.update({
                "scenario": ["les billes arrivent", "la machine se remplit", "fin ordonnée"],
                "interactions": ["collisions", "gravité"],
                "environment": "fond sombre uni, machine plein écran",
                "estimated_bodies": 400,
                "event": "",
            })
            return c
        if task == "code":
            src = (Path(__file__).parent / "mock" / "scene.cpp").read_text(encoding="utf-8")
            return {"files": [{"path": "scene.cpp", "content": src}], "notes": "scène simulée"}
        if task == "fix":
            return {"edits": [], "files": [], "notes": "le faux client ne sait pas corriger"}
        if task == "review":
            return {"approved": True, "score": 8, "issues": [], "edits": [], "notes": "aperçu correct (simulé)"}
        if task == "card":
            return {
                "title": "Rainbow Marble Cascade 🌈 Satisfying 4K Physics Simulation",
                "description": "Hundreds of rainbow marbles fall through a field of pegs and settle into neat columns. Relax and enjoy.",
                "hashtags": ["#satisfying", "#physics", "#4k"],
                "youtube_tags": ["satisfying simulation", "marble run", "4k animation", "physics simulation", "relaxing video"],
                "summary": "Planche de Galton arc-en-ciel, billes qui remplissent des colonnes.",
                "mechanisms": ["planche de galton", "colonnes"],
                "objects": ["billes", "clous"],
                "palette": ["arc-en-ciel"],
                "tags": ["galton", "billes"],
            }
        raise ClaudeError(f"Tâche simulée inconnue : {task}")
