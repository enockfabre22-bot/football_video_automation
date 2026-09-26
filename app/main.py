from pathlib import Path
from match_manager import MatchManager
from animation_manager import AnimationManager
from job_manager import JobManager
from publication_manager import PublicationManager

def run_daily_workflow(account_name="tiktok_main"):
    print("==================================================")
    print("🚀 DÉMARRAGE DU WORKFLOW D'AUTOMATISATION VIDÉO")
    print("==================================================")

    match_mgr = MatchManager()
    anim_mgr = AnimationManager()
    job_mgr = JobManager()
    pub_mgr = PublicationManager()

    # 1. Vérification du compte
    account_cfg = pub_mgr.get_account_config(account_name)
    if not account_cfg.get("enabled", False):
        print(f"[INFO] Le compte '{account_name}' est désactivé. Arrêt.")
        return
    platform = account_cfg.get("platform", "tiktok")

    # 2. Détection des matchs valides du lendemain
    valid_matches = match_mgr.get_valid_matches_for_tomorrow()
    if not valid_matches:
        print("[INFO] Aucun match valide à traiter pour demain.")
        return

    # 3. Traitement de chaque match
    for match in valid_matches:
        home, away, date_str = match["home"], match["away"], match["date"]
        print(f"\n--- Traitement du match : {home} VS {away} ({date_str}) ---")

        # Test 11 : Vérification anti-doublon
        if job_mgr.is_match_already_completed(date_str, home, away, account_name):
            print(f"[SKIP] Ce match a déjà été publié sur '{account_name}'. Ignoré.")
            continue

        # Test 15 : Vérification s'il existe un job en attente à reprendre
        job = job_mgr.find_existing_pending_job(date_str, home, away, account_name)
        
        if not job:
            anim_config = anim_mgr.select_animation(platform=platform)
            if not anim_config:
                print(f"[ERREUR] Aucune animation disponible pour la plateforme {platform}.")
                continue
            job = job_mgr.create_job(match, anim_config["name"], account_name)
            print(f"[JOB] Nouveau job créé : {job['job_id']}")
        else:
            print(f"[JOB] Reprise d'un job existant : {job['job_id']} (Statut actuel : {job['status']})")
            anims = anim_mgr.discover_animations(platform=platform)
            anim_config = next((a for a in anims if a["name"] == job["animation"]), None)

        job_id = job["job_id"]
        video_path = job.get("video_path")

        # 4. Génération de la vidéo (si elle n'est pas déjà prête)
        if job["status"] in ["PENDING", "RUNNING"] or not video_path or not Path(video_path).exists():
            job_mgr.update_status(job_id, "RUNNING")
            video_path = anim_mgr.execute_animation(match, anim_config, job_id)
            
            if not video_path:
                job_mgr.update_status(job_id, "FAILED", error="Échec lors de la génération de la vidéo")
                print(f"[JOB FAILED] Le job {job_id} a été déplacé dans jobs/failed/")
                continue
                
            job_mgr.update_status(job_id, "VIDEO_READY", video_path=video_path)
        else:
            print(f"[CACHE] Vidéo déjà prête et validée ({Path(video_path).name}), passage direct à la publication.")

        # 5. Génération des métadonnées (Test 12)
        metadata = pub_mgr.generate_metadata(match)
        job_mgr.update_status(job_id, "SCHEDULED", metadata=metadata)

        # 6. Publication (Test 13 : Dry-Run)
        pub_success = pub_mgr.publish_video(video_path, metadata, account_name)
        if pub_success:
            job_mgr.update_status(job_id, "PUBLISHED")
            print(f"✅ [SUCCÈS TOTAL] Job {job_id} terminé et classé dans jobs/completed/ !")
        else:
            job_mgr.update_status(job_id, "FAILED", error="Échec lors de la publication sur la plateforme")
            print(f"❌ [JOB FAILED] Échec de publication. Job déplacé dans jobs/failed/")

if __name__ == "__main__":
    run_daily_workflow()