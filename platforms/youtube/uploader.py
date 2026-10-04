import os
from pathlib import Path
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
from google.auth.exceptions import RefreshError

def upload_youtube_video(video_path, metadata, account_name, account_cfg, base_dir):
    """
    Gère l'authentification et l'upload de la vidéo via YouTube Data API v3.
    """
    # 1. Résolution des chemins
    account_dir = base_dir / "accounts" / account_name
    token_path = account_dir / "token.json"
    
    if not token_path.exists():
        print(f"  --> [YouTube ERREUR] Fichier token.json introuvable dans {account_dir}")
        return False
        
    video_file = Path(video_path)
    if not video_file.exists():
        print(f"  --> [YouTube ERREUR] Fichier vidéo introuvable : {video_file}")
        return False

    # 2. Authentification Google (gère le rafraîchissement du jeton automatiquement)
    try:
        creds = Credentials.from_authorized_user_file(
            str(token_path), 
            ["https://www.googleapis.com/auth/youtube.upload"]
        )
        youtube = build('youtube', 'v3', credentials=creds)
    except RefreshError as e:
        print(f"  --> [YouTube Auth ERREUR] Le jeton d'accès a expiré et ne peut pas être rafraîchi: {e}")
        return False
    except Exception as e:
        print(f"  --> [YouTube Auth ERREUR] Erreur de connexion à l'API: {e}")
        return False

    # 3. Préparation des métadonnées selon la configuration
    # (Par défaut, un short s'active automatiquement si la vidéo est <60s et au format 9:16)
    title = metadata.get("caption", metadata.get("title", "Video Automation"))
    # YouTube limite le titre à 100 caractères
    if len(title) > 100:
        title = title[:97] + "..."
        
    category_id = account_cfg.get("category_id", "17") # 17 = Sports
    privacy_status = account_cfg.get("privacy_status", "private") # private par sécurité par défaut
    tags = metadata.get("hashtags", [])

    request_body = {
        'snippet': {
            'title': title,
            'description': metadata.get("caption", ""),
            'tags': tags,
            'categoryId': category_id
        },
        'status': {
            'privacyStatus': privacy_status,
            'selfDeclaredMadeForKids': False
        }
    }

    # 4. Upload binaire
    print(f"  --> [YouTube API] Début de l'envoi de {video_file.name} vers la chaîne {account_name}...")
    try:
        media_file = MediaFileUpload(
            str(video_file), 
            chunksize=-1, 
            resumable=True, 
            mimetype='video/mp4'
        )
        
        request = youtube.videos().insert(
            part='snippet,status',
            body=request_body,
            media_body=media_file
        )
        
        response = request.execute()
        video_id = response.get('id')
        print(f"  --> [YouTube SUCCÈS] Vidéo publiée (Statut: {privacy_status}) ! Lien: https://youtu.be/{video_id}")
        return True
        
    except Exception as e:
        print(f"  --> [YouTube ERREUR] Échec de l'upload : {e}")
        return False