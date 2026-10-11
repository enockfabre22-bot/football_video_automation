import os
from google_auth_oauthlib.flow import InstalledAppFlow

# L'autorisation stricte dont on a besoin pour uploader des vidéos
SCOPES = ['https://www.googleapis.com/auth/youtube.upload']

def main():
    client_secrets_file = "client_secret.json"
    
    if not os.path.exists(client_secrets_file):
        print(f"❌ Le fichier {client_secrets_file} est introuvable.")
        return

    print("Ouverture du navigateur pour l'authentification Google...")
    
    # Lancement du flux d'authentification OAuth
    flow = InstalledAppFlow.from_client_secrets_file(client_secrets_file, SCOPES)
    creds = flow.run_local_server(port=0)

    # Sauvegarde du jeton d'accès
    with open('token.json', 'w') as token:
        token.write(creds.to_json())
        
    print("✅ Authentification réussie ! Le fichier 'token.json' a été généré.")

if __name__ == '__main__':
    main()