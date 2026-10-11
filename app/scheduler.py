import time
import random
import schedule
from datetime import datetime, timedelta
from main import prepare_daily_videos, publish_scheduled_job, setup_logging
from publication_manager import PublicationManager

class AutomationScheduler:
    def __init__(self):
        # Plus de compte codé en dur ici, le scheduler gère TOUT le monde !
        self.pub_mgr = PublicationManager()

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

    def daily_orchestration(self, account_name):
        """
        Job quotidien ciblé sur un compte spécifique.
        """
        setup_logging()
        print(f"\n⏰ [SCHEDULER] Lancement du contrôle quotidien pour '{account_name}'")

        ready_jobs = prepare_daily_videos(account_name)
        if not ready_jobs:
            print(f"⏰ [SCHEDULER] Aucun job à planifier aujourd'hui pour '{account_name}'.")
            return

        for job in ready_jobs:
            job_id = job["job_id"]
            
            # On lit la fenêtre depuis les métadonnées du job
            window = job.get("metadata", {}).get("publish_window", {"start": "08:00", "end": "10:00"})
            start_win = window.get("start", "08:00")
            end_win = window.get("end", "10:00")
            
            # Tirage d'une seule heure pour ce job spécifique
            pub_time = self.pick_random_times_in_window(start_win, end_win, 1)[0]
            
            print(f"📅 [PLANIFICATION] Le job '{job_id}' ({account_name}) est programmé à {pub_time} (Créneau: {start_win}-{end_win})")
            
            # Planifie une exécution unique
            schedule.every().day.at(pub_time).do(self._execute_and_cancel, job=job, account_name=account_name).tag(job_id)

    def _execute_and_cancel(self, job, account_name):
        """Publie la vidéo à l'heure prévue puis retire la tâche ponctuelle du scheduler."""
        print(f"\n🚀 [SCHEDULER] Heure atteinte ! Publication automatique du job : {job['job_id']} sur {account_name}")
        publish_scheduled_job(job, account_name)
        return schedule.CancelJob

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
                
            # On lance la préparation 30 minutes avant le premier créneau du compte
            prep_dt = datetime.strptime(earliest_start, "%H:%M") - timedelta(minutes=30)
            prep_time_str = prep_dt.strftime("%H:%M")

            print(f"   ⚙️  [{acc_name}] Heure de préparation calée à {prep_time_str}")
            
            # On planifie la préparation spécifique à CE compte
            schedule.every().day.at(prep_time_str).do(self.daily_orchestration, account_name=acc_name)

        print("==================================================\n")

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