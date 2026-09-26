import sys
from datetime import datetime
from pathlib import Path
from match_manager import MatchManager
from animation_manager import AnimationManager
from job_manager import JobManager
from publication_manager import PublicationManager

class FileLogger:
    """Redirige automatiquement tous les print() vers la console ET vers logs/automation.log."""
    def __init__(self, log_file_path):
        self.terminal = sys.stdout
        self.log_file = open(log_file_path, "a", encoding="utf-8")

    def write(self, message):
        self.terminal.write(message)
        if message.strip():
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            for line in message.rstrip().split("\n"):
                self.log_file.write(f"[{timestamp}] {line}\n")
            self.log_file.flush()

    def flush(self):
        self.terminal.flush()
        self.log_file.flush()

def setup_logging():
    base_dir = Path(__file__).resolve().parent.parent
    log_dir = base_dir / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "automation.log"
    if not isinstance(sys.stdout, FileLogger):
        sys.stdout = FileLogger(log_file)

def prepare_daily_videos(account_name="tiktok_main"):
    """
    Phase 1 : Détecte les matchs du lendemain, génère les vidéos et prépare les métadonnées.
    Retourne la liste des jobs prêts à être publiés (statut SCHEDULED).
    """
    setup_logging()
    print("==================================================")
    print(f"🎬 PHASE 1 : PRÉPARATION DES VIDÉOS ({account_name})")
    print("==================================================")

    match_mgr = MatchManager()
    anim_mgr = AnimationManager()
    job_mgr = JobManager()
    pub_mgr = PublicationManager()

    account_cfg = pub_mgr.get_account_config(account_name)
    if not account_cfg.get("enabled", False):
        print(f"[INFO] Le compte '{account_name}' est désactivé. Arrêt.")
        return []
    platform = account_cfg.get("platform", "tiktok")

    valid_matches = match_mgr.get_valid_matches_for_tomorrow()
    if not valid_matches:
        print("[INFO] Aucun match valide à traiter pour demain.")
        return []

    ready_jobs = []

    for match in valid_matches:
        home, away, date_str = match["home"], match["away"], match["date"]
        print(f"\n--- Préparation du match : {home} VS {away} ({date_str}) ---")

        if job_mgr.is_match_already_completed(date_str, home, away, account_name):
            print(f"[SKIP] Ce match a déjà été publié sur '{account_name}'. Ignoré.")
            continue

        job = job_mgr.find_existing_pending_job(date_str, home, away, account_name)
        
        if not job:
            anim_config = anim_mgr.select_animation(platform=platform)
            if not anim_config:
                print(f"[ERREUR] Aucune animation disponible pour la plateforme {platform}.")
                continue
            job = job_mgr.create_job(match, anim_config["name"], account_name)
            print(f"[JOB] Nouveau job créé : {job['job_id']}")
        else:
            print(f"[JOB] Reprise d'un job existant : {job['job_id']} (Statut : {job['status']})")
            anims = anim_mgr.discover_animations(platform=platform)
            anim_config = next((a for a in anims if a["name"] == job["animation"]), None)

        job_id = job["job_id"]
        video_path = job.get("video_path")

        if job["status"] in ["PENDING", "RUNNING"] or not video_path or not Path(video_path).exists():
            job_mgr.update_status(job_id, "RUNNING")
            video_path = anim_mgr.execute_animation(match, anim_config, job_id)
            
            if not video_path:
                job_mgr.update_status(job_id, "FAILED", error="Échec lors de la génération de la vidéo")
                print(f"[JOB FAILED] Le job {job_id} a été déplacé dans jobs/failed/")
                continue
                
            job_mgr.update_status(job_id, "VIDEO_READY", video_path=video_path)
        else:
            print(f"[CACHE] Vidéo déjà prête et validée ({Path(video_path).name}).")

        metadata = job.get("metadata") or pub_mgr.generate_metadata(match)
        updated_job = job_mgr.update_status(job_id, "SCHEDULED", video_path=video_path, metadata=metadata)
        ready_jobs.append(updated_job)

    return ready_jobs

def publish_scheduled_job(job, account_name="tiktok_main"):
    """Phase 2 : Publie un job spécifique déjà préparé."""
    setup_logging()
    job_mgr = JobManager()
    pub_mgr = PublicationManager()

    job_id = job["job_id"]
    video_path = job["video_path"]
    metadata = job["metadata"]

    pub_success = pub_mgr.publish_video(video_path, metadata, account_name)
    if pub_success:
        job_mgr.update_status(job_id, "PUBLISHED")
        print(f"✅ [SUCCÈS TOTAL] Job {job_id} terminé et classé dans jobs/completed/ !")
        return True
    else:
        job_mgr.update_status(job_id, "FAILED", error="Échec lors de la publication sur la plateforme")
        print(f"❌ [JOB FAILED] Échec de publication. Job déplacé dans jobs/failed/")
        return False

def run_daily_workflow(account_name="tiktok_main"):
    """Exécute immédiatement la préparation suivie de la publication (Mode direct / Test)."""
    ready_jobs = prepare_daily_videos(account_name)
    for job in ready_jobs:
        publish_scheduled_job(job, account_name)

if __name__ == "__main__":
    run_daily_workflow()