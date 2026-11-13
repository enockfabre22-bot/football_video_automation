"""
Historique CRÉATIF d'une chaîne : une fiche JSON par création (réussie ou non), écrite par
l'application (jamais directement par Claude). Partagé par toutes les animations de la chaîne
(ex. format long et Short) pour éviter de refaire le même mécanisme.
Stockage : ai_data/channels/<chaîne>/creative/<creation_id>.json
"""
from studio_common import ai_data_dir, read_json, write_json_atomic


class ChannelHistory:
    def __init__(self, channel):
        self.channel = channel
        self.dir = ai_data_dir() / "channels" / channel / "creative"

    def save_card(self, card):
        write_json_atomic(self.dir / f"{card['creation_id']}.json", card)

    def cards(self, statuses=None):
        if not self.dir.exists():
            return []
        out = []
        for p in self.dir.glob("*.json"):
            c = read_json(p)
            if c and (statuses is None or c.get("status") in statuses):
                out.append(c)
        out.sort(key=lambda c: c.get("created_at", ""))
        return out

    def successful_cards(self):
        return self.cards(statuses={"CODE_READY", "READY", "PUBLISHED"})

    def summary_lines(self, count):
        lines = []
        for c in self.successful_cards()[-count:]:
            mech = ", ".join(c.get("mechanisms", [])[:5])
            palette = ", ".join(c.get("palette", [])[:4])
            lines.append(f"- [{c.get('created_at', '')[:10]}] {c.get('title', '?')} — {c.get('summary', '')} "
                         f"(mécanismes : {mech} ; palette : {palette or '?'})")
        return lines

    def event_count(self, animation, event_name):
        return sum(1 for c in self.successful_cards()
                   if c.get("animation") == animation and c.get("event") == event_name)
