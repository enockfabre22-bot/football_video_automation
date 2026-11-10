import json
import os
from datetime import date, datetime
from pathlib import Path

# Statuts d'une création IA (le studio IA produit les autres statuts : DRAFT, FAILED, ABANDONED...)
READY = "READY"            # Vidéo finale validée, disponible pour un créneau
RESERVED = "RESERVED"      # Réservée par un job (en attente de publication)
PUBLISHED = "PUBLISHED"    # Publiée : vidéo supprimée du stock, fiche conservée
EXPIRED = "EXPIRED"        # Création à thème (ex. Noël) dont la fenêtre de publication est passée


class AIStockManager:
    """
    Gère le stock des animations générées par le studio IA.

    Chaque création vit dans ai_data/<animation>/creations/<creation_id>/ avec un state.json.
    Le scheduler ne fait que consommer le stock (READY -> RESERVED -> PUBLISHED) ;
    il ne génère jamais rien lui-même et ne dépend donc jamais de l'API Claude.
    """

    STATE_FILE = "state.json"

    def __init__(self):
        self.base_dir = Path(__file__).resolve().parent.parent
        with open(self.base_dir / "config" / "paths.json", "r", encoding="utf-8") as f:
            paths = json.load(f)
        self.ai_data_dir = self.base_dir / paths.get("ai_data", "ai_data/")

    # ------------------------------------------------------------------ #
    # Lecture / écriture
    # ------------------------------------------------------------------ #
    def _creations_dir(self, anim_name):
        return self.ai_data_dir / anim_name / "creations"

    def _load_state(self, creation_dir):
        try:
            with open(creation_dir / self.STATE_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except (OSError, json.JSONDecodeError):
            return None

    def _save_state(self, creation_dir, state):
        """Écriture atomique (fichier temporaire puis remplacement) pour ne jamais corrompre l'état."""
        state["updated_at"] = datetime.now().isoformat(timespec="seconds")
        tmp = creation_dir / (self.STATE_FILE + ".tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=4, ensure_ascii=False)
        os.replace(tmp, creation_dir / self.STATE_FILE)

    def list_creations(self, anim_name, status=None):
        """Retourne [(dossier, state)] triés du plus ancien au plus récent."""
        root = self._creations_dir(anim_name)
        if not root.exists():
            return []
        items = []
        for d in root.iterdir():
            if not d.is_dir():
                continue
            state = self._load_state(d)
            if state is None:
                continue
            if status and state.get("status") != status:
                continue
            items.append((d, state))
        items.sort(key=lambda it: it[1].get("created_at", ""))
        return items

    def get_video_path(self, creation_dir, state):
        video = state.get("final_video")
        return (creation_dir / video) if video else None

    def _is_deliverable(self, creation_dir, state):
        video = self.get_video_path(creation_dir, state)
        return bool(video and video.exists() and video.stat().st_size > 0)

    def _expire_outdated(self, anim_name, today):
        """Une création à thème dont la fenêtre est passée ne sera jamais publiée : on libère le disque."""
        for d, s in self.list_creations(anim_name, READY):
            before = s.get("publish_before")
            if before and before < today:
                video = self.get_video_path(d, s)
                if video and video.exists():
                    video.unlink()
                s["status"] = EXPIRED
                self._save_state(d, s)
                print(f"  -> [STOCK IA] Création '{s.get('creation_id', d.name)}' expirée (fenêtre jusqu'au {before})")

    def servable(self, anim_name, today=None):
        """
        Créations READY publiables aujourd'hui : vidéo présente et fenêtre de publication respectée.
        Les créations à thème dont la fenêtre est ouverte passent en premier (la plus urgente d'abord),
        puis les créations intemporelles de la plus ancienne à la plus récente.
        """
        today = today or date.today().isoformat()
        self._expire_outdated(anim_name, today)
        items = []
        for d, s in self.list_creations(anim_name, READY):
            if not self._is_deliverable(d, s):
                continue
            if s.get("mock"):
                continue   # création de test (faux Claude) : jamais publiée
            after, before = s.get("publish_after"), s.get("publish_before")
            if after and after > today:
                continue
            if before and before < today:
                continue
            items.append((d, s))
        items.sort(key=lambda it: (0, it[1]["publish_before"]) if it[1].get("publish_before")
                   else (1, it[1].get("created_at", "")))
        return items

    # ------------------------------------------------------------------ #
    # API utilisée par le scheduler
    # ------------------------------------------------------------------ #
    def count_ready(self, anim_name):
        return len(self.servable(anim_name))

    def get_reservation(self, anim_name, job_id):
        for d, s in self.list_creations(anim_name, RESERVED):
            if s.get("reserved_by") == job_id:
                return d, s
        return None

    def can_serve(self, anim_name, job_id):
        """Vrai si ce job a déjà une réservation ou si une création READY est disponible."""
        return self.get_reservation(anim_name, job_id) is not None or self.count_ready(anim_name) > 0

    def reserve(self, anim_name, job_id):
        """
        Réserve la plus ancienne création READY pour ce job (ou renvoie la réservation existante
        si le job est repris après un redémarrage). Retourne (dossier, state) ou None.
        """
        existing = self.get_reservation(anim_name, job_id)
        if existing:
            return existing

        for d, s in self.servable(anim_name):
            s["status"] = RESERVED
            s["reserved_by"] = job_id
            s["reserved_at"] = datetime.now().isoformat(timespec="seconds")
            self._save_state(d, s)
            print(f"  -> [STOCK IA] Création '{s.get('creation_id', d.name)}' réservée pour le job {job_id}")
            return d, s
        return None

    def release(self, anim_name, job_id, reason=""):
        """Remet la création réservée par ce job dans le stock (ex. échec de publication)."""
        found = self.get_reservation(anim_name, job_id)
        if not found:
            return False
        d, s = found
        s["status"] = READY
        s.pop("reserved_by", None)
        s.pop("reserved_at", None)
        s.setdefault("release_log", []).append({
            "at": datetime.now().isoformat(timespec="seconds"),
            "job_id": job_id,
            "reason": reason,
        })
        self._save_state(d, s)
        print(f"  -> [STOCK IA] Création '{s.get('creation_id', d.name)}' remise en stock ({reason})")
        return True

    def mark_published(self, anim_name, job_id, account_name, platform, url=None):
        """
        Enregistre la publication dans l'historique de publication de la chaîne,
        supprime la vidéo du stock et passe la création en PUBLISHED.
        """
        found = self.get_reservation(anim_name, job_id)
        if not found:
            return False
        d, s = found
        now = datetime.now().isoformat(timespec="seconds")

        record = {
            "published_at": now,
            "creation_id": s.get("creation_id", d.name),
            "animation": anim_name,
            "title": s.get("title"),
            "account": account_name,
            "platform": platform,
            "job_id": job_id,
            "url": url,
        }
        history_dir = self.ai_data_dir / anim_name / "history"
        history_dir.mkdir(parents=True, exist_ok=True)
        with open(history_dir / "publications.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(record, ensure_ascii=False) + "\n")

        video = self.get_video_path(d, s)
        if video and video.exists():
            video.unlink()

        s["status"] = PUBLISHED
        s["published"] = record
        s.pop("reserved_by", None)
        self._save_state(d, s)
        print(f"  -> [STOCK IA] Création '{record['creation_id']}' publiée et retirée du stock.")
        return True
