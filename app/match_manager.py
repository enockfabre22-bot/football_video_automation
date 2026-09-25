import json
from datetime import datetime, timedelta
from pathlib import Path

class MatchManager:
    def __init__(self):
        # On définit la racine du projet dynamiquement par rapport à ce fichier
        self.base_dir = Path(__file__).resolve().parent.parent
        
        # Chargement des configurations
        self.paths = self._load_json(self.base_dir / "config" / "paths.json")
        self.app_config = self._load_json(self.base_dir / "config" / "app.json")
        
        # Chemin vers le fichier des matchs
        self.matches_file = self.base_dir / self.paths["data"] / "matches.json"

    def _load_json(self, filepath):
        """Fonction utilitaire pour charger un fichier JSON proprement."""
        with open(filepath, 'r', encoding='utf-8') as f:
            return json.load(f)

    def get_tomorrow_date_str(self):
        """Calcule la date de demain au format AAAA-MM-JJ."""
        tomorrow = datetime.now() + timedelta(days=1)
        return tomorrow.strftime("%Y-%m-%d")

    def get_matches_for_tomorrow(self):
        """Filtre et retourne la liste des matchs prévus pour demain."""
        if not self.matches_file.exists():
            print(f"[ERREUR] Le fichier {self.matches_file} est introuvable.")
            return []

        matches = self._load_json(self.matches_file)
        tomorrow_str = self.get_tomorrow_date_str()
        print(f"[INFO] Recherche des matchs pour le : {tomorrow_str}")

        # Filtrage en compréhension de liste
        tomorrow_matches = [m for m in matches if m.get("date") == tomorrow_str]
        
        return tomorrow_matches

# ==========================================
# BLOC DE TEST (Exécuté uniquement si on lance ce fichier directement)
# ==========================================
if __name__ == "__main__":
    manager = MatchManager()
    matchs_demain = manager.get_matches_for_tomorrow()
    
    print(f"\n[RÉSULTAT] {len(matchs_demain)} match(s) trouvé(s) pour demain :")
    for match in matchs_demain:
        print(f" - {match['home']} VS {match['away']}")