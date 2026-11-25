import json
import shutil
from datetime import datetime
from pathlib import Path

from job_manager import JobManager

BASE_DIR = Path(__file__).resolve().parent.parent

# Statuts d'un job dont le rendu n'est pas terminé (aucune vidéo exploitable)
UNFINISHED = {"PENDING", "RUNNING"}


def _load_app_config():
    with open(BASE_DIR / "config" / "app.json", "r", encoding="utf-8") as f:
        return json.load(f)


# Rendus lancés à la main (scripts/test_animation.py -> test_*, scripts/publish_now.py -> manual_*) : ils n'ont pas
# de job en attente, mais le démon (redémarré juste avant, ou préparation quotidienne) ne doit pas effacer leur
# dossier en plein rendu. Ils ne sont supprimés qu'après MANUAL_MAX_AGE_H heures (rendu planté et abandonné).
MANUAL_PREFIXES = ("test_", "manual_")
MANUAL_MAX_AGE_H = 12


def _is_recent_manual(path):
    if not path.name.startswith(MANUAL_PREFIXES):
        return False
    try:
        age_h = (datetime.now().timestamp() - path.stat().st_mtime) / 3600
    except OSError:
        return False
    return age_h < MANUAL_MAX_AGE_H


def _size_gb(path):
    if path.is_file():
        return path.stat().st_size / 1024**3
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file()) / 1024**3


def free_disk_gb():
    return shutil.disk_usage(BASE_DIR).free / 1024**3


def has_enough_disk_space(animation_config=None):
    """
    Garde-fou avant un rendu : vérifie qu'il reste assez d'espace disque.
    Seuil = 'min_free_disk_gb' de l'animation si défini, sinon celui de config/app.json.
    """
    threshold = None
    if animation_config:
        threshold = animation_config.get("min_free_disk_gb")
    if threshold is None:
        threshold = _load_app_config().get("min_free_disk_gb", 15)

    free = free_disk_gb()
    if free < threshold:
        print(f"[ERREUR DISQUE] Espace libre insuffisant : {free:.1f} Go < {threshold} Go requis. Rendu annulé.")
        return False
    return True


def cleanup_disk():
    """
    Nettoyage appelé au démarrage du démon et avant chaque préparation quotidienne
    (moments où aucun rendu n'est en cours dans le scheduler) :
      1. jobs non terminés (PENDING/RUNNING) dont la date est passée -> jobs/failed/ (jamais repris)
      2. jobs non terminés restants -> dossier temporaire vidé (le rendu repartira de zéro proprement)
      3. dossiers output/temporary/ et vidéos output/videos/ qui n'appartiennent à aucun job en attente
    """
    from main import discard_job  # import local : évite un import circulaire

    job_mgr = JobManager()
    temp_dir = job_mgr.temp_dir
    videos_dir = BASE_DIR / "output" / "videos"
    today = datetime.now().strftime("%Y-%m-%d")
    freed = 0.0

    # 1 & 2. Jobs dont le rendu a été interrompu (plantage, disque plein, redémarrage...)
    for job in job_mgr.list_pending_jobs():
        if job.get("status") not in UNFINISHED:
            continue
        job_id = job["job_id"]
        job_date = (job.get("match") or {}).get("date", "")
        job_temp = temp_dir / job_id
        if job_temp.exists():
            freed += _size_gb(job_temp)

        if job_date and job_date <= today:
            discard_job(job, reason="Rendu interrompu et jamais repris (nettoyage automatique)")
        elif job_temp.exists():
            shutil.rmtree(job_temp, ignore_errors=True)
            print(f"[MAINTENANCE] Images partielles supprimées pour le job interrompu : {job_id}")

    # 2 bis. Jobs dont la vidéo est déjà prête : leurs images intermédiaires ne servent plus
    for job in job_mgr.list_pending_jobs():
        if job.get("status") in UNFINISHED:
            continue
        frames_dir = temp_dir / job["job_id"] / "frames"
        if frames_dir.exists():
            freed += _size_gb(frames_dir)
            shutil.rmtree(frames_dir, ignore_errors=True)
            print(f"[MAINTENANCE] Images intermédiaires supprimées (vidéo déjà prête) : {job['job_id']}")

    # 3. Fichiers orphelins
    kept_ids = {job["job_id"] for job in job_mgr.list_pending_jobs()}

    if temp_dir.exists():
        for d in temp_dir.iterdir():
            if d.name not in kept_ids and not _is_recent_manual(d):
                freed += _size_gb(d)
                if d.is_dir():
                    shutil.rmtree(d, ignore_errors=True)
                else:
                    d.unlink(missing_ok=True)
                print(f"[MAINTENANCE] Dossier temporaire orphelin supprimé : {d.name}")

    if videos_dir.exists():
        for f in videos_dir.iterdir():
            if f.is_file() and f.stem not in kept_ids and not _is_recent_manual(f):
                freed += _size_gb(f)
                f.unlink(missing_ok=True)
                print(f"[MAINTENANCE] Vidéo orpheline supprimée : {f.name}")

    print(f"[MAINTENANCE] Nettoyage terminé : {freed:.2f} Go libérés, {free_disk_gb():.1f} Go libres sur le disque.")
