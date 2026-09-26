import json
import shutil
from datetime import datetime
from pathlib import Path

class JobManager:
    def __init__(self):
        self.base_dir = Path(__file__).resolve().parent.parent
        self.paths = self._load_json(self.base_dir / "config" / "paths.json")
        
        self.pending_dir = self.base_dir / self.paths["jobs"]["pending"]
        self.completed_dir = self.base_dir / self.paths["jobs"]["completed"]
        self.failed_dir = self.base_dir / self.paths["jobs"]["failed"]
        self.temp_dir = self.base_dir / self.paths["output_temp"]

        for d in [self.pending_dir, self.completed_dir, self.failed_dir]:
            d.mkdir(parents=True, exist_ok=True)

    def _load_json(self, filepath):
        with open(filepath, 'r', encoding='utf-8') as f:
            return json.load(f)

    def _save_json(self, filepath, data):
        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=4, ensure_ascii=False)

    def is_match_already_completed(self, date_str, home, away, account_name):
        """
        Vérifie dans jobs/completed/ si ce match a déjà été publié pour ce compte.
        Garantit l'idempotence (Test 11).
        """
        prefix = f"{date_str}_{home.replace(' ', '_')}_{away.replace(' ', '_')}_"
        for f in self.completed_dir.glob(f"{prefix}*.json"):
            data = self._load_json(f)
            if data.get("account") == account_name and data.get("status") == "PUBLISHED":
                return True
        return False

    def find_existing_pending_job(self, date_str, home, away, account_name):
        """
        Cherche si un job est déjà en cours (ex: VIDEO_READY suite à un arrêt du serveur)
        pour reprendre sans refaire le rendu vidéo (Test 15).
        """
        prefix = f"{date_str}_{home.replace(' ', '_')}_{away.replace(' ', '_')}_"
        for f in self.pending_dir.glob(f"{prefix}*.json"):
            data = self._load_json(f)
            if data.get("account") == account_name:
                return data
        return None

    def create_job(self, match_data, anim_name, account_name):
        """Crée un nouveau job à l'état PENDING dans jobs/pending/."""
        clean_home = match_data['home'].replace(' ', '_')
        clean_away = match_data['away'].replace(' ', '_')
        job_id = f"{match_data['date']}_{clean_home}_{clean_away}_{anim_name}"

        job_file = self.pending_dir / f"{job_id}.json"
        now_str = datetime.now().isoformat(timespec="seconds")

        job_state = {
            "job_id": job_id,
            "status": "PENDING",
            "account": account_name,
            "animation": anim_name,
            "match": match_data,
            "video_path": None,
            "metadata": None,
            "error": None,
            "created_at": now_str,
            "updated_at": now_str
        }
        self._save_json(job_file, job_state)
        return job_state

    def update_status(self, job_id, status, video_path=None, metadata=None, error=None):
        """Met à jour l'état du job et le déplace dans completed/ ou failed/ si nécessaire."""
        pending_file = self.pending_dir / f"{job_id}.json"
        if not pending_file.exists():
            return None

        job_state = self._load_json(pending_file)
        job_state["status"] = status
        job_state["updated_at"] = datetime.now().isoformat(timespec="seconds")

        if video_path:
            job_state["video_path"] = str(video_path)
        if metadata:
            job_state["metadata"] = metadata
        if error:
            job_state["error"] = str(error)

        if status == "PUBLISHED":
            target_file = self.completed_dir / f"{job_id}.json"
            self._save_json(target_file, job_state)
            pending_file.unlink()
            self.cleanup_temp(job_id)
        elif status == "FAILED":
            target_file = self.failed_dir / f"{job_id}.json"
            self._save_json(target_file, job_state)
            pending_file.unlink()
        else:
            self._save_json(pending_file, job_state)

        return job_state

    def cleanup_temp(self, job_id):
        """Supprime les frames temporaires une fois le job terminé pour économiser le disque du VPS."""
        job_temp = self.temp_dir / job_id
        if job_temp.exists():
            shutil.rmtree(job_temp, ignore_errors=True)
            print(f"[CLEANUP] Dossier temporaire nettoyé : {job_id}")