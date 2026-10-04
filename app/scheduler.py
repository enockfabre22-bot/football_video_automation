import time
import random
import schedule
from datetime import datetime, timedelta
from main import prepare_daily_videos, publish_scheduled_job, setup_logging
from publication_manager import PublicationManager

class AutomationScheduler:
    def __init__(self, account_name="tiktok_main"):
        self.account_name = account_name
        self.pub_mgr = PublicationManager()

    def pick_random_times_in_window(self, start_str, end_str, count=1):
        """
        Tire au sort 'count' horaires (HH:MM) répartis dans la fenêtre configurée.
        Exemple : entre 08:00 et 10:00 -> ['08:24', '09:18']
        """
        fmt = "%H:%M"
        start_dt = datetime.strptime(start_str, fmt)
        end_dt = datetime.strptime(end_str, fmt)
        
        total_minutes = int((end_dt - start_dt).total_seconds() // 60)
        if total_minutes <= 0:
            return [start_str] * count

        chosen_offsets = sorted(random.sample(range(total_minutes + 1), min(count, total_minutes + 1)))
        return [(start_dt + timedelta(minutes=offset)).strftime(fmt) for offset in chosen_offsets]

    def daily_orchestration(self):
        """
        Job quotidien : prépare les vidéos des matchs du lendemain et planifie
        leur publication selon la fenêtre (slot) associée au job préparé.
        """
        setup_logging()
        print(f"\n⏰ [SCHEDULER] Lancement du contrôle quotidien pour '{self.account_name}'")

        ready_jobs = prepare_daily_videos(self.account_name)
        if not ready_jobs:
            print("⏰ [SCHEDULER] Aucun job à planifier aujourd'hui.")
            return

        for job in ready_jobs:
            job_id = job["job_id"]
            
            # --- MODIFICATION ICI : On lit la fenêtre depuis les métadonnées du job ---
            window = job.get("metadata", {}).get("publish_window", {"start": "08:00", "end": "10:00"})
            start_win = window.get("start", "08:00")
            end_win = window.get("end", "10:00")
            
            # Tirage d'une seule heure pour ce job spécifique
            pub_time = self.pick_random_times_in_window(start_win, end_win, 1)[0]
            
            print(f"📅 [PLANIFICATION] Le job '{job_id}' est programmé pour publication à {pub_time} (Créneau: {start_win}-{end_win})")
            
            # Planifie une exécution unique (CancelJob après envoi)
            schedule.every().day.at(pub_time).do(self._execute_and_cancel, job=job).tag(job_id)

    def _execute_and_cancel(self, job):
        """Publie la vidéo à l'heure prévue puis retire la tâche ponctuelle du scheduler."""
        print(f"\n🚀 [SCHEDULER] Heure atteinte ! Publication automatique du job : {job['job_id']}")
        publish_scheduled_job(job, self.account_name)
        return schedule.CancelJob

    def start_daemon(self):
        """Lance la boucle infinie (pour le service VPS 24h/24)."""
        setup_logging()
        
        # On lit la config globale juste pour savoir quand lancer l'orchestration quotidienne
        cfg = self.pub_mgr.get_account_config(self.account_name)
        
        # Si on a des slots, on prend le début du premier slot pour caler l'heure de préparation
        slots = cfg.get("schedule_slots", [])
        if slots:
            earliest_start = min([s.get("window", {}).get("start", "08:00") for s in slots])
        else:
            window = cfg.get("publication_window", {"start": "08:00", "end": "10:00"})
            earliest_start = window.get("start", "08:00")
            
        # On lance la préparation des vidéos 30 minutes avant le premier slot
        prep_dt = datetime.strptime(earliest_start, "%H:%M") - timedelta(minutes=30)
        prep_time_str = prep_dt.strftime("%H:%M")

        print("==================================================")
        print(f"🤖 DÉMON SCHEDULER ACTIF (Compte : {self.account_name})")
        print(f"   - Heure de préparation des rendus : {prep_time_str}")
        print("==================================================")

        schedule.every().day.at(prep_time_str).do(self.daily_orchestration)

        while True:
            schedule.run_pending()
            time.sleep(20)

# ==========================================
# BLOC DE TEST DU SCHEDULER
# ==========================================
if __name__ == "__main__":
    import sys
    # <-- Tu peux changer "tiktok_main" par "youtube_main" ici pour tester ton compte YouTube
    account_to_run = "tiktok_main" 
    sched = AutomationScheduler(account_to_run)
    
    if "--daemon" in sys.argv:
        sched.start_daemon()
    else:
        # Simple test de la fonction de tirage aléatoire
        tirage = sched.pick_random_times_in_window("08:00", "10:00", count=2)
        print(f"[TEST SCHEDULER] Compte ciblé : {account_to_run}")
        print(f"[TEST SCHEDULER] Exemple de 2 horaires tirés au sort pour le créneau 08:00-10:00 : {tirage}")