import sys
import random
from datetime import datetime, timedelta
from pathlib import Path
from match_manager import MatchManager
from animation_manager import AnimationManager
from job_manager import JobManager
from publication_manager import PublicationManager
from ai_stock import AIStockManager
import time
import os

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

def resolve_ai_animation(anim_config, slot, account_name, anim_mgr, ai_stock):
    """
    Si l'animation choisie est une animation IA sans création prête dans le stock,
    bascule sur l'animation de secours du slot ('fallback_animation') ou, en stratégie
    'random', sur une autre animation. Les animations classiques sont renvoyées telles quelles.
    """
    if anim_config.get("type") != "ai":
        return anim_config

    anim_name = anim_config["name"]
    slot_id = slot.get("id", "unknown_slot")
    date_str = (datetime.now() + timedelta(days=1)).strftime("%Y-%m-%d")
    expected_job_id = JobManager.build_job_id(date_str, slot_id, "no_match", anim_name)

    if ai_stock.can_serve(anim_name, expected_job_id):
        print(f"  -> Animation IA '{anim_name}' : {ai_stock.count_ready(anim_name)} création(s) prête(s) en stock")
        return anim_config

    print(f"  -> [STOCK IA VIDE] Aucune création prête pour '{anim_name}'.")

    fallback = slot.get("fallback_animation")
    if fallback:
        print(f"  -> [FALLBACK] Utilisation de l'animation de secours '{fallback}'")
        fallback_cfg = anim_mgr.select_animation(account_name, strategy="fixed", target_animation=fallback)
        if fallback_cfg and fallback_cfg.get("type") == "ai":
            # Une animation de secours IA pourrait elle aussi être vide : on refuse l'enchaînement
            print(f"  -> [FALLBACK] '{fallback}' est une animation IA : elle ne peut pas servir de secours.")
            return None
        return fallback_cfg

    if slot.get("animation_strategy", "random") == "random":
        exclude = list(slot.get("exclude_animations", [])) + [anim_name]
        # On exclut aussi toutes les autres animations IA sans stock
        for candidate in anim_mgr.discover_animations(account_name):
            if candidate.get("type") == "ai" and ai_stock.count_ready(candidate["name"]) == 0:
                exclude.append(candidate["name"])
        print("  -> [FALLBACK] Nouveau tirage aléatoire parmi les animations disponibles")
        return anim_mgr.select_animation(account_name, strategy="random", exclude_animations=exclude)

    return None

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
    ai_stock = AIStockManager()

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
    base_dir = Path(__file__).resolve().parent.parent

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

        # 1.bis Animation IA : elle ne peut servir le créneau que si le studio IA a du stock prêt.
        # Sinon -> animation de secours (fallback_animation) ou, en mode random, une autre animation.
        anim_config = resolve_ai_animation(anim_config, slot, account_name, anim_mgr, ai_stock)
        if not anim_config:
            print(f"[INFO] Aucune animation disponible pour le slot '{slot_id}'. Créneau ignoré.")
            continue

        is_ai = anim_config.get("type") == "ai"
        requires_match = anim_config.get("requires_match_data", False)
        match_data = None
        
        # 2. Gestion de la dépendance au calendrier ou aux fichiers externes (Piano)
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
            home, away = slot_id, "no_match" 
            match_data = {"date": date_str, "home": home, "away": away, "resources": {}}

            # SÉLECTION DYNAMIQUE DES FICHIERS (MIDI, JPG, etc.)
            inputs = anim_config.get("inputs", [])
            
            # --- GESTION DES FICHIERS MIDI ---
            # Détecte si l'animation demande "midi" (classique) ou "midi2" (vertical)
            midi_key = "midi2" if "midi2" in inputs else "midi" if "midi" in inputs else None
            
            if midi_key:
                midi_dir = base_dir / "assets" / midi_key
                midis = list(midi_dir.glob("*.mid"))
                if not midis:
                    print(f"[ERREUR] Aucun fichier .mid trouvé dans assets/{midi_key}/")
                    continue
                chosen_midi = random.choice(midis)
                match_data["resources"]["midi_file"] = str(chosen_midi)
                
                # Extraction du nom du fichier
                stem = chosen_midi.stem
                match_data["song_title"] = stem
                if " - " in stem:
                    parts = stem.split(" - ", 1)
                    match_data["song_author"] = parts[1].strip()
                else:
                    match_data["song_author"] = "Unknown"
                    
                print(f"  -> Audio sélectionné : {match_data['song_title']} (Dossier: {midi_key})")

            # --- GESTION DES FONDS D'ÉCRAN (JPG) ---
            # Détecte si l'animation demande "jpg" (horizontal) ou "jpg_portrait" (vertical)
            jpg_key = "jpg_portrait" if "jpg_portrait" in inputs else "jpg" if "jpg" in inputs else None
            
            if jpg_key:
                jpg_dir = base_dir / "assets" / jpg_key
                jpgs = list(jpg_dir.glob("*.jpg"))
                if not jpgs:
                    print(f"[ERREUR] Aucune image .jpg trouvée dans assets/{jpg_key}/")
                    continue
                chosen_jpg = random.choice(jpgs)
                match_data["resources"]["bg_image"] = str(chosen_jpg)
                print(f"  -> Fond sélectionné : {chosen_jpg.name} (Dossier: {jpg_key})")

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

        # 3.bis Animation IA : réservation d'une création du stock pour ce job
        ai_creation = None
        if is_ai:
            reserved = ai_stock.reserve(anim_config["name"], job_id)
            if not reserved:
                job_mgr.update_status(job_id, "FAILED", error="Stock IA vide au moment de la réservation")
                print(f"[JOB FAILED] Stock IA vide pour '{anim_config['name']}'. Créneau ignoré.")
                continue
            ai_creation = reserved[1]

        # 4. Génération des métadonnées
        metadata = job.get("metadata")
        if not metadata:
            extra = None
            if ai_creation:
                extra = {
                    "ai_title": ai_creation.get("title", ""),
                    "ai_concept": ai_creation.get("pitch", ""),
                }
            metadata = anim_mgr.generate_job_metadata(
                anim_config, account_cfg, match_data if requires_match else None, extra_placeholders=extra
            )

            if ai_creation:
                metadata["ai_generated"] = True
                metadata["ai_animation"] = anim_config["name"]
                metadata["ai_creation_id"] = ai_creation.get("creation_id")
                overrides = anim_config.get("ai", {}).get("publish_overrides")
                if overrides:
                    metadata["publish_overrides"] = overrides
            
            # Injection des données musicales dans les métadonnées
            if not requires_match and "song_title" in match_data:
                song_title = match_data["song_title"]
                metadata["song_title"] = song_title
                metadata["song_author"] = match_data["song_author"]
                
                # AJOUT : On glisse les fichiers dans les métadonnées pour être sûr que Processing les lise !
                if "resources" in match_data:
                    metadata["midi_file"] = match_data["resources"].get("midi_file", "")
                    metadata["bg_image"] = match_data["resources"].get("bg_image", "")
                
                # Remplacement de {midi} dans les textes configurés
                if "title" in metadata:
                    metadata["title"] = metadata["title"].replace("{midi}", song_title)
                if "description" in metadata:
                    metadata["description"] = metadata["description"].replace("{midi}", song_title)
                if "youtube_tags" in metadata:
                    metadata["youtube_tags"] = [tag.replace("{midi}", song_title) for tag in metadata["youtube_tags"]]
            
            job["metadata"] = metadata

        # 5. Exécution de l'animation
        if job["status"] in ["PENDING", "RUNNING"] or not video_path or not Path(video_path).exists():
            job_mgr.update_status(job_id, "RUNNING")
            
            # Injection du job_id et des metadata (On envoie match_data même si requires_match est False)
            video_path = anim_mgr.execute_animation(match_data, anim_config, job_id, job_metadata=metadata)
            
            if not video_path:
                job_mgr.update_status(job_id, "FAILED", error="Échec lors de la génération de la vidéo")
                print(f"[JOB FAILED] Le job {job_id} a été déplacé dans jobs/failed/")
                if is_ai:
                    ai_stock.release(anim_config["name"], job_id, reason="échec de livraison de la vidéo")
            
                # --- NOUVEAU : NETTOYAGE SI LA GÉNÉRATION PLANTE ---
                temp_dir = Path("output/temporary") / job_id
                if temp_dir.exists():
                    import shutil
                    shutil.rmtree(temp_dir)
                    print(f"[CLEANUP FATAL] Dossier temporaire nettoyé après crash de génération : {temp_dir}")
                # -----------------------------------------------------
                continue
                
            job_mgr.update_status(job_id, "VIDEO_READY", video_path=video_path)
        else:
            print(f"[CACHE] Vidéo déjà prête et validée ({Path(video_path).name}).")

        # 5.bis Récupération de la miniature personnalisée (si générée par Processing)
        # On cherche dans le dossier temporaire du job
        job_temp_dir = base_dir / "output" / "temporary" / job_id
        possible_thumbnail = job_temp_dir / "thumbnail_source.jpg"
        if possible_thumbnail.exists():
            metadata["thumbnail_path"] = str(possible_thumbnail)
            print(f" [INFO] Miniature détectée et associée pour le job : {job_id}")

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

    # 0. Suivi du stock IA (ne doit jamais faire échouer la publication elle-même)
    ai_anim = metadata.get("ai_animation")
    if ai_anim:
        try:
            ai_stock = AIStockManager()
            if pub_success:
                platform = pub_mgr.get_account_config(account_name).get("platform", "unknown")
                ai_stock.mark_published(ai_anim, job_id, account_name, platform, url=metadata.get("published_url"))
            else:
                ai_stock.release(ai_anim, job_id, reason="échec de publication")
        except Exception as e:
            print(f"[AVERTISSEMENT] Mise à jour du stock IA impossible : {e}")

    # 1. GESTION DU STATUT (Succès ou Échec)
    if pub_success:
        job_mgr.update_status(job_id, "PUBLISHED")
        print(f"✅ [SUCCÈS TOTAL] Job {job_id} terminé et classé dans jobs/completed/ !")
        print("Pause de 30 secondes pour éviter le blocage API...")
        time.sleep(30)
    else:
        job_mgr.update_status(job_id, "FAILED", error="Échec lors de la publication sur la plateforme")
        print(f"❌ [JOB FAILED] Échec de publication. Job déplacé dans jobs/failed/")

    # 2. NETTOYAGE GARANTI (S'exécute dans TOUS LES CAS : Succès ou Échec)
    if os.path.exists(video_path):
        os.remove(video_path)
        print(f"[CLEANUP] Fichier vidéo supprimé pour libérer de l'espace : {video_path}")
        
    temp_dir = Path("output/temporary") / job_id
    if temp_dir.exists():
        import shutil
        shutil.rmtree(temp_dir)
        print(f"[CLEANUP] Dossier temporaire nettoyé : {temp_dir}")
    
    # 3. RETOUR DU RÉSULTAT
    return pub_success

def discard_job(job, reason):
    """
    Abandonne un job sans le publier (ex. créneau expiré pendant un arrêt du service) :
    job -> jobs/failed/, vidéo et dossier temporaire supprimés, création IA remise en stock.
    """
    job_id = job["job_id"]
    job_mgr = JobManager()
    job_mgr.update_status(job_id, "FAILED", error=reason)
    print(f"❌ [JOB FAILED] {job_id} : {reason}")

    ai_anim = (job.get("metadata") or {}).get("ai_animation")
    if ai_anim:
        try:
            AIStockManager().release(ai_anim, job_id, reason="job expiré")
        except Exception as e:
            print(f"[AVERTISSEMENT] Mise à jour du stock IA impossible : {e}")

    video_path = job.get("video_path")
    if video_path and os.path.exists(video_path):
        os.remove(video_path)
        print(f"[CLEANUP] Fichier vidéo supprimé : {video_path}")
    job_mgr.cleanup_temp(job_id)

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