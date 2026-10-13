import os
import json
import random
import subprocess
import sys
from pathlib import Path
from dotenv import load_dotenv

# Import du nouveau générateur de miniatures
from thumbnail_generator import generate_custom_thumbnail

load_dotenv()

class AnimationManager:
    def __init__(self):
        self.base_dir = Path(__file__).resolve().parent.parent
        self.paths = self._load_json(self.base_dir / "config" / "paths.json")
        
        self.animations_dir = self.base_dir / self.paths["animations"]
        self.temp_dir = self.base_dir / self.paths["output_temp"]
        self.videos_dir = self.base_dir / self.paths["output_videos"]

    def _load_json(self, filepath):
        with open(filepath, 'r', encoding='utf-8') as f:
            return json.load(f)

    def discover_animations(self, account_name):
        available = []
        if not self.animations_dir.exists():
            return available

        for anim_folder in self.animations_dir.iterdir():
            if anim_folder.is_dir():
                config_file = anim_folder / "config.json"
                if config_file.exists():
                    try:
                        config = self._load_json(config_file)
                        if config.get("enabled", False) and account_name in config.get("accounts", []):
                            config["folder_path"] = anim_folder
                            available.append(config)
                    except Exception as e:
                        print(f"[ERREUR] Lecture impossible de {config_file}: {e}")
        return available

    def select_animation(self, account_name, strategy="random", target_animation=None, exclude_animations=None):
        if exclude_animations is None:
            exclude_animations = []

        animations = self.discover_animations(account_name)
        if not animations:
            print(f"[ERREUR] Aucune animation configurée pour le compte '{account_name}'.")
            return None

        if strategy == "fixed":
            if not target_animation:
                print("[ERREUR] La stratégie 'fixed' requiert 'target_animation'.")
                return None
            for anim in animations:
                if anim["name"] == target_animation:
                    return anim
            print(f"[ERREUR] L'animation cible '{target_animation}' est introuvable ou désactivée.")
            return None

        elif strategy == "random":
            valid_animations = [a for a in animations if a["name"] not in exclude_animations]
            if not valid_animations:
                print(f"[ERREUR] Toutes les animations ont été exclues pour '{account_name}'.")
                return None
            return random.choice(valid_animations)

        else:
            print(f"[ERREUR] Stratégie '{strategy}' inconnue.")
            return None

    def generate_job_metadata(self, animation_config, account_config, match_data=None):
        anim_meta = animation_config.get("metadata", {})
        acc_rules = account_config.get("metadata_rules", {})
        
        titles = anim_meta.get("titles", ["Simulation Football"])
        descriptions = anim_meta.get("descriptions", [""])
        
        chosen_title = random.choice(titles) if titles else "Simulation Football"
        chosen_desc = random.choice(descriptions) if descriptions else ""
        
        if match_data:
            home = match_data.get("home", "Domicile")
            away = match_data.get("away", "Extérieur")
            chosen_title = chosen_title.replace("{home}", home).replace("{away}", away)
            chosen_desc = chosen_desc.replace("{home}", home).replace("{away}", away)
            
            # Remplacement dynamique basé sur les fichiers en entrée (ressources)
            if "resources" in match_data and isinstance(match_data["resources"], dict):
                for res_key, res_path in match_data["resources"].items():
                    if isinstance(res_path, str):
                        # Extrait le nom du fichier sans l'extension
                        file_name_without_ext = Path(res_path).stem 
                        chosen_title = chosen_title.replace(f"{{{res_key}}}", file_name_without_ext)
                        chosen_desc = chosen_desc.replace(f"{{{res_key}}}", file_name_without_ext)
            
        hashtags_pool = anim_meta.get("hashtags", [])
        nb_hashtags = acc_rules.get("hashtag_count", 3)
        nb_hashtags = min(nb_hashtags, len(hashtags_pool))
        chosen_hashtags = random.sample(hashtags_pool, nb_hashtags) if nb_hashtags > 0 else []
        
        yt_tags_pool = anim_meta.get("youtube_tags", [])
        nb_yt_tags = acc_rules.get("youtube_tag_count", 0)
        nb_yt_tags = min(nb_yt_tags, len(yt_tags_pool))
        chosen_yt_tags = random.sample(yt_tags_pool, nb_yt_tags) if nb_yt_tags > 0 else []

        # Remplacement dynamique dans les tags générés
        if match_data:
            home_nom = match_data.get("home", "Domicile")
            away_nom = match_data.get("away", "Extérieur")
            
            # Hashtags (on retire les espaces pour que #Real Madrid devienne #RealMadrid)
            home_ht = home_nom.replace(" ", "")
            away_ht = away_nom.replace(" ", "")
            
            chosen_hashtags = [
                h.replace("{home}", home_ht).replace("{away}", away_ht) 
                for h in chosen_hashtags
            ]
            
            # YouTube Tags (les espaces sont autorisés nativement)
            chosen_yt_tags = [
                t.replace("{home}", home_nom).replace("{away}", away_nom) 
                for t in chosen_yt_tags
            ]

        # --- NOUVEAUTÉ : Ajout sécurisé des hashtags dans le titre ---
        titre_final = chosen_title
        for hashtag in chosen_hashtags:
            # On vérifie si l'ajout du hashtag (+1 pour l'espace) dépasse 100 caractères
            if len(titre_final) + len(hashtag) + 1 <= 100:
                titre_final += f" {hashtag}"
            else:
                break # Stop : on protège la publication YouTube

        return {
            "title": titre_final,
            "description": chosen_desc,
            "hashtags": chosen_hashtags,
            "youtube_tags": chosen_yt_tags
        }

    def execute_animation(self, match_data, animation_config, job_id, job_metadata=None):
        if job_metadata is None:
            job_metadata = {}

        anim_name = animation_config["name"]
        anim_folder = animation_config["folder_path"]
        print(f"\n[ANIMATION] Démarrage de '{anim_name}' pour le job : {job_id}")

        job_work_dir = self.temp_dir / job_id
        job_work_dir.mkdir(parents=True, exist_ok=True)
        self.videos_dir.mkdir(parents=True, exist_ok=True)

        output_video_path = self.videos_dir / f"{job_id}.mp4"
        job_file_path = job_work_dir / "job.json"

        job_payload = {
            "job_id": job_id,
            "animation": anim_name,
            "match": {
                "date": match_data["date"] if match_data else "",
                "home": match_data["home"] if match_data else "",
                "away": match_data["away"] if match_data else ""
            },
            "metadata": job_metadata,
            "resources": match_data["resources"] if match_data else {},
            "work_dir": str(job_work_dir.resolve()),
            "output_video": str(output_video_path.resolve())
        }

        with open(job_file_path, 'w', encoding='utf-8') as f:
            json.dump(job_payload, f, indent=4, ensure_ascii=False)

        steps = animation_config.get("steps", [])
        for index, step in enumerate(steps, start=1):
            step_type = step.get("type")
            step_name = step.get("name", f"Étape {index}")
            print(f"  --> Exécution étape {index}/{len(steps)} ({step_type}) : {step_name}")

            success = self._run_step(step, anim_folder, job_file_path)
            if not success:
                print(f"[ERREUR] Échec critique à l'étape {index} ({step_name}). Abandon du rendu.")
                return None

        # Validation de la vidéo finale
        if output_video_path.exists() and output_video_path.stat().st_size > 0:
            print(f"[SUCCÈS] Vidéo finale générée et validée : {output_video_path}")
            
            # Bloc Miniature Facultative
            thumb_config = animation_config.get("thumbnail", {})
            if thumb_config.get("enabled", False):
                source_filename = thumb_config.get("source_image", "thumbnail_source.png")
                source_path = anim_folder / source_filename
                output_thumb_path = self.videos_dir / f"{job_id}.jpg"
                
                if source_path.exists():
                    print(f"  --> Création de la miniature personnalisée depuis {source_filename}...")
                    title = job_metadata.get("title", "Simulation Football")
                    t_path = generate_custom_thumbnail(str(source_path), str(output_thumb_path), title)
                    if t_path:
                        # Injection dynamique dans les métadonnées
                        job_metadata["thumbnail_path"] = t_path
                        print(f"      [SUCCÈS] Miniature sauvegardée : {output_thumb_path.name}")
                else:
                    print(f"  --> [AVERTISSEMENT] Image source de miniature introuvable : {source_path}")
            
            return output_video_path
        else:
            print(f"[ERREUR] Le pipeline s'est terminé mais la vidéo finale est absente ou vide : {output_video_path}")
            return None

    def _run_step(self, step, anim_folder, job_file_path):
        step_type = step.get("type")
        working_dir = anim_folder

        if step_type == "python":
            script_rel_path = step.get("script")
            script_full_path = anim_folder / script_rel_path
            
            if not script_full_path.exists():
                print(f"      [ERREUR] Script introuvable : {script_full_path}")
                return False

            cmd = [sys.executable, str(script_full_path), str(job_file_path)]

        elif step_type == "processing":
            project_rel_path = step.get("project")
            sketch_folder = anim_folder / project_rel_path
            
            if not sketch_folder.exists():
                print(f"      [ERREUR] Dossier Processing introuvable : {sketch_folder}")
                return False

            processing_dir = os.getenv("PROCESSING_DIR")
            if processing_dir and os.name == "nt":
                p_dir = Path(processing_dir)
                java_exe = p_dir / "app" / "resources" / "jdk" / "bin" / "java.exe"
                resources_dir = p_dir / "app" / "resources"
                
                classpath = (
                    f"{resources_dir / 'core/library'}/*;"
                    f"{resources_dir / 'modes/java/mode'}/*"
                )
                working_dir = resources_dir
                
                cmd = [
                    str(java_exe),
                    f"-Dcompose.application.resources.dir={resources_dir}",
                    "-cp", classpath,
                    "processing.mode.java.Commander",
                    f"--sketch={sketch_folder.resolve()}",
                    "--run",
                    str(job_file_path)
                ]
            else:
                processing_bin = os.getenv("PROCESSING_CMD", "processing-java")
                base_cmd = [processing_bin, f"--sketch={sketch_folder.resolve()}", "--run", str(job_file_path)]
                
                if os.name == "posix" and not os.getenv("DISPLAY"):
                    cmd = ["xvfb-run", "-a"] + base_cmd
                else:
                    cmd = base_cmd

        else:
            print(f"      [ERREUR] Type d'étape non supporté par le moteur : {step_type}")
            return False

        try:
            result = subprocess.run(cmd, cwd=working_dir, capture_output=True, text=True, check=False)
            if result.stdout:
                print(result.stdout.rstrip())
            if result.returncode != 0:
                print(f"      [ERREUR CODE {result.returncode}] Détail :\n{result.stderr.rstrip()}")
                return False
            return True
        except Exception as e:
            print(f"      [EXCEPTION] Erreur lors de l'exécution de la commande : {e}")
            return False