import os
import hashlib
import base64
import secrets
import urllib.parse
import webbrowser
import requests
from dotenv import load_dotenv

load_dotenv()

CLIENT_KEY = os.getenv("TIKTOK_MAIN_CLIENT_KEY")
CLIENT_SECRET = os.getenv("TIKTOK_MAIN_CLIENT_SECRET")
REDIRECT_URI = "https://example.com/callback"
SCOPES = "user.info.basic,video.publish,video.upload"

# Génération PKCE (requis par TikTok OAuth v2)
code_verifier = secrets.token_urlsafe(64)
code_challenge = base64.urlsafe_b64encode(
    hashlib.sha256(code_verifier.encode("ascii")).digest()
).decode("ascii").rstrip("=")

def main():
    if not CLIENT_KEY or CLIENT_KEY == "votre_client_key_ici":
        print("[ERREUR] Renseigne d'abord TIKTOK_MAIN_CLIENT_KEY et TIKTOK_MAIN_CLIENT_SECRET dans ton fichier .env !")
        return

    auth_url = (
        "https://www.tiktok.com/v2/auth/authorize/"
        f"?client_key={CLIENT_KEY}"
        f"&scope={SCOPES}"
        "&response_type=code"
        f"&redirect_uri={urllib.parse.quote(REDIRECT_URI, safe='')}"
        "&state=random_state_123"
        f"&code_challenge={code_challenge}"
        "&code_challenge_method=S256"
    )

    print("1. Ouverture du navigateur pour autoriser le compte TikTok...")
    print(f"Si le navigateur ne s'ouvre pas, clique ici :\n{auth_url}\n")
    webbrowser.open(auth_url)

    print("2. Une fois que tu as cliqué sur 'Continuer/Autoriser' sur TikTok,")
    print("   ton navigateur va afficher la page 'Example Domain'.")
    redirected_url = input("\n👉 Copie l'URL complète de la barre d'adresse du navigateur et colle-la ici : ").strip()

    parsed = urllib.parse.urlparse(redirected_url)
    params = urllib.parse.parse_qs(parsed.query)

    if "code" not in params:
        print(f"\n❌ [ERREUR] Aucun code trouvé dans l'URL fournie : {params}")
        return

    auth_code = params["code"][0]

    print("\n3. Code extrait avec succès ! Échange contre les jetons d'accès en cours...")
    token_url = "https://open.tiktokapis.com/v2/oauth/token/"
    headers = {"Content-Type": "application/x-www-form-urlencoded"}
    data = {
        "client_key": CLIENT_KEY,
        "client_secret": CLIENT_SECRET,
        "code": auth_code,
        "grant_type": "authorization_code",
        "redirect_uri": REDIRECT_URI,
        "code_verifier": code_verifier
    }

    res = requests.post(token_url, headers=headers, data=data)
    res_json = res.json()

    if res.status_code == 200 and "access_token" in res_json:
        print("\n✅ SUCCÈS ! Copie ces deux lignes dans ton fichier .env :\n")
        print(f"TIKTOK_MAIN_ACCESS_TOKEN={res_json['access_token']}")
        print(f"TIKTOK_MAIN_REFRESH_TOKEN={res_json['refresh_token']}\n")
    else:
        print(f"\n❌ [ERREUR] Échec lors de la récupération du token : {res_json}")

if __name__ == "__main__":
    main()