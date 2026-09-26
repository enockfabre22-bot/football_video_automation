from pathlib import Path
from platforms.tiktok.client import TikTokClient

def upload_tiktok_video(video_path, metadata, account_name, account_cfg):
    """
    Fonction appelée par PublicationManager lorsque dry_run est False.
    Gère l'authentification, l'initialisation et l'upload du fichier MP4.
    """
    env_prefix = account_cfg.get("env_prefix", "TIKTOK_MAIN")
    client = TikTokClient(env_prefix=env_prefix)

    if not client.is_configured():
        print(f"  --> [TikTok ERREUR] Les clés API pour '{env_prefix}' ne sont pas configurées dans le fichier .env.")
        return False

    video_file = Path(video_path)
    if not video_file.exists():
        print(f"  --> [TikTok ERREUR] Fichier vidéo introuvable : {video_file}")
        return False

    video_size = video_file.stat().st_size
    caption = metadata.get("caption", metadata.get("title", ""))

    # 1. Tentative d'initialisation de la publication
    print("  --> [TikTok API] Initialisation de la publication sur TikTok...")
    status_code, res_json = client.init_video_publish(video_size, caption, account_cfg)

    # Si le token a expiré (code 401), on le rafraîchit automatiquement et on réessaie
    if status_code == 401:
        print("  --> [TikTok API] Jeton expiré (401), rafraîchissement automatique en cours...")
        if client.refresh_access_token():
            status_code, res_json = client.init_video_publish(video_size, caption, account_cfg)
        else:
            return False

    error_info = res_json.get("error", {})
    if status_code != 200 or error_info.get("code") != "ok":
        print(f"  --> [TikTok ERREUR] Échec d'initialisation (HTTP {status_code}) : {error_info}")
        return False

    upload_url = res_json.get("data", {}).get("upload_url")
    publish_id = res_json.get("data", {}).get("publish_id")

    if not upload_url:
        print("  --> [TikTok ERREUR] Aucune URL d'upload reçue de TikTok.")
        return False

    # 2. Envoi du fichier vidéo MP4
    print(f"  --> [TikTok API] Envoi du fichier MP4 ({video_size / (1024*1024):.2f} Mo)...")
    upload_ok = client.upload_video_binary(upload_url, video_file)

    if upload_ok:
        print(f"  --> [TikTok SUCCÈS] Vidéo envoyée avec succès ! (publish_id: {publish_id})")
        return True
    else:
        print("  --> [TikTok ERREUR] Échec lors du transfert binaire du fichier MP4.")
        return False