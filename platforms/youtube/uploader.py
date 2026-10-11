import os
from pathlib import Path
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
from google.auth.exceptions import RefreshError

def upload_youtube_video(video_path, metadata, account_name, account_cfg, base_dir):
    """
    Gère l'authentification et l'upload de la vidéo via YouTube Data API v3,
    et associe une miniature personnalisée si elle est présente.
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

    # 3. Préparation des métadonnées (V2)
    # (Par défaut, un short s'active automatiquement si la vidéo est <60s et au format 9:16)
    title = metadata.get("title", "Video Automation")
    
    # YouTube limite le titre à 100 caractères
    if len(title) > 100:
        title = title[:97] + "..."
        
    # Assemblage de la description et des hashtags
    desc = metadata.get("description", "")
    hashtags_str = " ".join(metadata.get("hashtags", []))
    full_desc = f"{desc}\n\n{hashtags_str}".strip()
        
    category_id = account_cfg.get("category_id", "17") # 17 = Sports
    privacy_status = account_cfg.get("privacy_status", "private") # private par sécurité
    
    # On privilégie les tags YouTube dédiés, sinon on nettoie les hashtags (sans le #)
    tags = metadata.get("youtube_tags", [])
    if not tags:
        tags = [t.replace("#", "") for t in metadata.get("hashtags", [])]

    request_body = {
        'snippet': {
            'title': title,
            'description': full_desc,
            'tags': tags[:15], # YouTube recommande max 15 tags
            'categoryId': category_id
        },
        'status': {
            'privacyStatus': privacy_status,
            'selfDeclaredMadeForKids': False
        }
    }

    # 4. Upload binaire de la vidéo
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
        
    except Exception as e:
        print(f"  --> [YouTube ERREUR] Échec de l'upload vidéo : {e}")
        return False

    # 5. Association de la miniature (si activée)
    thumbnail_path = metadata.get("thumbnail_path")
    if thumbnail_path and os.path.exists(thumbnail_path):
        print(f"  --> [YouTube API] Envoi de la miniature {Path(thumbnail_path).name}...")
        try:
            youtube.thumbnails().set(
                videoId=video_id,
                media_body=MediaFileUpload(thumbnail_path, mimetype='image/jpeg')
            ).execute()
            print("      [YouTube SUCCÈS] Miniature associée avec succès !")
        except Exception as e:
            print(f"      [YouTube ERREUR] Échec de l'association de la miniature : {e}")
            print("      (Rappel : Votre chaîne doit être vérifiée par téléphone pour autoriser les miniatures)")
            # On ne retourne PAS False ici, car la vidéo est déjà en ligne avec succès !

    return True