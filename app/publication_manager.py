import sys
import os
import json
import random
from pathlib import Path

# Garantit que la racine du projet est dans sys.path pour importer 'platforms'
BASE_DIR = Path(__file__).resolve().parent.parent
if str(BASE_DIR) not in sys.path:
    sys.path.insert(0, str(BASE_DIR))

class PublicationManager:
    def __init__(self):
        self.base_dir = BASE_DIR
        self.paths = self._load_json(self.base_dir / "config" / "paths.json")
        self.app_config = self._load_json(self.base_dir / "config" / "app.json")
        
        self.hashtags_file = self.base_dir / self.paths["data"] / "hashtags.json"
        self.accounts_dir = self.base_dir / self.paths["accounts"]

    def _load_json(self, filepath):
        with open(filepath, 'r', encoding='utf-8') as f:
            return json.load(f)

    def get_account_config(self, account_name="tiktok_main"):
        config_path = self.accounts_dir / account_name / "config.json"
        if not config_path.exists():
            raise FileNotFoundError(f"Configuration du compte introuvable : {config_path}")
        return self._load_json(config_path)

    def get_all_active_accounts(self, accounts_dir="accounts"):
        """
        Parcourt le dossier accounts/ et retourne une liste des configurations 
        de tous les comptes dont le statut 'enabled' est à True.
        """
        active_accounts = []
        if not os.path.exists(accounts_dir):
            print(f"⚠️ [MANAGER] Le dossier {accounts_dir} est introuvable.")
            return active_accounts

        for account_folder in os.listdir(accounts_dir):
            config_path = os.path.join(accounts_dir, account_folder, "config.json")
            
            if os.path.isfile(config_path):
                try:
                    with open(config_path, "r", encoding="utf-8") as f:
                        config = json.load(f)
                        
                        # On injecte le nom du dossier comme identifiant interne de sécurité
                        config["internal_id"] = account_folder 
                        
                        if config.get("enabled", False):
                            active_accounts.append(config)
                        else:
                            print(f"⏸️ [MANAGER] Compte '{account_folder}' ignoré (désactivé).")
                            
                except json.JSONDecodeError:
                    print(f"❌ [MANAGER] Erreur de syntaxe JSON dans {config_path}")
                except Exception as e:
                    print(f"❌ [MANAGER] Impossible de lire {config_path} : {str(e)}")
                    
        return active_accounts

    def generate_metadata(self, match_data, max_hashtags=5):
        """
        Fallback V1 : Construit le format V2 de base si l'animation 
        n'a pas fourni ses propres métadonnées.
        """
        title = f"{match_data['home']} VS {match_data['away']}"
        
        hashtags_pool = []
        if self.hashtags_file.exists():
            data = self._load_json(self.hashtags_file)
            hashtags_pool = data.get("hashtags", [])

        k = min(len(hashtags_pool), max_hashtags)
        selected_hashtags = random.sample(hashtags_pool, k) if k > 0 else []

        return {
            "title": title,
            "description": "",
            "hashtags": selected_hashtags,
            "youtube_tags": []
        }

    def publish_video(self, video_path, metadata, account_name="tiktok_main"):
        """
        Prépare et délègue la publication au module de la plateforme.
        Respecte le mode dry_run (Test 13).
        """
        account_cfg = self.get_account_config(account_name)
        platform = account_cfg.get("platform", "tiktok")
        dry_run = self.app_config.get("dry_run", True)

        # Extraction sécurisée du nouveau format V2
        title = metadata.get("title", "Vidéo automatique")
        desc = metadata.get("description", "")
        hashtags_str = " ".join(metadata.get("hashtags", []))
        
        # Légende d'affichage pour les logs
        display_caption = f"{desc} {hashtags_str}".strip()
        if not display_caption:
            display_caption = title

        print(f"\n[PUBLICATION] Préparation pour le compte '{account_name}' ({platform})")
        print(f"  --> Titre    : {title}")
        print(f"  --> Légende  : {display_caption}")
        print(f"  --> Fichier  : {Path(video_path).name}")

        if dry_run:
            print("  --> [DRY-RUN ACTIVÉ] Simulation de publication réussie (aucun envoi réseau réel).")
            return True

        if platform == "tiktok":
            from platforms.tiktok.uploader import upload_tiktok_video
            return upload_tiktok_video(video_path, metadata, account_name, account_cfg)
            
        elif platform == "youtube":
            from platforms.youtube.uploader import upload_youtube_video
            return upload_youtube_video(video_path, metadata, account_name, account_cfg, self.base_dir)
            
        else:
            print(f"  --> [ERREUR] Plateforme non supportée : {platform}")
            return False