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
    Associe chaque match à un slot de publication spécifique défini dans la configuration du compte.
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

    valid_matches = match_mgr.get_valid_matches_for_tomorrow()
    if not valid_matches:
        print("[INFO] Aucun match valide à traiter pour demain.")
        return []

    # Récupération de la grille de programmation (ou création d'un slot par défaut pour la rétrocompatibilité)
    schedule_slots = account_cfg.get("schedule_slots", [])
    if not schedule_slots:
        default_window = account_cfg.get("publication_window", {"start": "08:00", "end": "10:00"})
        schedule_slots = [{
            "id": "default_slot",
            "animation_strategy": "random",
            "window": default_window
        }]

    ready_jobs = []

    # On associe chaque match à un slot disponible
    for index, match in enumerate(valid_matches):
        if index >= len(schedule_slots):
            print(f"[INFO] Plus de matchs que de créneaux disponibles. Le match {match['home']} VS {match['away']} est ignoré pour aujourd'hui.")
            break
            
        slot = schedule_slots[index]
        home, away, date_str = match["home"], match["away"], match["date"]
        print(f"\n--- Préparation du match : {home} VS {away} ({date_str}) [Slot: {slot.get('id', 'N/A')}] ---")

        if job_mgr.is_match_already_completed(date_str, home, away, account_name):
            print(f"[SKIP] Ce match a déjà été publié sur '{account_name}'. Ignoré.")
            continue

        job = job_mgr.find_existing_pending_job(date_str, home, away, account_name)
        
        if not job:
            # Extraction des règles du slot
            strategy = slot.get("animation_strategy", "random")
            target_anim = slot.get("target_animation")
            exclude_anims = slot.get("exclude_animations", [])

            # Sélection de l'animation en respectant la stratégie du slot
            anim_config = anim_mgr.select_animation(
                account_name=account_name, 
                strategy=strategy, 
                target_animation=target_anim, 
                exclude_animations=exclude_anims
            )
            
            if not anim_config:
                print(f"[ERREUR] Impossible de satisfaire la stratégie du slot '{slot.get('id')}'. Match ignoré.")
                continue
                
            job = job_mgr.create_job(match, anim_config["name"], account_name)
            print(f"[JOB] Nouveau job créé : {job['job_id']} (Animation: {anim_config['name']})")
        else:
            print(f"[JOB] Reprise d'un job existant : {job['job_id']} (Statut : {job['status']})")
            anims = anim_mgr.discover_animations(account_name=account_name)
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

        # Génération des métadonnées ET injection de la fenêtre de publication du slot
        metadata = job.get("metadata") or pub_mgr.generate_metadata(match)
        metadata["publish_window"] = slot.get("window", {"start": "08:00", "end": "20:00"})
        
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