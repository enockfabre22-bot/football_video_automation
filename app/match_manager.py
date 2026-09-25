import json
from datetime import datetime, timedelta
from pathlib import Path

class MatchManager:
    def __init__(self):
        self.base_dir = Path(__file__).resolve().parent.parent
        self.paths = self._load_json(self.base_dir / "config" / "paths.json")
        self.app_config = self._load_json(self.base_dir / "config" / "app.json")
        
        self.matches_file = self.base_dir / self.paths["data"] / "matches.json"
        self.logos_dir = self.base_dir / self.paths["logos"]

    def _load_json(self, filepath):
        """Fonction utilitaire pour charger un fichier JSON proprement."""
        with open(filepath, 'r', encoding='utf-8') as f:
            return json.load(f)

    def get_tomorrow_date_str(self):
        """Calcule la date de demain au format AAAA-MM-JJ."""
        tomorrow = datetime.now() + timedelta(days=1)
        return tomorrow.strftime("%Y-%m-%d")

    def get_matches_for_tomorrow(self):
        """Filtre et retourne la liste brute des matchs prévus pour demain."""
        if not self.matches_file.exists():
            print(f"[ERREUR] Le fichier {self.matches_file} est introuvable.")
            return []

        matches = self._load_json(self.matches_file)
        tomorrow_str = self.get_tomorrow_date_str()
        print(f"[INFO] Recherche des matchs pour le : {tomorrow_str}")

        return [m for m in matches if m.get("date") == tomorrow_str]

    def get_team_logo_path(self, team_name):
        """
        Normalise le nom de l'équipe et vérifie si le logo existe.
        Retourne l'objet Path du logo s'il existe, sinon None.
        """
        # Nettoyage des espaces inutiles au début et à la fin
        clean_name = team_name.strip()
        logo_path = self.logos_dir / f"{clean_name}.png"
        
        if logo_path.exists():
            return logo_path
        return None

    def get_valid_matches_for_tomorrow(self):
        """
        Récupère les matchs de demain et vérifie la présence des logos.
        Retourne uniquement les matchs complets avec les chemins des ressources.
        """
        raw_matches = self.get_matches_for_tomorrow()
        valid_matches = []

        for match in raw_matches:
            home = match.get("home", "")
            away = match.get("away", "")

            home_logo = self.get_team_logo_path(home)
            away_logo = self.get_team_logo_path(away)

            # Vérification et signalement précis en cas de logo manquant
            missing = []
            if not home_logo:
                missing.append(f"{home} (attendu: {home.strip()}.png)")
            if not away_logo:
                missing.append(f"{away} (attendu: {away.strip()}.png)")

            if missing:
                print(f"[AVERTISSEMENT] Match ignoré ({home} VS {away}) -> Logo(s) manquant(s) : {', '.join(missing)}")
                continue

            # Si tout est bon, on enrichit le dictionnaire du match pour la suite du pipeline
            match_ready = {
                "date": match["date"],
                "home": home.strip(),
                "away": away.strip(),
                "resources": {
                    "home_logo": str(home_logo),
                    "away_logo": str(away_logo)
                }
            }
            valid_matches.append(match_ready)

        return valid_matches

# ==========================================
# BLOC DE TEST (Tests 5 et 6)
# ==========================================
if __name__ == "__main__":
    manager = MatchManager()
    print("--- Vérification des matchs et des ressources ---")
    matchs_valides = manager.get_valid_matches_for_tomorrow()
    
    print(f"\n[RÉSULTAT] {len(matchs_valides)} match(s) prêt(s) pour la génération vidéo :")
    for m in matchs_valides:
        print(f" ✅ {m['home']} VS {m['away']}")
        print(f"    Logo Domicile : {m['resources']['home_logo']}")
        print(f"    Logo Extérieur : {m['resources']['away_logo']}")