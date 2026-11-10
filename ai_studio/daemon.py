"""
Service du studio IA (lancé par systemd : python ai_studio/cli.py daemon).

Toutes les `studio.loop_interval_minutes` :
  1. reprend les créations interrompues (arrêt du service, redémarrage du VPS) ;
  2. SESSION DE CONCEPTION : si le stock d'avance est insuffisant, enchaîne un LOT de créations
     (concept -> code -> aperçu -> contrôles) dans la même heure pour partager le cache Claude ;
     chacune s'arrête en CODE_READY ;
  3. FILE DE RENDU : rend UNE création CODE_READY (CPU seulement, aucun appel API sauf la fiche).

Le studio ne publie jamais rien : il remplit le stock que le scheduler consomme.
Une panne du studio n'affecte jamais le scheduler (processus et service séparés).
"""
import signal
import subprocess
import time
import traceback
from datetime import datetime

from studio_common import (ai_data_dir, ai_settings, list_ai_animations, load_ai_config, log, stock_target)


def _in_window(window):
    now = datetime.now().strftime("%H:%M")
    start, end = window.get("start", "00:00"), window.get("end", "23:59")
    return start <= now <= end if start <= end else (now >= start or now <= end)


class StudioDaemon:
    def __init__(self, mock=False, once=False):
        self.mock = mock
        self.once = once

    # ------------------------------------------------------------------ #
    def _client(self, ai_cfg):
        from claude_client import MockClaude, StudioClaude
        return MockClaude(ai_cfg) if self.mock else StudioClaude(ai_cfg)

    def _animations(self):
        return [a for a in list_ai_animations() if a.get("enabled")]

    def _creations(self, anim_name, status):
        from ai_stock import AIStockManager
        stock = AIStockManager()
        return [(d, s) for d, s in stock.list_creations(anim_name, status) if bool(s.get("mock")) == self.mock]

    def stock_levels(self, anim_cfg):
        """(objectif, vidéos prêtes, codes prêts, en cours)."""
        from ai_stock import AIStockManager
        name = anim_cfg["name"]
        stock = AIStockManager()
        ready = sum(1 for d, s in stock.list_creations(name, "READY")
                    if bool(s.get("mock")) == self.mock and stock._is_deliverable(d, s))
        return (stock_target(anim_cfg), ready, len(self._creations(name, "CODE_READY")),
                len(self._creations(name, "IN_PROGRESS")))

    @staticmethod
    def _kill_stale_containers():
        """Conteneurs du studio orphelins (service tué pendant un rendu)."""
        try:
            res = subprocess.run(["docker", "ps", "-q", "--filter", "name=anim-work-", "--filter", "name=anim-render-"],
                                 capture_output=True, text=True)
            ids = res.stdout.split()
            if ids:
                subprocess.run(["docker", "kill"] + ids, capture_output=True)
                log(f"{len(ids)} conteneur(s) orphelin(s) arrêté(s)")
        except FileNotFoundError:
            pass

    # ------------------------------------------------------------------ #
    def resume_interrupted(self, ai_cfg):
        from pipeline import DESIGN_STEPS, CreationPipeline
        for anim in self._animations():
            for d, s in self._creations(anim["name"], "IN_PROGRESS"):
                log(f"Reprise de la création interrompue {s['creation_id']} (étape « {s.get('step')} »)")
                p = CreationPipeline.resume(s["creation_id"], self._client(ai_cfg), ai_cfg)
                p.run(stop_before_render=s.get("step") in DESIGN_STEPS)

    def created_today(self):
        today = datetime.now().strftime("%Y%m%d")
        return sum(1 for a in self._animations()
                   for d in (ai_data_dir() / a["name"] / "creations").glob(f"*_{today}_*"))

    def design_session(self, ai_cfg):
        from claude_client import BudgetExceeded
        from ledger import UsageLedger
        from pipeline import CreationPipeline
        studio = ai_cfg.get("studio", {})
        batch_size = int(studio.get("design_batch_size", 3))
        max_per_day = int(studio.get("max_creations_per_day", 8))

        deficits, urgent = {}, False
        for anim in self._animations():
            target, ready, code_ready, in_progress = self.stock_levels(anim)
            missing = target - (ready + code_ready + in_progress)
            if missing > 0:
                deficits[anim["name"]] = missing
            if ready + code_ready < min(2, target):
                urgent = True
        total = sum(deficits.values())
        if not deficits:
            return
        # On attend d'avoir un lot complet à produire (cache partagé), sauf si le stock est presque vide
        if total < batch_size and not urgent:
            return
        remaining_today = max_per_day - self.created_today()
        count = min(batch_size, total, remaining_today)
        if count <= 0:
            log(f"Limite de {max_per_day} créations par jour atteinte : session de conception reportée")
            return

        log(f"=== Session de conception : {count} création(s) — manques : {deficits} ===")
        failures_in_a_row = 0
        for _ in range(count):
            name = max(deficits, key=deficits.get)
            try:
                UsageLedger(ai_cfg).check_budgets(name, "__nouvelle__")
            except BudgetExceeded as e:
                log(f"Session interrompue : {e}")
                return
            p = CreationPipeline.start_new(name, self._client(ai_cfg), ai_cfg)
            status = p.run(stop_before_render=True)
            deficits[name] -= 1
            if deficits[name] <= 0:
                deficits.pop(name)
            failures_in_a_row = 0 if status == "CODE_READY" else failures_in_a_row + 1
            if failures_in_a_row >= 2:
                log("2 échecs de suite : session de conception interrompue (vérifier logs/ai_studio.log)")
                return
            if not deficits:
                return

    def render_one(self, ai_cfg):
        from maintenance import has_enough_disk_space
        from pipeline import CreationPipeline
        queue = []
        for anim in self._animations():
            target, ready, _, _ = self.stock_levels(anim)
            for d, s in self._creations(anim["name"], "CODE_READY"):
                queue.append((ready, s.get("code_ready_at", ""), anim, s))
        if not queue:
            return False
        queue.sort(key=lambda q: (q[0], q[1]))   # le format le moins fourni d'abord, puis le plus ancien
        ready, _, anim, s = queue[0]
        if not has_enough_disk_space(anim):
            log("Espace disque insuffisant : rendu reporté")
            return False
        log(f"=== File de rendu : {s['creation_id']} ({len(queue)} en attente) ===")
        CreationPipeline.resume(s["creation_id"], self._client(ai_cfg), ai_cfg).run()
        return True

    # ------------------------------------------------------------------ #
    def iteration(self):
        ai_cfg = load_ai_config()
        if not ai_cfg.get("enabled") and not self.mock:
            log("Studio désactivé (config/ai.json : enabled = false)")
            return False
        if not _in_window(ai_cfg.get("studio", {}).get("window", {})):
            return False
        self.resume_interrupted(ai_cfg)
        self.design_session(ai_cfg)
        return self.render_one(ai_cfg)

    def run_forever(self):
        def _stop(signum, frame):
            raise SystemExit(0)
        signal.signal(signal.SIGTERM, _stop)

        log(f"Démarrage du service du studio IA{' (SIMULÉ)' if self.mock else ''}")
        self._kill_stale_containers()
        while True:
            busy = False
            try:
                busy = self.iteration()
            except SystemExit:
                log("Arrêt demandé : les créations en cours reprendront au prochain démarrage")
                raise
            except Exception as e:   # le service ne doit jamais s'arrêter sur une erreur
                log(f"ERREUR dans la boucle du studio : {type(e).__name__}: {e}\n{traceback.format_exc()}")
            if self.once:
                return
            # Après un rendu, on enchaîne directement (file d'attente) ; sinon on patiente
            interval = 0.1 if busy else float(load_ai_config().get("studio", {}).get("loop_interval_minutes", 10))
            time.sleep(interval * 60)
