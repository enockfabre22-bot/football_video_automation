import json
from datetime import datetime, timedelta
from pathlib import Path

import football_data
import football_fixtures

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
        """Matchs bruts de demain (calendrier manuel + automatique)."""
        return self.get_matches_for_date(self.get_tomorrow_date_str())

    def get_matches_for_date(self, date_str):
        """
        Matchs bruts d'une date : calendrier manuel data/matches.json (prioritaire, dans l'ordre saisi)
        puis gros matchs du calendrier automatique data/matches_api.json (football_fixtures.py), par note.
        """
        print(f"[INFO] Recherche des matchs pour le : {date_str}")
        manual = []
        if self.matches_file.exists():
            manual = [m for m in self._load_json(self.matches_file) if m.get("date") == date_str]
        auto = [m for m in football_fixtures.load_api_matches() if m.get("date") == date_str]
        auto.sort(key=lambda m: -m.get("score", 0))
        return manual + auto

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
        # Nom saisi autrement (casse, accents, nom anglais « Bayern Munich »...) : registre des équipes
        key = football_data.resolve_key(clean_name)
        if key and (self.logos_dir / f"{key}.png").exists():
            return self.logos_dir / f"{key}.png"
        return None

    def get_valid_matches_for_tomorrow(self):
        return self.get_valid_matches(self.get_tomorrow_date_str())

    def get_valid_matches(self, date_str):
        """
        Matchs d'une date dont les deux logos existent, enrichis (noms anglais, hashtags, coup d'envoi).
        Un même match présent dans les deux calendriers n'est gardé qu'une fois.
        """
        raw_matches = self.get_matches_for_date(date_str)
        valid_matches = []
        seen = set()

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

            # Si tout est bon, on enrichit le dictionnaire du match pour la suite du pipeline.
            # Noms affichés (anglais, bien écrits) et hashtags viennent du registre data/teams.json.
            h_info = football_data.team_info(home_logo.stem)
            a_info = football_data.team_info(away_logo.stem)
            pair = frozenset((h_info["key"], a_info["key"]))
            if pair in seen:
                continue
            seen.add(pair)
            match_ready = {
                "date": match["date"],
                "home": h_info["name"],
                "away": a_info["name"],
                "home_tag": h_info["tag"],
                "away_tag": a_info["tag"],
                "competition": match.get("competition", ""),
                "big_match": bool(match.get("big_match", False)),
                "kickoff": match.get("kickoff", ""),          # ISO, heure locale (calendrier automatique)
                "time": match.get("time", ""),                # HH:MM si connu
                "source": match.get("source", "manual"),
                "resources": {
                    "home_logo": str(home_logo),
                    "away_logo": str(away_logo),
                    "home_key": h_info["key"],
                    "away_key": a_info["key"],
                    "competition": match.get("competition", ""),
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