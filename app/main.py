import sys
import random
from datetime import datetime, timedelta
from pathlib import Path
from match_manager import MatchManager
from animation_manager import AnimationManager
from job_manager import JobManager
from publication_manager import PublicationManager
from ai_stock import AIStockManager
import song_meta
import midi_library
import football_data
import football_fixtures
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
            # Disque plein : on perd la ligne de log plutôt que de faire planter tout le service
            try:
                for line in message.rstrip().split("\n"):
                    self.log_file.write(f"[{timestamp}] {line}\n")
                self.log_file.flush()
            except OSError:
                pass

    def flush(self):
        self.terminal.flush()
        try:
            self.log_file.flush()
        except OSError:
            pass

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
    expected_job_id = JobManager.build_job_id(date_str, slot_id, "no_match", anim_name, account_name)

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

def metadata_config(anim_config, match_data):
    """
    Animation foot avec le championnat ENTIER : on ajoute les titres / descriptions « classement prédit »
    (titles_full_pack, descriptions_full_pack) aux pools habituels. Sinon la config est rendue telle quelle.
    """
    lineup = ((match_data or {}).get("resources") or {}).get("football_lineup")
    if not (lineup and lineup.get("full_pack")):
        return anim_config
    pools = dict(anim_config.get("metadata", {}))
    for key in ("titles", "descriptions"):
        if pools.get(f"{key}_full_pack"):
            pools[key] = pools[f"{key}_full_pack"] + list(pools.get(key, []))
    return dict(anim_config, metadata=pools)


def expand_match_slots(slots):
    """
    Un créneau de match avec "max_matches": N devient N créneaux (id, id_2, ...). Seul le premier tire une
    autre animation s'il n'y a pas de match ; les suivants sont simplement sautés.
    """
    out = []
    for slot in slots:
        out.append(slot)
        for k in range(2, int(slot.get("max_matches", 1)) + 1):
            extra = dict(slot)
            extra["id"] = f"{slot['id']}_{k}"
            extra["skip_without_match"] = True
            out.append(extra)
    return out


def kickoff_window(match, minutes_before, margin=10):
    """
    Fenêtre de publication {start, end} centrée sur (coup d'envoi - minutes_before).
    None : pas d'heure connue (on garde la fenêtre du créneau). False : trop tard pour publier avant le match.
    """
    if not minutes_before or not match:
        return None
    kick = None
    if match.get("kickoff"):
        try:
            kick = datetime.fromisoformat(match["kickoff"]).astimezone().replace(tzinfo=None)
        except ValueError:
            kick = None
    elif match.get("time") and match.get("date"):
        try:
            kick = datetime.strptime(f"{match['date']} {match['time']}", "%Y-%m-%d %H:%M")
        except ValueError:
            kick = None
    if kick is None:
        return None
    target = kick - timedelta(minutes=minutes_before)
    now = datetime.now()
    if target.date() != now.date() and target < now:
        return False
    # préparation tardive : on publie dès que possible, tant qu'il reste au moins 45 min avant le match
    earliest = now + timedelta(minutes=25)
    if target - timedelta(minutes=margin) < earliest:
        if kick - earliest < timedelta(minutes=45):
            return False
        target = earliest + timedelta(minutes=margin)
    start, end = target - timedelta(minutes=margin), target + timedelta(minutes=margin)
    if end.date() != start.date():      # pas de fenêtre à cheval sur minuit
        end = start.replace(hour=23, minute=59)
    return {"start": start.strftime("%H:%M"), "end": end.strftime("%H:%M")}


