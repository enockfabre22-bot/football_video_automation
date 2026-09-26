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

    def get_window_config(self):
        """Lit la fenêtre horaire depuis accounts/<account_name>/config.json."""
        cfg = self.pub_mgr.get_account_config(self.account_name)
        window = cfg.get("publication_window", {"start": "08:00", "end": "10:00"})
        return window.get("start", "08:00"), window.get("end", "10:00")

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
        leur publication à une heure aléatoire dans la fenêtre autorisée.
        """
        setup_logging()
        start_win, end_win = self.get_window_config()
        print(f"\n⏰ [SCHEDULER] Lancement du contrôle quotidien pour '{self.account_name}'")
        print(f"⏰ [SCHEDULER] Fenêtre de publication configurée : {start_win} -> {end_win}")

        ready_jobs = prepare_daily_videos(self.account_name)
        if not ready_jobs:
            print("⏰ [SCHEDULER] Aucun job à planifier aujourd'hui.")
            return

        publish_times = self.pick_random_times_in_window(start_win, end_win, len(ready_jobs))

        for job, pub_time in zip(ready_jobs, publish_times):
            job_id = job["job_id"]
            print(f"📅 [PLANIFICATION] Le job '{job_id}' est programmé pour publication à {pub_time}")
            
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
        start_win, end_win = self.get_window_config()
        
        # On lance la préparation des vidéos 30 minutes avant l'ouverture de la fenêtre
        prep_dt = datetime.strptime(start_win, "%H:%M") - timedelta(minutes=30)
        prep_time_str = prep_dt.strftime("%H:%M")

        print("==================================================")
        print(f"🤖 DÉMON SCHEDULER ACTIF (Compte : {self.account_name})")
        print(f"   - Heure de préparation des rendus : {prep_time_str}")
        print(f"   - Fenêtre de publication          : {start_win} -> {end_win}")
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
    sched = AutomationScheduler("tiktok_main")
    
    if "--daemon" in sys.argv:
        sched.start_daemon()
    else:
        start_w, end_w = sched.get_window_config()
        tirage = sched.pick_random_times_in_window(start_w, end_w, count=2)
        print(f"[TEST SCHEDULER] Fenêtre lue : {start_w} -> {end_w}")
        print(f"[TEST SCHEDULER] Exemple de 2 horaires tirés au sort pour demain : {tirage}")