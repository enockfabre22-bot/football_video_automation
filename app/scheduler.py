import os
import time
import random
import schedule
from datetime import datetime, timedelta
from main import prepare_daily_videos, publish_scheduled_job, discard_job, setup_logging
from publication_manager import PublicationManager
from job_manager import JobManager
from maintenance import cleanup_disk


def safe_cleanup():
    """Le nettoyage ne doit jamais faire tomber le démon."""
    try:
        cleanup_disk()
    except Exception as e:
        print(f"[MAINTENANCE] Erreur pendant le nettoyage (ignorée) : {e}")

# Au redémarrage, une publication manquée est rattrapée si son créneau s'est terminé
# il y a moins de CATCH_UP_MAX_HOURS heures. Au-delà, le job est considéré comme expiré.
CATCH_UP_MAX_HOURS = 12

class AutomationScheduler:
    def __init__(self):
        # Plus de compte codé en dur ici, le scheduler gère TOUT le monde !
        self.pub_mgr = PublicationManager()
        self.job_mgr = JobManager()

    def pick_random_times_in_window(self, start_str, end_str, count=1):
        """
        Tire au sort 'count' horaires (HH:MM) répartis dans la fenêtre configurée.
        """
        fmt = "%H:%M"
        start_dt = datetime.strptime(start_str, fmt)
        end_dt = datetime.strptime(end_str, fmt)

        total_minutes = int((end_dt - start_dt).total_seconds() // 60)
        if total_minutes <= 0:
            return [start_str] * count

        chosen_offsets = sorted(random.sample(range(total_minutes + 1), min(count, total_minutes + 1)))
        return [(start_dt + timedelta(minutes=offset)).strftime(fmt) for offset in chosen_offsets]

    @staticmethod
    def _at(day, hhmm):
        """Combine une date et une heure 'HH:MM' en datetime."""
        return datetime.combine(day, datetime.strptime(hhmm, "%H:%M").time())

    @staticmethod
    def _window(job):
        window = (job.get("metadata") or {}).get("publish_window", {"start": "08:00", "end": "10:00"})
        return window.get("start", "08:00"), window.get("end", "10:00")

    def daily_orchestration(self, account_name):
        """
        Job quotidien ciblé sur un compte spécifique.
        """
        setup_logging()
        print(f"\n⏰ [SCHEDULER] Lancement du contrôle quotidien pour '{account_name}'")
        safe_cleanup()

        ready_jobs = prepare_daily_videos(account_name)
        if not ready_jobs:
            print(f"⏰ [SCHEDULER] Aucun job à planifier aujourd'hui pour '{account_name}'.")
            return

        for job in ready_jobs:
            job_id = job["job_id"]
            start_win, end_win = self._window(job)

            # Tirage d'une seule heure pour ce job spécifique
            pub_time = self.pick_random_times_in_window(start_win, end_win, 1)[0]

            # Comme avant : si l'heure est déjà passée aujourd'hui, la publication a lieu demain
            planned_dt = self._at(datetime.now().date(), pub_time)
            if planned_dt <= datetime.now():
                planned_dt += timedelta(days=1)

            # L'heure prévue est enregistrée dans le job : elle survit à un redémarrage du service
            metadata = job.get("metadata") or {}
            metadata["planned_publish_at"] = planned_dt.isoformat(timespec="minutes")
            job = self.job_mgr.update_status(job_id, "SCHEDULED", metadata=metadata) or job

            print(f"📅 [PLANIFICATION] Le job '{job_id}' ({account_name}) est programmé à {pub_time} (Créneau: {start_win}-{end_win})")
            self._schedule_publication(job, account_name, planned_dt)

    def _schedule_publication(self, job, account_name, planned_dt):
        """Planifie une publication unique (sans doublon si le job est déjà planifié)."""
        job_id = job["job_id"]
        if schedule.get_jobs(job_id):
            print(f"   [PLANIFICATION] '{job_id}' est déjà planifié, pas de doublon.")
            return
        schedule.every().day.at(planned_dt.strftime("%H:%M")).do(
            self._execute_and_cancel,
            job=job,
            account_name=account_name,
            planned_date=planned_dt.date().isoformat(),
        ).tag(job_id)

    def _execute_and_cancel(self, job, account_name, planned_date=None):
        """Publie la vidéo à l'heure prévue puis retire la tâche ponctuelle du scheduler."""
        # Si la publication est prévue un autre jour, on attend ce jour-là
        if planned_date and datetime.now().date().isoformat() < planned_date:
            return None

        # On relit le job sur le disque : s'il n'est plus en attente, il a déjà été traité
        current = self.job_mgr.get_pending_job(job["job_id"])
        if not current:
            print(f"[SCHEDULER] Job '{job['job_id']}' introuvable dans jobs/pending/ (déjà traité). Annulé.")
            return schedule.CancelJob

        print(f"\n🚀 [SCHEDULER] Heure atteinte ! Publication automatique du job : {job['job_id']} sur {account_name}")
        publish_scheduled_job(current, account_name)
        return schedule.CancelJob

    def restore_scheduled_jobs(self, active_accounts):
        """
        Au démarrage du démon : reprend les jobs SCHEDULED de jobs/pending/ (perdus de la mémoire
        lors d'un redémarrage) -> reprogrammés, rattrapés tout de suite, ou expirés.
        Retourne la liste des jobs à publier immédiatement.
        """
        now = datetime.now()
        catch_up = []
        jobs = self.job_mgr.list_pending_jobs(status="SCHEDULED")
        if not jobs:
            return catch_up

        print(f"🔁 [REPRISE] {len(jobs)} job(s) planifié(s) trouvé(s) dans jobs/pending/")

        for job in jobs:
            job_id = job["job_id"]
            account = job.get("account")
            if account not in active_accounts:
                print(f"   [REPRISE] '{job_id}' : compte '{account}' inactif, job laissé en attente.")
                continue
            if schedule.get_jobs(job_id):
                continue  # déjà planifié en mémoire

            video_path = job.get("video_path")
            if not video_path or not os.path.exists(video_path):
                print(f"   [REPRISE] '{job_id}' : vidéo introuvable ({video_path}) -> job abandonné")
                discard_job(job, reason="Vidéo introuvable lors de la reprise après redémarrage")
                continue

            start_win, end_win = self._window(job)
            planned_str = (job.get("metadata") or {}).get("planned_publish_at")

            if planned_str:
                planned_dt = datetime.fromisoformat(planned_str)
                pub_day = planned_dt.date()
            else:
                # Jobs créés avant ce correctif : jour de publication = première occurrence
                # du créneau qui se termine après la planification (updated_at)
                planned_dt = None
                ref = datetime.fromisoformat(job.get("updated_at") or job.get("created_at"))
                pub_day = ref.date()
                if ref > self._at(pub_day, end_win):
                    pub_day += timedelta(days=1)

            deadline = self._at(pub_day, end_win)

            if planned_dt and planned_dt > now:
                print(f"   [REPRISE] '{job_id}' reprogrammé à {planned_dt:%Y-%m-%d %H:%M}")
                self._schedule_publication(job, account, planned_dt)

            elif not planned_dt and now < deadline:
                # Nouveau tirage dans ce qui reste du créneau
                earliest = max(self._at(pub_day, start_win), now + timedelta(minutes=2))
                if earliest >= deadline:
                    earliest = deadline
                pub_time = self.pick_random_times_in_window(earliest.strftime("%H:%M"), end_win, 1)[0]
                new_dt = self._at(pub_day, pub_time)
                print(f"   [REPRISE] '{job_id}' reprogrammé à {new_dt:%Y-%m-%d %H:%M} (créneau {start_win}-{end_win})")
                self._schedule_publication(job, account, new_dt)

            elif now <= deadline + timedelta(hours=CATCH_UP_MAX_HOURS):
                print(f"   [REPRISE] '{job_id}' : heure de publication manquée -> rattrapage immédiat")
                catch_up.append(job)

            else:
                print(f"   [REPRISE] '{job_id}' : créneau terminé depuis plus de {CATCH_UP_MAX_HOURS} h -> expiré")
                discard_job(job, reason=f"Expiré : créneau terminé le {deadline:%Y-%m-%d %H:%M} (service arrêté)")

        return catch_up

    def start_daemon(self):
        """Lance la boucle infinie multi-comptes."""
        setup_logging()

        accounts = self.pub_mgr.get_all_active_accounts("accounts")

        print("==================================================")
        print(f"🤖 DÉMON SCHEDULER MULTI-COMPTES ACTIF")
        print(f"   - {len(accounts)} compte(s) chargé(s) en mémoire")
        print("==================================================")

        for cfg in accounts:
            acc_name = cfg["internal_id"]
            slots = cfg.get("schedule_slots", [])

            if slots:
                earliest_start = min([s.get("window", {}).get("start", "08:00") for s in slots])
            else:
                window = cfg.get("publication_window", {"start": "08:00", "end": "10:00"})
                earliest_start = window.get("start", "08:00")

            # On lance la préparation 300 minutes avant le premier créneau du compte
            prep_dt = datetime.strptime(earliest_start, "%H:%M") - timedelta(minutes=300)
            prep_time_str = prep_dt.strftime("%H:%M")

            print(f"   ⚙️  [{acc_name}] Heure de préparation calée à {prep_time_str}")

            # On planifie la préparation spécifique à CE compte
            schedule.every().day.at(prep_time_str).do(self.daily_orchestration, account_name=acc_name)

        print("==================================================\n")

        # Nettoyage des restes d'un éventuel plantage (images partielles, vidéos orphelines)
        safe_cleanup()

        # Reprise des publications perdues lors d'un redémarrage du service
        catch_up = self.restore_scheduled_jobs({cfg["internal_id"] for cfg in accounts})
        for job in catch_up:
            print(f"\n🚀 [RATTRAPAGE] Publication du job : {job['job_id']} sur {job['account']}")
            publish_scheduled_job(job, job["account"])

        while True:
            schedule.run_pending()
            time.sleep(20)

# ==========================================
# BLOC DE TEST DU SCHEDULER
# ==========================================
if __name__ == "__main__":
    import sys
    sched = AutomationScheduler()

    if "--daemon" in sys.argv:
        sched.start_daemon()
    else:
        print("[TEST SCHEDULER] Test du tirage aléatoire :")
        tirage = sched.pick_random_times_in_window("08:00", "10:00", count=2)
        print(f" - 2 horaires tirés entre 08:00 et 10:00 : {tirage}")

        print("\n[TEST SCHEDULER] Test de chargement multi-comptes avant lancement du Démon...")
        accounts = sched.pub_mgr.get_all_active_accounts("accounts")
        for acc in accounts:
            print(f" - Détection du compte : {acc['internal_id']}")
