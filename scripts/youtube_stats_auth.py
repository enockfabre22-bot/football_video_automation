"""
Autorisation EN LECTURE SEULE des statistiques d'une chaîne YouTube (à faire une fois, sur le PC).

    python scripts/youtube_stats_auth.py youtube_avantfoot

Ouvre le navigateur : choisir le compte Google PUIS la chaîne (ex. avantfoot). Le jeton est enregistré dans
accounts/<compte>/stats_token.json (ignoré par git). Il ne permet ni de publier ni de modifier quoi que ce soit :
uniquement lire les vidéos (youtube.readonly) et les statistiques YouTube Analytics (yt-analytics.readonly).
Pré-requis : « YouTube Analytics API » activée dans le projet Google Cloud de client_secret.json.
"""
import sys
from pathlib import Path

from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build

BASE_DIR = Path(__file__).resolve().parent.parent
SCOPES = ["https://www.googleapis.com/auth/youtube.readonly",
          "https://www.googleapis.com/auth/yt-analytics.readonly"]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    account = sys.argv[1]
    acc_dir = BASE_DIR / "accounts" / account
    if not acc_dir.is_dir():
        print(f"❌ Compte inconnu : {acc_dir}")
        return 1
    secrets = BASE_DIR / "client_secret.json"
    if not secrets.exists():
        print(f"❌ {secrets} introuvable.")
        return 1

    print("Ouverture du navigateur : choisis le compte Google puis la chaîne à analyser...")
    flow = InstalledAppFlow.from_client_secrets_file(str(secrets), SCOPES)
    creds = flow.run_local_server(port=0, prompt="consent")

    yt = build("youtube", "v3", credentials=creds, cache_discovery=False)
    items = yt.channels().list(part="snippet,statistics", mine=True).execute().get("items", [])
    if not items:
        print("❌ Aucune chaîne sur ce compte : recommence en choisissant la bonne chaîne.")
        return 1
    ch = items[0]
    out = acc_dir / "stats_token.json"
    out.write_text(creds.to_json(), encoding="utf-8")
    st = ch["statistics"]
    print(f"✅ Chaîne autorisée : {ch['snippet']['title']} ({st.get('subscriberCount')} abonnés, "
          f"{st.get('videoCount')} vidéos) -> {out.relative_to(BASE_DIR)}")
    print("   Vérifie que c'est bien la bonne chaîne, sinon relance le script.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
