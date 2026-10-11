import sys
from datetime import datetime, timedelta
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
    Phase 1 : Itère sur les créneaux (slots) du compte, sélectionne l'animation,
    associe un match si requis, génère les métadonnées spécifiques, puis produit la vidéo.
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
    if not account_cfg or not account_cfg.get("enabled", False):
        print(f"[INFO] Le compte '{account_name}' est introuvable ou désactivé. Arrêt.")
        return []

    # Chargement global des matchs pour demain
    valid_matches = match_mgr.get_valid_matches_for_tomorrow()
    match_index = 0

    schedule_slots = account_cfg.get("schedule_slots", [])
    if not schedule_slots:
        default_window = account_cfg.get("publication_window", {"start": "08:00", "end": "10:00"})
        schedule_slots = [{
            "id": "default_slot",
            "animation_strategy": "random",
            "window": default_window
        }]

    ready_jobs = []

    for slot in schedule_slots:
        slot_id = slot.get("id", "unknown_slot")
        print(f"\n--- Évaluation du créneau : [{slot_id}] ---")

        strategy = slot.get("animation_strategy", "random")
        target_anim = slot.get("target_animation")
        exclude_anims = slot.get("exclude_animations", [])

        # 1. Sélection de l'animation
        anim_config = anim_mgr.select_animation(
            account_name=account_name, 
            strategy=strategy, 
            target_animation=target_anim, 
            exclude_animations=exclude_anims
        )
        
        if not anim_config:
            print(f"[ERREUR] Impossible de satisfaire la stratégie du slot '{slot_id}'. Créneau ignoré.")
            continue

        requires_match = anim_config.get("requires_match_data", False)
        match_data = None
        
        # 2. Gestion de la dépendance au calendrier
        if requires_match:
            if match_index >= len(valid_matches):
                print(f"[INFO] Plus de matchs disponibles pour remplir le slot '{slot_id}'. Ignoré.")
                continue
            match_data = valid_matches[match_index]
            match_index += 1
            home, away, date_str = match_data["home"], match_data["away"], match_data["date"]
            print(f"  -> Match assigné : {home} VS {away} ({date_str})")
        else:
            print(f"  -> Animation '{anim_config['name']}' sélectionnée (Ne requiert aucun match)")
            date_str = (datetime.now() + timedelta(days=1)).strftime("%Y-%m-%d")
            # Faux match_data pour préserver la structure du JobManager sans le casser
            home, away = slot_id, "no_match" 
            match_data = {"date": date_str, "home": home, "away": away, "resources": {}}

        # 3. Création ou récupération du Job (Anti-doublons)
        if job_mgr.is_match_already_completed(date_str, home, away, account_name):
            print(f"[SKIP] La tâche '{home}_VS_{away}' a déjà été complétée sur '{account_name}'. Ignorée.")
            continue

        job = job_mgr.find_existing_pending_job(date_str, home, away, account_name)
        
        if not job:
            job = job_mgr.create_job(match_data, anim_config["name"], account_name)
            print(f"[JOB] Nouveau job créé : {job['job_id']}")
        else:
            print(f"[JOB] Reprise du job existant : {job['job_id']} (Statut : {job['status']})")

        job_id = job["job_id"]
        video_path = job.get("video_path")

        # 4. Génération des métadonnées (Titre, description, hashtags piochés au hasard)
        metadata = job.get("metadata")
        if not metadata:
            metadata = anim_mgr.generate_job_metadata(anim_config, account_cfg, match_data if requires_match else None)
            job["metadata"] = metadata

        # 5. Exécution de l'animation
        if job["status"] in ["PENDING", "RUNNING"] or not video_path or not Path(video_path).exists():
            job_mgr.update_status(job_id, "RUNNING")
            
            # Injection de match_data complet si requis, ou None, ET des metadata générées
            video_path = anim_mgr.execute_animation(match_data if requires_match else None, anim_config, job_id, job_metadata=metadata)
            
            if not video_path:
                job_mgr.update_status(job_id, "FAILED", error="Échec lors de la génération de la vidéo")
                print(f"[JOB FAILED] Le job {job_id} a été déplacé dans jobs/failed/")
                continue
                
            job_mgr.update_status(job_id, "VIDEO_READY", video_path=video_path)
        else:
            print(f"[CACHE] Vidéo déjà prête et validée ({Path(video_path).name}).")

        # 6. Planification finale
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
    import sys
    account_to_test = "tiktok_main"
    if len(sys.argv) > 1:
        account_to_test = sys.argv[1]
    run_daily_workflow(account_to_test)