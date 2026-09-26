import os
import requests
from pathlib import Path
from dotenv import load_dotenv, set_key

BASE_DIR = Path(__file__).resolve().parent.parent.parent
ENV_PATH = BASE_DIR / ".env"
load_dotenv(ENV_PATH)

class TikTokClient:
    BASE_URL = "https://open.tiktokapis.com/v2"

    def __init__(self, env_prefix="TIKTOK_MAIN"):
        self.env_prefix = env_prefix
        self.client_key = os.getenv(f"{env_prefix}_CLIENT_KEY")
        self.client_secret = os.getenv(f"{env_prefix}_CLIENT_SECRET")
        self.access_token = os.getenv(f"{env_prefix}_ACCESS_TOKEN")
        self.refresh_token = os.getenv(f"{env_prefix}_REFRESH_TOKEN")

    def is_configured(self):
        """Vérifie que les identifiants minimums sont renseignés dans le .env."""
        if not self.access_token and not self.refresh_token:
            return False
        if self.access_token and self.access_token != "votre_access_token_ici":
            return True
        if self.refresh_token and self.refresh_token != "votre_refresh_token_ici":
            return True
        return False

    def refresh_access_token(self):
        """
        Utilise le refresh_token pour obtenir un nouvel access_token valide 24h.
        Indispensable pour l'autonomie sur le VPS.
        """
        if not all([self.client_key, self.client_secret, self.refresh_token]):
            print("  --> [TikTok Auth] Clés de rafraîchissement incomplètes dans .env.")
            return False

        url = f"{self.BASE_URL}/oauth/token/"
        headers = {"Content-Type": "application/x-www-form-urlencoded"}
        data = {
            "client_key": self.client_key,
            "client_secret": self.client_secret,
            "grant_type": "refresh_token",
            "refresh_token": self.refresh_token
        }

        try:
            response = requests.post(url, headers=headers, data=data, timeout=15)
            res_json = response.json()

            if response.status_code == 200 and "access_token" in res_json:
                self.access_token = res_json["access_token"]
                self.refresh_token = res_json.get("refresh_token", self.refresh_token)
                
                # Sauvegarde automatique sur le disque dans .env pour le VPS
                if ENV_PATH.exists():
                    set_key(str(ENV_PATH), f"{self.env_prefix}_ACCESS_TOKEN", self.access_token)
                    set_key(str(ENV_PATH), f"{self.env_prefix}_REFRESH_TOKEN", self.refresh_token)
                    
                print("  --> [TikTok Auth] Jeton d'accès rafraîchi et sauvegardé dans .env avec succès.")
                return True
            else:
                err_msg = res_json.get("error_description", res_json)
                print(f"  --> [TikTok Auth - ERREUR] Échec du rafraîchissement : {err_msg}")
                return False
        except Exception as e:
            print(f"  --> [TikTok Auth - EXCEPTION] Erreur réseau OAuth : {e}")
            return False

    def init_video_publish(self, video_size, caption, account_cfg):
        """
        Étape 1 de la publication TikTok :
        - post_mode == "inbox"  -> envoie dans les brouillons/notifications TikTok (video.upload)
        - post_mode == "direct" -> publie directement sur le profil (video.publish)
        """
        post_mode = account_cfg.get("post_mode", "direct")
        headers = {
            "Authorization": f"Bearer {self.access_token}",
            "Content-Type": "application/json; charset=UTF-8"
        }

        if post_mode == "inbox":
            url = f"{self.BASE_URL}/post/publish/inbox/video/init/"
            payload = {
                "source_info": {
                    "source": "FILE_UPLOAD",
                    "video_size": video_size,
                    "chunk_size": video_size,
                    "total_chunk_count": 1
                }
            }
        else:
            url = f"{self.BASE_URL}/post/publish/video/init/"
            payload = {
                "post_info": {
                    "title": caption,
                    "privacy_level": account_cfg.get("privacy_level", "SELF_ONLY"),
                    "disable_duet": account_cfg.get("disable_duet", False),
                    "disable_comment": account_cfg.get("disable_comment", False),
                    "disable_stitch": account_cfg.get("disable_stitch", False)
                },
                "source_info": {
                    "source": "FILE_UPLOAD",
                    "video_size": video_size,
                    "chunk_size": video_size,
                    "total_chunk_count": 1
                }
            }

        response = requests.post(url, headers=headers, json=payload, timeout=30)
        return response.status_code, response.json()

    def upload_video_binary(self, upload_url, video_path):
        """
        Étape 2 de la publication TikTok : envoie le fichier binaire MP4 via PUT.
        """
        video_file = Path(video_path)
        video_size = video_file.stat().st_size

        headers = {
            "Content-Range": f"bytes 0-{video_size - 1}/{video_size}",
            "Content-Type": "video/mp4"
        }

        with open(video_file, "rb") as f:
            response = requests.put(upload_url, headers=headers, data=f, timeout=300)

        return response.status_code in [200, 201, 206]