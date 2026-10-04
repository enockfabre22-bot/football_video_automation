import os
import json
import random
import subprocess
import sys
from pathlib import Path
from dotenv import load_dotenv

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
        """
        Parcourt le dossier animations/ et retourne la liste des animations
        activées et compatibles avec le compte cible.
        """
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
        """
        Choisit une animation selon une stratégie donnée.
        - strategy: "random" ou "fixed"
        - target_animation: requis si strategy est "fixed"
        - exclude_animations: liste d'animations à ignorer si strategy est "random"
        """
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
            # On retire les animations exclues de la liste des choix possibles
            valid_animations = [a for a in animations if a["name"] not in exclude_animations]
            if not valid_animations:
                print(f"[ERREUR] Toutes les animations ont été exclues pour '{account_name}'.")
                return None
            return random.choice(valid_animations)

        else:
            print(f"[ERREUR] Stratégie '{strategy}' inconnue.")
            return None

    def execute_animation(self, match_data, animation_config, job_id):
        """
        Prépare l'environnement du job, exécute le pipeline étape par étape
        et valide la présence de la vidéo finale.
        """
        anim_name = animation_config["name"]
        anim_folder = animation_config["folder_path"]
        print(f"\n[ANIMATION] Démarrage de '{anim_name}' pour le job : {job_id}")

        # 1. Préparation des dossiers et chemins du Job
        job_work_dir = self.temp_dir / job_id
        job_work_dir.mkdir(parents=True, exist_ok=True)
        self.videos_dir.mkdir(parents=True, exist_ok=True)

        output_video_path = self.videos_dir / f"{job_id}.mp4"
        job_file_path = job_work_dir / "job.json"

        # 2. Création du contrat d'interface (job.json)
        job_payload = {
            "job_id": job_id,
            "animation": anim_name,
            "match": {
                "date": match_data["date"],
                "home": match_data["home"],
                "away": match_data["away"]
            },
            "resources": match_data["resources"],
            "work_dir": str(job_work_dir.resolve()),
            "output_video": str(output_video_path.resolve())
        }

        with open(job_file_path, 'w', encoding='utf-8') as f:
            json.dump(job_payload, f, indent=4, ensure_ascii=False)

        # 3. Exécution séquentielle des étapes (steps)
        steps = animation_config.get("steps", [])
        for index, step in enumerate(steps, start=1):
            step_type = step.get("type")
            step_name = step.get("name", f"Étape {index}")
            print(f"  --> Exécution étape {index}/{len(steps)} ({step_type}) : {step_name}")

            success = self._run_step(step, anim_folder, job_file_path)
            if not success:
                print(f"[ERREUR] Échec critique à l'étape {index} ({step_name}). Abandon du rendu.")
                return None

        # 4. Validation de la vidéo finale produite (Test 10)
        if output_video_path.exists() and output_video_path.stat().st_size > 0:
            print(f"[SUCCÈS] Vidéo finale générée et validée : {output_video_path}")
            return output_video_path
        else:
            print(f"[ERREUR] Le pipeline s'est terminé mais la vidéo finale est absente ou vide : {output_video_path}")
            return None

    def _run_step(self, step, anim_folder, job_file_path):
        """Exécute une étape spécifique selon son type (python, processing, etc.)."""
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
                # Sur VPS Linux : utilisation d'un écran virtuel (xvfb-run) si pas d'écran physique
                processing_bin = os.getenv("PROCESSING_CMD", "processing-java")
                base_cmd = [processing_bin, f"--sketch={sketch_folder.resolve()}", "--run", str(job_file_path)]
                
                if os.name == "posix" and not os.getenv("DISPLAY"):
                    cmd = ["xvfb-run", "-a"] + base_cmd
                else:
                    cmd = base_cmd

        else:
            print(f"      [ERREUR] Type d'étape non supporté par le moteur : {step_type}")
            return False

        # Lancement du sous-processus avec le bon working_dir
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

if __name__ == "__main__":
    from match_manager import MatchManager
    
    print("--- Test du Moteur d'Animations ---")
    match_mgr = MatchManager()
    anim_mgr = AnimationManager()

    anims = anim_mgr.discover_animations(account_name="tiktok_main")
    print(f"[INFO] {len(anims)} animation(s) découverte(s) pour le compte tiktok_main : {[a['name'] for a in anims]}")

    matchs = match_mgr.get_valid_matches_for_tomorrow()

    if matchs and anims:
        premier_match = matchs[0]
        # Test avec la nouvelle signature
        anim_choisie = anim_mgr.select_animation(account_name="tiktok_main", strategy="random")
        
        if anim_choisie:
            clean_home = premier_match['home'].replace(' ', '_')
            clean_away = premier_match['away'].replace(' ', '_')
            job_id = f"{premier_match['date']}_{clean_home}_{clean_away}_{anim_choisie['name']}"
            
            video_finale = anim_mgr.execute_animation(premier_match, anim_choisie, job_id)