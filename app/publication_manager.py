import sys
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

    def generate_metadata(self, match_data, max_hashtags=5):
        """
        Construit le titre (Home VS Away), sélectionne les hashtags
        et assemble la légende finale (Test 12).
        """
        title = f"{match_data['home']} VS {match_data['away']}"
        
        hashtags_pool = []
        if self.hashtags_file.exists():
            data = self._load_json(self.hashtags_file)
            hashtags_pool = data.get("hashtags", [])

        k = min(len(hashtags_pool), max_hashtags)
        selected_hashtags = random.sample(hashtags_pool, k) if k > 0 else []
        
        hashtags_str = " ".join(selected_hashtags)
        full_caption = f"{title} {hashtags_str}".strip()

        return {
            "title": title,
            "hashtags": selected_hashtags,
            "caption": full_caption
        }

    def publish_video(self, video_path, metadata, account_name="tiktok_main"):
        """
        Prépare et délègue la publication au module de la plateforme.
        Respecte le mode dry_run (Test 13).
        """
        account_cfg = self.get_account_config(account_name)
        platform = account_cfg.get("platform", "tiktok")
        dry_run = self.app_config.get("dry_run", True)

        print(f"\n[PUBLICATION] Préparation pour le compte '{account_name}' ({platform})")
        print(f"  --> Titre    : {metadata['title']}")
        print(f"  --> Légende  : {metadata['caption']}")
        print(f"  --> Fichier  : {Path(video_path).name}")

        if dry_run:
            print("  --> [DRY-RUN ACTIVÉ] Simulation de publication réussie (aucun envoi réseau réel).")
            return True

        if platform == "tiktok":
            from platforms.tiktok.uploader import upload_tiktok_video
            return upload_tiktok_video(video_path, metadata, account_name, account_cfg)
        else:
            print(f"  --> [ERREUR] Plateforme non supportée : {platform}")
            return False