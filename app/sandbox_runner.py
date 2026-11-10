import json
import os
import random
import shutil
import subprocess
import time
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
PROFILES_DIR = BASE_DIR / "sandbox" / "profiles"

DEFAULT_SANDBOX_CFG = {
    "cpus": 3,
    "memory": "5g",
    "pids_limit": 256,
    "gpu": False,
    "build_timeout_seconds": 300,
    "preview_timeout_seconds": 600,
    "final_timeout_seconds": 7200,
}


class SandboxRunner:
    """
    Compile et exécute un programme d'animation dans un conteneur Docker isolé :
      - aucun réseau (--network none), système de fichiers en lecture seule (--read-only),
      - aucune variable d'environnement de l'hôte (donc aucune clé API / OAuth),
      - seuls les dossiers nécessaires sont montés : sources (lecture seule), runtime (lecture seule),
        sons (lecture seule) et le dossier de travail du job (seul dossier en écriture),
      - CPU, mémoire, nombre de processus et durée limités ; jamais exécuté en root.
    Utilisé par l'étape "sandbox" des animations (manuelles) et, plus tard, par le studio IA.
    """

    def __init__(self):
        self.last_phase = None
        cfg = dict(DEFAULT_SANDBOX_CFG)
        ai_cfg_path = BASE_DIR / "config" / "ai.json"
        if ai_cfg_path.exists():
            with open(ai_cfg_path, "r", encoding="utf-8") as f:
                cfg.update(json.load(f).get("sandbox", {}))
        self.cfg = cfg

    # ------------------------------------------------------------------ #
    # Outils
    # ------------------------------------------------------------------ #
    @staticmethod
    def load_profile(name):
        profile_dir = PROFILES_DIR / name
        with open(profile_dir / "profile.json", "r", encoding="utf-8") as f:
            profile = json.load(f)
        profile["dir"] = profile_dir
        return profile

    @staticmethod
    def image_exists(image):
        try:
            res = subprocess.run(["docker", "image", "inspect", image], capture_output=True, text=True)
            return res.returncode == 0
        except FileNotFoundError:
            return False

    def _docker_cmd(self, image, mounts, command, container_name):
        cmd = [
            "docker", "run", "--rm", "--name", container_name,
            "--network", "none",
            "--read-only",
            "--tmpfs", "/tmp:rw,exec,size=1g",
            "--memory", str(self.cfg["memory"]), "--memory-swap", str(self.cfg["memory"]),
            "--cpus", str(self.cfg["cpus"]),
            "--pids-limit", str(self.cfg["pids_limit"]),
            "--security-opt", "no-new-privileges",
            "--cap-drop", "ALL",
        ]
        if hasattr(os, "getuid"):
            cmd += ["--user", f"{os.getuid()}:{os.getgid()}"]
        if self.cfg.get("gpu"):
            cmd += ["--gpus", "all"]
        for host_path, container_path, read_only in mounts:
            spec = f"{Path(host_path).resolve()}:{container_path}" + (":ro" if read_only else "")
            cmd += ["-v", spec]
        cmd += [image, "bash", "-c", command]
        return cmd

    def _run(self, cmd, container_name, timeout, label):
        print(f"      [SANDBOX] {label} (limite {timeout} s)...")
        start = time.time()
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                                 errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired:
            subprocess.run(["docker", "kill", container_name], capture_output=True)
            print(f"      [SANDBOX ERREUR] {label} : délai de {timeout} s dépassé, conteneur arrêté.")
            return False, "TIMEOUT"
        except FileNotFoundError:
            print("      [SANDBOX ERREUR] Docker n'est pas installé ou introuvable dans le PATH.")
            return False, "NO_DOCKER"

        output = (res.stdout or "") + (res.stderr or "")
        elapsed = time.time() - start
        if res.returncode != 0:
            tail = "\n".join(output.strip().splitlines()[-40:])
            print(f"      [SANDBOX ERREUR] {label} : code {res.returncode} après {elapsed:.0f} s\n{tail}")
            return False, output
        for line in output.strip().splitlines():
            if line.startswith("[RUNTIME]"):
                print(f"      {line}")
        print(f"      [SANDBOX] {label} terminé en {elapsed:.0f} s")
        return True, output

    # ------------------------------------------------------------------ #
    # Compilation + exécution
    # ------------------------------------------------------------------ #
    def build_and_run(self, profile_name, source_dir, work_dir, video, seed,
                      sounds_dir=None, mode="final", capture_frames=None):
        """
        Compile les sources puis exécute le programme. Les sorties sont dans <work_dir>/sandbox_out/ :
        video.mp4 (mode final), frame_XXXXXX.png (images capturées), events.jsonl, metrics.json.
        Retourne (succès, détail_erreur). self.last_phase indique l'étape atteinte : "setup", "build" ou "run".
        """
        self.last_phase = "setup"
        profile = self.load_profile(profile_name)
        image = profile["image"]
        if not self.image_exists(image):
            msg = (f"Image Docker '{image}' absente. Construis-la avec : bash scripts/build_sandbox.sh {profile_name}")
            print(f"      [SANDBOX ERREUR] {msg}")
            return False, msg

        work_dir = Path(work_dir)
        out_dir = work_dir / "sandbox_out"
        if out_dir.exists():
            shutil.rmtree(out_dir)
        out_dir.mkdir(parents=True)
        (work_dir / "build").mkdir(exist_ok=True)

        width, height = int(video.get("width", 1080)), int(video.get("height", 1920))
        scale = float(video.get("preview_scale", 0.5)) if mode == "preview" else 1.0
        lines = {
            "mode": mode,
            "render_width": int(width * scale) // 2 * 2,
            "render_height": int(height * scale) // 2 * 2,
            "logical_width": width,
            "logical_height": height,
            "fps": int(video.get("fps", 60)),
            "duration": float(video.get("duration_s", 30)),
            "seed": int(seed),
            "crf": int(video.get("crf", 18)),
            "out_dir": "/work/sandbox_out",
            "sounds_dir": "/sounds" if sounds_dir else "",
            "capture_frames": ",".join(str(f) for f in (capture_frames or [])),
        }
        with open(work_dir / "sandbox_job.txt", "w", encoding="utf-8") as f:
            for k, v in lines.items():
                f.write(f"{k}={v}\n")

        mounts = [
            (source_dir, "/src", True),
            (profile["dir"] / "runtime", "/runtime", True),
            (work_dir, "/work", False),
        ]
        if sounds_dir:
            mounts.append((sounds_dir, "/sounds", True))

        tag = f"anim-{work_dir.name[-40:]}-{int(time.time())}".replace(" ", "_").lower()
        tag = "".join(c if c.isalnum() or c in "-_." else "-" for c in tag)

        self.last_phase = "build"
        ok, out = self._run(self._docker_cmd(image, mounts, profile["build_cmd"], tag + "-build"),
                            tag + "-build", int(self.cfg["build_timeout_seconds"]), "Compilation")
        if not ok:
            return False, out

        self.last_phase = "run"
        timeout_key = "preview_timeout_seconds" if mode == "preview" else "final_timeout_seconds"
        label = "Aperçu" if mode == "preview" else "Rendu final"
        ok, out = self._run(self._docker_cmd(image, mounts, profile["run_cmd"], tag + "-run"),
                            tag + "-run", int(self.cfg[timeout_key]), label)
        return ok, out

    def run_animation_step(self, step, anim_folder, job_file_path):
        """Étape "sandbox" d'une animation : rendu final puis copie vers output_video du job."""
        with open(job_file_path, "r", encoding="utf-8") as f:
            job = json.load(f)
        work_dir = Path(job["work_dir"])
        output_video = Path(job["output_video"])

        source_dir = Path(anim_folder) / step.get("source", "scene")
        if not source_dir.exists():
            print(f"      [SANDBOX ERREUR] Dossier source introuvable : {source_dir}")
            return False

        sounds_dir = None
        if step.get("sounds_dir"):
            sounds_dir = (Path(anim_folder) / step["sounds_dir"]).resolve()
            if not sounds_dir.exists():
                print(f"      [SANDBOX AVERTISSEMENT] Dossier de sons introuvable : {sounds_dir} (vidéo sans son)")
                sounds_dir = None

        seed = step.get("seed")
        if seed is None or seed == "random":
            seed = random.randint(1, 2**31 - 1)
        print(f"      [SANDBOX] Profil '{step.get('profile', 'raylib_box2d')}', seed {seed}")

        ok, _ = self.build_and_run(
            step.get("profile", "raylib_box2d"), source_dir, work_dir,
            step.get("video", {}), seed, sounds_dir=sounds_dir, mode="final",
        )
        if not ok:
            return False

        produced = work_dir / "sandbox_out" / "video.mp4"
        if not produced.exists() or produced.stat().st_size == 0:
            print(f"      [SANDBOX ERREUR] Le programme n'a produit aucune vidéo : {produced}")
            return False
        output_video.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(produced, output_video)
        return True