def apply_song_metadata(metadata, match_data):
    """
    Données du morceau dans les métadonnées : chemins (relus par le moteur) et remplacement des repères
    {midi} {title} {artist} {artist_tag} {easy} dans titre / description / hashtags / tags (voir song_meta.py).
    Partagé par la préparation quotidienne et scripts/publish_now.py.
    """
    if "song_title" not in match_data:
        return metadata
    song_title = match_data["song_title"]
    metadata["song_title"] = song_title
    metadata["song_author"] = match_data.get("song_author", "")
    if "resources" in match_data:
        metadata["midi_file"] = match_data["resources"].get("midi_file", "")
        metadata["bg_image"] = match_data["resources"].get("bg_image", "")
    song = match_data.get("song") or song_meta.parse_song_name(song_title)
    metadata["song_name"], metadata["song_artist"] = song["title"], song["artist"]
    if "title" in metadata:
        metadata["title"] = song_meta.fill(metadata["title"], song)
    if "description" in metadata:
        metadata["description"] = song_meta.fill(metadata["description"], song)
        if song.get("credit"):
            metadata["description"] = (metadata["description"] + "\n\n" + song["credit"]).strip()
    if "hashtags" in metadata:
        metadata["hashtags"] = song_meta.fill_list(metadata["hashtags"], song, prefix="#")
    if "youtube_tags" in metadata:
        metadata["youtube_tags"] = song_meta.fill_list(metadata["youtube_tags"], song)
    return metadata


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

    schedule_slots = account_cfg.get("schedule_slots", [])
    if not schedule_slots:
        default_window = account_cfg.get("publication_window", {"start": "08:00", "end": "10:00"})
        schedule_slots = [{
            "id": "default_slot",
            "animation_strategy": "random",
            "window": default_window
        }]
    schedule_slots = expand_match_slots(schedule_slots)

    # Matchs du JOUR (le duel est publié le jour même, avant le coup d'envoi) : calendrier automatique
    # (API football-data.org, mis en cache) + calendrier manuel data/matches.json (prioritaire)
    football_fixtures.sync()
    valid_matches = match_mgr.get_valid_matches(datetime.now().strftime("%Y-%m-%d"))
    # les gros matchs passent en premier ; ceux dont l'heure de publication est déjà dépassée sont écartés
    lead = max([s.get("publish_before_kickoff_min", 0) for s in schedule_slots] or [0])
    valid_matches = [m for m in valid_matches if kickoff_window(m, lead) is not False]
    valid_matches.sort(key=lambda m: not m.get("big_match", False))
    if valid_matches:
        print("[MATCHS] " + " | ".join(f"{m['home']} - {m['away']} {m.get('time', '')}".strip() for m in valid_matches))
    match_index = 0
    duel_kickoffs = []
    used_today = []      # animations déjà tirées dans cette préparation

    ready_jobs = []
    base_dir = Path(__file__).resolve().parent.parent

    for slot in schedule_slots:
        slot_id = slot.get("id", "unknown_slot")
        print(f"\n--- Évaluation du créneau : [{slot_id}] ---")

        strategy = slot.get("animation_strategy", "random")
        target_anim = slot.get("target_animation")
        exclude_anims = slot.get("exclude_animations", [])

        # 1. Sélection de l'animation (tirage : pas deux fois le même format le même jour sur ce compte)
        anim_config = None
        if strategy == "random" and used_today:
            anim_config = anim_mgr.select_animation(account_name=account_name, strategy="random",
                                                    exclude_animations=list(exclude_anims) + used_today)
        if not anim_config:
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
        
        # 1.ter Animation de match sans match disponible : on tire une animation qui n'en demande pas
        # (sauf si le créneau l'interdit avec "skip_without_match": true)
        if requires_match and match_index >= len(valid_matches) and not slot.get("skip_without_match"):
            no_match = [a["name"] for a in anim_mgr.discover_animations(account_name) if a.get("requires_match_data")]
            print(f"  -> [FALLBACK] Plus de match pour '{anim_config['name']}' : tirage d'une autre animation")
            anim_config = anim_mgr.select_animation(account_name, strategy="random",
                                                    exclude_animations=list(exclude_anims) + no_match + used_today) \
                or anim_mgr.select_animation(account_name, strategy="random",
                                             exclude_animations=list(exclude_anims) + no_match)
            anim_config = resolve_ai_animation(anim_config, slot, account_name, anim_mgr, ai_stock) if anim_config else None
            if not anim_config:
                print(f"[INFO] Aucune animation sans match disponible pour le slot '{slot_id}'. Créneau ignoré.")
                continue
            is_ai = anim_config.get("type") == "ai"
            requires_match = anim_config.get("requires_match_data", False)

        extra_meta = {}
        # 2. Gestion de la dépendance au calendrier ou aux fichiers externes (Piano)
        if requires_match:
            if match_index >= len(valid_matches):
                print(f"[INFO] Plus de matchs disponibles pour remplir le slot '{slot_id}'. Ignoré.")
                continue
            match_data = valid_matches[match_index]
            match_index += 1
            home, away, date_str = match_data["home"], match_data["away"], match_data["date"]
            print(f"  -> Match assigné : {home} VS {away} ({date_str})")
            extra_meta = football_data.match_placeholders(match_data)
        else:
            print(f"  -> Animation '{anim_config['name']}' sélectionnée (Ne requiert aucun match)")
            date_str = (datetime.now() + timedelta(days=1)).strftime("%Y-%m-%d")
            home, away = slot_id, "no_match" 
            match_data = {"date": date_str, "home": home, "away": away, "resources": {}}

            # SÉLECTION DYNAMIQUE DES FICHIERS (MIDI, JPG, etc.)
            inputs = anim_config.get("inputs", [])

            # --- ÉQUIPES DES ANIMATIONS FOOT À N ÉQUIPES (data/football_packs.json, app/football_data.py) ---
            if "football_pack" in inputs:
                lineup = football_data.choose_lineup(account_name, anim_config, date_str)
                if not lineup:
                    print("[ERREUR] Aucune composition d'équipes possible pour ce créneau.")
                    continue
                football_data.remember(account_name, anim_config["name"], lineup["pack"], date_str)
                match_data["resources"]["football_lineup"] = lineup
                extra_meta = football_data.lineup_placeholders(lineup)
                print(f"  -> Équipes : {lineup['competition']} ({lineup['count']}) : "
                      f"{', '.join(t['name'] for t in lineup['teams'])}")

            # --- BIBLIOTHÈQUE MIDI (assets/library, voir app/midi_library.py) ---
            # Morceau choisi par catégorie et notoriété, sans répétition récente sur ce compte.
            if "library" in inputs:
                entry = midi_library.choose_song(account_name, anim_config.get("library", {}))
                if not entry:
                    print("[ERREUR] Aucun morceau disponible dans la bibliothèque MIDI pour ce créneau.")
                    continue
                midi_library.remember(account_name, entry, date_str)
                match_data["resources"]["midi_file"] = entry["path"]
                match_data["song_title"] = Path(entry["path"]).stem      # "Artiste - Titre[ - EASY]" (relu par le moteur)
                match_data["song"] = {"title": entry["title"], "artist": entry["artist"],
                                      "easy": entry.get("easy", False), "credit": entry.get("credit", "")}
                match_data["song_author"] = entry["artist"] or "Unknown"
                print(f"  -> Morceau : {entry['title']} — {entry['artist']} "
                      f"[{entry['category']}, notoriété {entry['popularity']}]")

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

        used_today.append(anim_config["name"])

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
            # le job repris garde SON animation et SES équipes (titres déjà rédigés pour elles)
            if job.get("animation") and job["animation"] != anim_config["name"]:
                same = anim_mgr.select_animation(account_name, strategy="fixed", target_animation=job["animation"])
                if same:
                    anim_config = same
                    is_ai = anim_config.get("type") == "ai"
            old_lineup = ((job.get("match") or {}).get("resources") or {}).get("football_lineup")
            if old_lineup:
                match_data["resources"]["football_lineup"] = old_lineup

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
            extra = dict(extra_meta) or None    # repères foot : {competition} {count} {noun}...
            if ai_creation:
                extra = {
                    "ai_title": ai_creation.get("title", ""),
                    "ai_concept": ai_creation.get("pitch", ""),
                    "ai_description": (ai_creation.get("metadata") or {}).get("description", ""),
                }
            metadata = anim_mgr.generate_job_metadata(
                metadata_config(anim_config, match_data), account_cfg, match_data if requires_match else None,
                extra_placeholders=extra
            )

            if ai_creation:
                # Titre / description / tags rédigés par le studio IA pour CETTE vidéo (prioritaires)
                ai_meta = ai_creation.get("metadata") or {}
                for key in ("title", "description", "hashtags", "youtube_tags"):
                    if ai_meta.get(key):
                        metadata[key] = ai_meta[key]
                metadata["ai_generated"] = True
                metadata["ai_animation"] = anim_config["name"]
                metadata["ai_creation_id"] = ai_creation.get("creation_id")
                overrides = anim_config.get("ai", {}).get("publish_overrides")
                if overrides:
                    metadata["publish_overrides"] = overrides
            
            # Injection des données musicales (titre, artiste, repères des textes)
            if not requires_match:
                apply_song_metadata(metadata, match_data)

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

        # 6. Planification finale (duel : X minutes avant le coup d'envoi, ± 10 min)
        window = None
        if requires_match and slot.get("publish_before_kickoff_min"):
            # deux affiches au même coup d'envoi : la 2e sort 40 min plus tôt (pas deux duels d'affilée)
            same_kick = sum(1 for k in duel_kickoffs if k and k == match_data.get("kickoff"))
            window = kickoff_window(match_data, slot["publish_before_kickoff_min"] + 40 * same_kick)
            duel_kickoffs.append(match_data.get("kickoff"))
        metadata["publish_window"] = window or slot.get("window", {"start": "08:00", "end": "20:00"})
        if window and match_data.get("kickoff"):
            # au-delà, la vidéo de pronostic n'a plus de sens (voir scheduler.py)
            metadata["publish_deadline"] = match_data["kickoff"]
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