"""
Studio IA — ligne de commande (à lancer depuis la racine du projet).

  python ai_studio/cli.py status                         stock, créations en cours, budgets du mois
  python ai_studio/cli.py create <animation> [--mock]    lance UNE création complète
        [--event "Noël" | --no-event]                    forcer / interdire un événement
  python ai_studio/cli.py resume <creation_id> [--mock]  reprend une création interrompue
  python ai_studio/cli.py history <chaîne>               fiches créatives de la chaîne
  python ai_studio/cli.py costs [--month AAAA-MM|*]      dépenses API détaillées
  python ai_studio/cli.py events <chaîne>                événements à venir
  python ai_studio/cli.py context <animation>            taille du contexte mis en cache

--mock : faux Claude (aucun appel API, coût nul) pour tester le pipeline complet.
"""
import argparse
import sys
from collections import defaultdict
from datetime import datetime

from studio_common import BASE_DIR, ai_settings, list_ai_animations, load_ai_animation, load_ai_config


class _Tee:
    """Duplique la sortie dans logs/ai_studio.log avec horodatage."""

    def __init__(self, stream, path):
        self.stream = stream
        path.parent.mkdir(parents=True, exist_ok=True)
        self.file = open(path, "a", encoding="utf-8")
        self.at_line_start = True

    def write(self, text):
        self.stream.write(text)
        try:
            for chunk in text.splitlines(keepends=True):
                if self.at_line_start and chunk.strip():
                    self.file.write(datetime.now().strftime("[%Y-%m-%d %H:%M:%S] "))
                self.file.write(chunk)
                self.at_line_start = chunk.endswith("\n")
            self.file.flush()
        except OSError:
            pass

    def flush(self):
        self.stream.flush()


def _client(ai_cfg, mock):
    from claude_client import MockClaude, StudioClaude
    if mock:
        return MockClaude(ai_cfg)
    if not ai_cfg.get("enabled"):
        sys.exit("Le studio IA est désactivé (config/ai.json : \"enabled\": false). Utilise --mock pour tester.")
    return StudioClaude(ai_cfg)


def cmd_status(ai_cfg):
    from ai_stock import AIStockManager
    from ledger import UsageLedger
    stock = AIStockManager()
    ledger = UsageLedger(ai_cfg)
    print(f"Studio IA : {'ACTIVÉ' if ai_cfg.get('enabled') else 'désactivé'}")
    for anim in list_ai_animations():
        name = anim["name"]
        ai = ai_settings(anim)
        counts = defaultdict(int)
        for _, s in stock.list_creations(name):
            counts[s.get("status")] += 1
        print(f"\n■ {name} (chaîne {ai['channel']}, {'activée' if anim.get('enabled') else 'désactivée'}) "
              f"— prêtes {stock.count_ready(name)}/{ai['stock_target']}")
        print("   " + ", ".join(f"{k}: {v}" for k, v in sorted(counts.items())) if counts else "   (aucune création)")
        print(f"   dépense du mois : {ledger.spent(animation=name):.2f} $")
    b = ai_cfg.get("budgets_usd", {})
    print(f"\nDépense globale du mois : {ledger.spent():.2f} $ / {b.get('global_monthly')} $")


def cmd_create(ai_cfg, args):
    from pipeline import CreationPipeline
    client = _client(ai_cfg, args.mock)
    p = CreationPipeline.start_new(args.animation, client, ai_cfg, event_name=args.event, no_event=args.no_event)
    status = p.run()
    print(f"\nRésultat : {status} — {p.dir}")
    return 0 if status == "READY" else 1


def cmd_resume(ai_cfg, args):
    from pipeline import CreationPipeline
    p = CreationPipeline.resume(args.creation_id, _client(ai_cfg, args.mock), ai_cfg)
    status = p.run()
    print(f"\nRésultat : {status} — {p.dir}")
    return 0 if status == "READY" else 1


def cmd_history(args):
    from history import ChannelHistory
    cards = ChannelHistory(args.channel).cards()
    if not cards:
        print("Aucune fiche.")
    for c in cards:
        print(f"{c.get('created_at', '')[:16]}  {c.get('status', ''):9}  {c.get('animation', ''):22}  "
              f"{c.get('title', '')}  ({c.get('cost_usd', 0):.2f} $)"
              + (f"  [{c.get('event')}]" if c.get("event") else "")
              + (f"\n{'':60}raison : {c.get('reason')}" if c.get("reason") else ""))


def cmd_costs(ai_cfg, args):
    from ledger import UsageLedger
    entries = UsageLedger(ai_cfg).entries(args.month)
    by_task = defaultdict(lambda: [0, 0.0, 0, 0, 0])
    for e in entries:
        k = (e.get("animation"), e.get("task"), e.get("model"))
        by_task[k][0] += 1
        by_task[k][1] += e.get("cost_usd", 0)
        by_task[k][2] += e.get("input_tokens", 0)
        by_task[k][3] += e.get("cache_read_input_tokens", 0)
        by_task[k][4] += e.get("output_tokens", 0)
    total = 0.0
    for (anim, task, model), (n, cost, tin, tcache, tout) in sorted(by_task.items()):
        total += cost
        print(f"{anim:22} {task:8} {model:20} {n:4} appels  {cost:8.3f} $   in {tin}  cache {tcache}  out {tout}")
    print(f"TOTAL : {total:.3f} $ ({len(entries)} appels)")


def cmd_events(args):
    from events_calendar import upcoming_events
    for e in upcoming_events(args.channel, lead_days=120):
        print(f"{e['publish_after']} -> {e['publish_before']}  {e['name']}  ({e.get('ideas', '')})")


def cmd_context(args):
    from context_builder import build_stable_blocks
    ai = ai_settings(load_ai_animation(args.animation))
    _, summary = build_stable_blocks(ai["channel"], ai["profile"])
    for k, v in summary.items():
        print(f"{k}: {v}")


def main():
    parser = argparse.ArgumentParser(description="Studio IA")
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    p = sub.add_parser("create")
    p.add_argument("animation")
    p.add_argument("--mock", action="store_true")
    p.add_argument("--event")
    p.add_argument("--no-event", action="store_true")
    p = sub.add_parser("resume")
    p.add_argument("creation_id")
    p.add_argument("--mock", action="store_true")
    p = sub.add_parser("history")
    p.add_argument("channel")
    p = sub.add_parser("costs")
    p.add_argument("--month")
    p = sub.add_parser("events")
    p.add_argument("channel")
    p = sub.add_parser("context")
    p.add_argument("animation")
    args = parser.parse_args()

    sys.stdout = _Tee(sys.stdout, BASE_DIR / "logs" / "ai_studio.log")
    ai_cfg = load_ai_config()
    if args.cmd == "status":
        cmd_status(ai_cfg)
    elif args.cmd == "create":
        return cmd_create(ai_cfg, args)
    elif args.cmd == "resume":
        return cmd_resume(ai_cfg, args)
    elif args.cmd == "history":
        cmd_history(args)
    elif args.cmd == "costs":
        cmd_costs(ai_cfg, args)
    elif args.cmd == "events":
        cmd_events(args)
    elif args.cmd == "context":
        cmd_context(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
