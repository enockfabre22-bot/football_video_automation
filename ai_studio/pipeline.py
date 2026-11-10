"""
Pipeline d'UNE création IA. Chaque étape est enregistrée dans state.json : après un arrêt
(crash, redémarrage du VPS), la création reprend à l'étape en cours sans refacturer les appels faits.

  concept -> code -> preview (compilation + aperçu + contrôles auto, boucle de correction)
          -> review (revue visuelle) -> render (steps de l'animation) -> finalize (fiche, métadonnées)
          -> READY (disponible dans le stock pour le scheduler)
"""
import json
import random
import re
import shutil
import time
import traceback
from datetime import date, datetime
from pathlib import Path

import prompts
from checks import analyze_preview, capture_frames, metrics_summary
from claude_client import BudgetExceeded, ClaudeError
from context_builder import build_stable_blocks, image_block, text_block
from events_calendar import choose_event, event_to_state, upcoming_events
from history import ChannelHistory
from media import check_final_video, extract_frames
from similarity import rank_similar
from studio_common import (BASE_DIR, ai_data_dir, ai_settings, channel_dir, load_ai_animation, log,
                           merged_limits, now_iso, read_json, write_json_atomic)

IN_PROGRESS = "IN_PROGRESS"
READY = "READY"
FAILED = "FAILED"
ABANDONED = "ABANDONED"
FILE_NAME_RE = re.compile(r"^[A-Za-z0-9_\-]+\.(cpp|h|hpp)$")
MAX_FILE_CHARS = 300_000


class CreationStop(Exception):
    """Arrêt contrôlé d'une création (statut + raison)."""

    def __init__(self, status, reason):
        super().__init__(reason)
        self.status = status
        self.reason = reason


class CreationPipeline:
    def __init__(self, anim_cfg, client, creation_dir, ai_cfg):
        self.anim = anim_cfg
        self.name = anim_cfg["name"]
        self.ai = ai_settings(anim_cfg)
        self.ai_cfg = ai_cfg
        self.limits = merged_limits(ai_cfg, anim_cfg)
        self.client = client
        self.dir = Path(creation_dir)
        self.state = read_json(self.dir / "state.json")
        self.history = ChannelHistory(self.ai["channel"])
        self._stable = None
        from sandbox_runner import SandboxRunner
        self.sandbox = SandboxRunner()

    # ------------------------------------------------------------------ #
    # Création / reprise
    # ------------------------------------------------------------------ #
    @classmethod
    def start_new(cls, anim_name, client, ai_cfg, event_name=None, no_event=False):
        anim_cfg = load_ai_animation(anim_name)
        ai = ai_settings(anim_cfg)
        creation_id = f"{anim_name}_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
        cdir = ai_data_dir() / anim_name / "creations" / creation_id
        cdir.mkdir(parents=True, exist_ok=False)

        history = ChannelHistory(ai["channel"])
        event = None
        if event_name:
            event = next((e for e in upcoming_events(ai["channel"], lead_days=366) if e["name"] == event_name), None)
            if not event:
                raise RuntimeError(f"Événement '{event_name}' inconnu ou passé")
        elif not no_event:
            event = choose_event(ai["channel"], anim_name, ai.get("events", {}), history)

        state = {
            "creation_id": creation_id,
            "animation": anim_name,
            "channel": ai["channel"],
            "profile": ai["profile"],
            "status": IN_PROGRESS,
            "step": "concept",
            "created_at": now_iso(),
            "seed": random.randint(1, 2**31 - 1),
            "mock": bool(getattr(client, "mock", False)),
            "event": event_to_state(event),
            "counters": {"concepts": 0, "fixes": 0, "reviews": 0},
            "fix_notes": [],
            "errors": [],
            "elapsed_s": 0,
        }
        write_json_atomic(cdir / "state.json", state)
        log(f"Nouvelle création {creation_id}" + (f" (événement : {event['name']})" if event else ""))
        return cls(anim_cfg, client, cdir, ai_cfg)

    @classmethod
    def resume(cls, creation_id, client, ai_cfg):
        anim_name = creation_id.rsplit("_", 2)[0]
        cdir = ai_data_dir() / anim_name / "creations" / creation_id
        if not (cdir / "state.json").exists():
            raise RuntimeError(f"Création introuvable : {cdir}")
        return cls(load_ai_animation(anim_name), client, cdir, ai_cfg)

    # ------------------------------------------------------------------ #
    def save(self):
        write_json_atomic(self.dir / "state.json", self.state)

    @property
    def fmt(self):
        return {
            "label": self.ai.get("format_label", self.name),
            "video": self.ai["video"],
            "max_bodies": int(self.ai["max_bodies"]),
            "sound": self.ai["sound"],
        }

    @property
    def src_dir(self):
        return self.dir / "src"

    def stable(self):
        if self._stable is None:
            self._stable, summary = build_stable_blocks(self.ai["channel"], self.ai["profile"])
            log(f"Contexte de la chaîne : {len(summary['examples'])} exemples, {len(summary['images'])} images, "
                f"~{summary['approx_tokens']} tokens (mis en cache)")
        return self._stable

    def call(self, task, model_key, prompt_text, schema, extra_blocks=(), stable=None):
        models = self.ai_cfg.get("models", {})
        effort = self.ai_cfg.get("effort", {}).get(task, "medium")
        max_tokens = int(self.ai_cfg.get("max_tokens", {}).get(task, 16000))
        return self.client.call(
            task=task, model=models[model_key], system=prompts.SYSTEM,
            stable_blocks=self.stable() if stable is None else stable,
            variable_blocks=[text_block(prompt_text)] + list(extra_blocks),
            schema=schema, max_tokens=max_tokens, effort=effort,
            animation=self.name, creation_id=self.state["creation_id"],
            budget_per_creation=self.ai.get("budget_per_creation_usd"),
        )

    # ------------------------------------------------------------------ #
    # Boucle principale
    # ------------------------------------------------------------------ #
    def run(self):
        st = self.state
        if st["status"] != IN_PROGRESS:
            log(f"{st['creation_id']} est déjà {st['status']}")
            return st["status"]
        max_s = float(self.limits.get("max_creation_minutes", 180)) * 60
        t0 = time.time()
        try:
            while st["status"] == IN_PROGRESS:
                if st["elapsed_s"] + (time.time() - t0) > max_s:
                    raise CreationStop(ABANDONED, f"durée maximale dépassée ({max_s / 60:.0f} min)")
                step = st["step"]
                log(f"{st['creation_id']} — étape « {step} »")
                getattr(self, f"_step_{step}")()
                self.save()
        except CreationStop as e:
            self._finish(e.status, e.reason)
        except BudgetExceeded as e:
            self._finish(ABANDONED, f"budget : {e}")
        except ClaudeError as e:
            self._finish(FAILED, f"API Claude : {e}")
        except Exception as e:  # erreur inattendue : la création échoue, le studio continue
            self._finish(FAILED, f"erreur interne : {type(e).__name__}: {e}", traceback.format_exc())
        finally:
            st["elapsed_s"] = int(st["elapsed_s"] + (time.time() - t0))
            self.save()
        return st["status"]

    def _finish(self, status, reason, detail=None):
        st = self.state
        st["status"] = status
        st["end_reason"] = reason
        st["errors"].append({"at": now_iso(), "step": st.get("step"), "error": reason, "detail": (detail or "")[-4000:]})
        log(f"{st['creation_id']} -> {status} : {reason}")
        self._cleanup(keep_final=False)
        self.history.save_card({
            "creation_id": st["creation_id"], "animation": self.name, "status": status,
            "created_at": st["created_at"], "title": (st.get("concept") or {}).get("title", ""),
            "summary": (st.get("concept") or {}).get("pitch", ""), "reason": reason,
            "mechanisms": (st.get("concept") or {}).get("mechanisms", []),
            "objects": (st.get("concept") or {}).get("objects", []),
            "tags": (st.get("concept") or {}).get("tags", []),
            "event": (st.get("event") or {}).get("name", ""),
            "cost_usd": round(self.client.ledger.spent(creation_id=st["creation_id"]), 4),
        })

    def _cleanup(self, keep_final):
        for sub in ("work", "render"):
            shutil.rmtree(self.dir / sub, ignore_errors=True)
        if not keep_final:
            final = self.dir / "final.mp4"
            if final.exists():
                final.unlink()

    # ------------------------------------------------------------------ #
    # Étape 1 : concept + anti-répétition
    # ------------------------------------------------------------------ #
    def _step_concept(self):
        st = self.state
        sim_cfg = self.ai_cfg.get("similarity", {})
        threshold = float(sim_cfg.get("reject_threshold", 0.55))
        cards = self.history.successful_cards()
        history_lines = self.history.summary_lines(int(sim_cfg.get("history_summary_count", 15)))
        rejected = []
        best = None
        for _ in range(int(self.limits.get("max_concept_attempts", 2))):
            concept = self.call("concept", "design", prompts.concept_prompt(
                fmt=self.fmt, today=date.today().isoformat(), history_lines=history_lines,
                event=st.get("event"), rejected=rejected), prompts.CONCEPT_SCHEMA)
            st["counters"]["concepts"] += 1
            ranked = rank_similar(concept, cards, top=int(sim_cfg.get("similar_cards_count", 3)))
            score = ranked[0][0] if ranked else 0.0
            log(f"Concept « {concept.get('title')} » — similarité max {score:.2f} (seuil {threshold})")
            if score < threshold:
                best = (concept, ranked)
                break
            closest = ranked[0][1]
            rejected.append(f"- « {concept.get('title')} » trop proche de « {closest.get('title')} » "
                            f"({closest.get('summary', '')}) — similarité {score:.2f}")
        if not best:
            raise CreationStop(ABANDONED, "tous les concepts proposés sont trop proches de l'existant")
        concept, ranked = best
        st["concept"] = concept
        st["similar_to"] = [{"creation_id": c.get("creation_id"), "title": c.get("title"), "score": s}
                            for s, c in ranked]
        st["step"] = "code"

    # ------------------------------------------------------------------ #
    # Étape 2 : code
    # ------------------------------------------------------------------ #
    def _step_code(self):
        res = self.call("code", "code", prompts.code_prompt(fmt=self.fmt, concept=self.state["concept"]),
                        prompts.CODE_SCHEMA)
        files = self._validate_files(res.get("files", []))
        if "scene.cpp" not in files:
            raise CreationStop(FAILED, "le code renvoyé ne contient pas scene.cpp")
        shutil.rmtree(self.src_dir, ignore_errors=True)
        self.src_dir.mkdir(parents=True)
        for name, content in files.items():
            (self.src_dir / name).write_text(content, encoding="utf-8")
        self.state["code_notes"] = res.get("notes", "")
        self.state["step"] = "preview"

    def _validate_files(self, items):
        files = {}
        for it in items:
            name = Path(it.get("path", "")).name
            content = it.get("content", "")
            if not FILE_NAME_RE.match(name):
                raise CreationStop(FAILED, f"nom de fichier refusé : {it.get('path')!r}")
            if len(content) > MAX_FILE_CHARS:
                raise CreationStop(FAILED, f"fichier trop gros : {name}")
            files[name] = content
        return files

    def _current_files(self):
        return {p.name: p.read_text(encoding="utf-8") for p in sorted(self.src_dir.glob("*")) if p.is_file()}

    def _apply_changes(self, edits, files):
        """Applique des remplacements {file, old, new} et/ou des fichiers complets. Retourne les échecs."""
        failures = []
        for name, content in self._validate_files(files).items():
            (self.src_dir / name).write_text(content, encoding="utf-8")
        for e in edits or []:
            name = Path(e.get("file", "")).name
            path = self.src_dir / name
            if not FILE_NAME_RE.match(name):
                failures.append(f"fichier refusé {e.get('file')!r}")
                continue
            text = path.read_text(encoding="utf-8") if path.exists() else ""
            count = text.count(e.get("old", "")) if e.get("old") else 0
            if count != 1:
                failures.append(f"{name} : extrait « {e.get('old', '')[:80]} » trouvé {count} fois")
                continue
            path.write_text(text.replace(e["old"], e.get("new", ""), 1), encoding="utf-8")
        return failures

    # ------------------------------------------------------------------ #
    # Étape 3 : compilation + aperçu + contrôles automatiques
    # ------------------------------------------------------------------ #
    def _video(self, preview):
        v = dict(self.ai["video"])
        if preview:
            v["preview_scale"] = float(v.get("preview_scale", 0.25))
        return v

    def _sounds_dir(self):
        if self.ai["sound"] == "events" and self.ai.get("sounds_dir"):
            p = (BASE_DIR / self.ai["sounds_dir"]).resolve()
            return p if p.exists() else None
        return None

    def _step_preview(self):
        from render_lock import render_lock
        st = self.state
        v = self._video(preview=True)
        total = int(round(float(v["duration_s"]) * int(v.get("fps", 60))))
        frames = capture_frames(total, int(v.get("fps", 60)))
        work = self.dir / "work"
        with render_lock(owner=f"studio:{st['creation_id']}", blocking=True):
            ok, out = self.sandbox.build_and_run(st["profile"], self.src_dir, work, v, st["seed"],
                                                 sounds_dir=self._sounds_dir(), mode="preview",
                                                 capture_frames=frames)
        if not ok:
            phase = self.sandbox.last_phase
            if phase == "setup":
                raise CreationStop(FAILED, f"bac à sable indisponible : {out}")
            if out == "TIMEOUT":
                problem = (f"L'aperçu (simulation complète en basse résolution) dépasse le délai maximal : "
                           f"simulation trop lourde ou boucle infinie. Réduis le coût par image.")
            elif phase == "build":
                problem = "ERREURS DE COMPILATION :\n" + _filter_compiler_output(out)
            else:
                problem = "LE PROGRAMME A PLANTÉ PENDANT L'APERÇU :\n" + _tail(out, 40)
            self._request_fix(problem)
            return

        out_dir = work / "sandbox_out"
        failures, warnings, metrics = analyze_preview(out_dir, self.fmt["max_bodies"])
        prev_dir = self.dir / "preview"
        shutil.rmtree(prev_dir, ignore_errors=True)
        prev_dir.mkdir()
        for p in out_dir.glob("frame_*.png"):
            shutil.copy2(p, prev_dir / p.name)
        if (out_dir / "metrics.json").exists():
            shutil.copy2(out_dir / "metrics.json", prev_dir / "metrics.json")
        log(f"Aperçu : {metrics_summary(metrics).splitlines()[1] if metrics else 'sans mesures'}")
        if failures:
            log("Contrôles automatiques : " + " | ".join(failures))
            self._request_fix("CONTRÔLES AUTOMATIQUES EN ÉCHEC :\n" + "\n".join(f"- {f}" for f in failures)
                              + "\n\nMESURES :\n" + metrics_summary(metrics))
            return
        st["preview"] = {"warnings": warnings, "at": now_iso()}
        st["step"] = "review"

    def _request_fix(self, problem):
        st = self.state
        signature = _signature(problem)
        if signature and signature == st.get("last_problem_signature"):
            st["same_problem_count"] = st.get("same_problem_count", 1) + 1
        else:
            st["same_problem_count"] = 1
        st["last_problem_signature"] = signature
        st["last_problem"] = problem[-3000:]
        if st["same_problem_count"] > int(self.limits.get("max_same_error_repeats", 2)):
            raise CreationStop(FAILED, "la même erreur se répète malgré les corrections")
        if st["counters"]["fixes"] >= int(self.limits.get("max_code_fixes", 3)):
            raise CreationStop(FAILED, f"nombre maximal de corrections atteint ({st['counters']['fixes']})")
        res = self.call("fix", "code", prompts.fix_prompt(
            fmt=self.fmt, concept=st["concept"], files=self._current_files(), problem=problem,
            previous_notes=st["fix_notes"]), prompts.FIX_SCHEMA)
        st["counters"]["fixes"] += 1
        failures = self._apply_changes(res.get("edits"), res.get("files"))
        note = res.get("notes", "")
        if failures:
            note += " [modifications non appliquées : " + "; ".join(failures) + "]"
        st["fix_notes"].append(note)
        log(f"Correction {st['counters']['fixes']} : {note}")

    # ------------------------------------------------------------------ #
    # Étape 4 : revue visuelle
    # ------------------------------------------------------------------ #
    def _step_review(self):
        st = self.state
        max_reviews = int(self.limits.get("max_visual_reviews", 2))
        if max_reviews <= 0:
            st["step"] = "render"
            return
        prev_dir = self.dir / "preview"
        fps = int(self.ai["video"].get("fps", 60))
        frames = sorted(prev_dir.glob("frame_*.png"))
        times, blocks = [], []
        for p in frames:
            t = f"{int(p.stem.split('_')[1]) / fps:.1f} s"
            times.append(t)
            blocks += [text_block(f"Image à t = {t}"), image_block(p, max_side=1024)]
        metrics = read_json(prev_dir / "metrics.json")
        res = self.call("review", "visual_review", prompts.review_prompt(
            fmt=self.fmt, concept=st["concept"], files=self._current_files(), frame_times=times,
            metrics_summary=metrics_summary(metrics), warnings=st.get("preview", {}).get("warnings", []),
            attempt=st["counters"]["reviews"] + 1, max_attempts=max_reviews), prompts.REVIEW_SCHEMA, blocks)
        st["counters"]["reviews"] += 1
        st.setdefault("reviews", []).append({k: res.get(k) for k in ("approved", "score", "issues", "notes")})
        log(f"Revue {st['counters']['reviews']} : approuvée={res.get('approved')} score={res.get('score')} "
            f"{'; '.join(res.get('issues', [])[:3])}")
        if res.get("approved"):
            st["step"] = "render"
            return
        if st["counters"]["reviews"] >= max_reviews:
            accept = int(self.limits.get("review_accept_score", 7))
            if int(res.get("score", 0)) >= accept:
                log(f"Revues épuisées mais score {res.get('score')} >= {accept} : rendu final quand même")
                st["step"] = "render"
                return
            raise CreationStop(FAILED, f"revue visuelle non validée (score {res.get('score')})")
        failures = self._apply_changes(res.get("edits"), [])
        if failures:
            st["fix_notes"].append("revue : modifications non appliquées : " + "; ".join(failures))
        st["fix_notes"].append("revue : " + "; ".join(res.get("issues", [])))
        st["step"] = "preview"

    # ------------------------------------------------------------------ #
    # Étape 5 : rendu final = "steps" de l'animation (comme une animation classique)
    # ------------------------------------------------------------------ #
    def _step_render(self):
        from animation_manager import AnimationManager
        from render_lock import render_lock
        st = self.state
        render_dir = self.dir / "render"
        shutil.rmtree(render_dir, ignore_errors=True)
        render_dir.mkdir(parents=True)
        final = self.dir / "final.mp4"
        if final.exists():
            final.unlink()
        job_file = render_dir / "job.json"
        write_json_atomic(job_file, {
            "job_id": st["creation_id"],
            "animation": self.name,
            "match": {"date": "", "home": "", "away": ""},
            "metadata": {"ai_concept": st["concept"]},
            "resources": {},
            "work_dir": str(render_dir.resolve()),
            "output_video": str(final.resolve()),
        })
        steps = self.anim.get("steps") or [{"type": "ai_generated", "name": "Rendu du programme IA"}]
        mgr = AnimationManager()
        with render_lock(owner=f"studio:{st['creation_id']}", blocking=True):
            for i, step in enumerate(steps, start=1):
                log(f"Rendu final — étape {i}/{len(steps)} ({step.get('type')}) : {step.get('name', '')}")
                if step.get("type") == "ai_generated":
                    ok, out = self.sandbox.build_and_run(st["profile"], self.src_dir, render_dir, self._video(False),
                                                         st["seed"], sounds_dir=self._sounds_dir(), mode="final")
                    produced = render_dir / "sandbox_out" / "video.mp4"
                    if not ok or not produced.exists():
                        raise CreationStop(FAILED, f"rendu final en échec : {_tail(str(out), 15)}")
                    shutil.move(str(produced), str(final))
                    shutil.rmtree(render_dir / "build", ignore_errors=True)
                else:
                    if not mgr._run_step(step, self.anim["folder_path"], job_file):
                        raise CreationStop(FAILED, f"étape « {step.get('name')} » en échec")
        problems = check_final_video(final, self.ai["video"], audio_required=self.ai.get("audio_required", True))
        if problems:
            raise CreationStop(FAILED, "vidéo finale non conforme : " + "; ".join(problems))
        music = read_json(render_dir / "music.json")
        if music:
            st["music"] = music.get("tracks", [])
        st["step"] = "finalize"

    # ------------------------------------------------------------------ #
    # Étape 6 : fiche + métadonnées de publication (Haiku), puis stock
    # ------------------------------------------------------------------ #
    def _step_finalize(self):
        st = self.state
        final = self.dir / "final.mp4"
        frames_dir = self.dir / "frames"
        frames = extract_frames(final, [0.15, 0.5, 0.85], frames_dir, width=1024)
        thumbs = extract_frames(final, [0.6], self.dir, prefix="thumb", width=1280)
        if thumbs:
            thumbs[0][0].replace(self.dir / "thumbnail.jpg")
        metrics = read_json(self.dir / "preview" / "metrics.json")
        music = ", ".join(st.get("music", []))
        event = st.get("event")

        card = None
        try:
            brief = (channel_dir(self.ai["channel"]) / "brief" / "brief.md").read_text(encoding="utf-8")
            blocks = []
            for p, t in frames:
                blocks += [text_block(f"Image de la vidéo finale à t = {t:.0f} s"), image_block(p, max_side=1024)]
            card = self.call("card", "history_card", prompts.card_prompt(
                fmt=self.fmt, concept=st["concept"], metrics_summary=metrics_summary(metrics),
                music=music, event=event), prompts.CARD_SCHEMA, blocks,
                stable=[text_block("# CAHIER DES CHARGES DE LA CHAÎNE\n\n" + brief)])
        except (ClaudeError, BudgetExceeded) as e:
            log(f"Fiche IA impossible ({e}) : métadonnées de secours à partir du concept")
        if not card:
            c = st["concept"]
            card = {"title": f"{c.get('title', 'Satisfying Simulation')} | Satisfying 4K",
                    "description": c.get("pitch", ""), "hashtags": ["#satisfying", "#4k", "#relaxing"],
                    "youtube_tags": c.get("tags", []), "summary": c.get("pitch", ""),
                    "mechanisms": c.get("mechanisms", []), "objects": c.get("objects", []),
                    "palette": c.get("palette", []), "tags": c.get("tags", [])}

        description = card.get("description", "").strip()
        if music:
            description += f"\n\n🎵 Music: {music}"
        metadata = {
            "title": card.get("title", "").strip()[:100],
            "description": description[:4900],
            "hashtags": [h if h.startswith("#") else f"#{h}" for h in card.get("hashtags", [])][:5],
            "youtube_tags": _limit_tags(card.get("youtube_tags", [])),
        }
        st["title"] = metadata["title"]
        st["pitch"] = st["concept"].get("pitch", "")
        st["metadata"] = metadata
        st["final_video"] = "final.mp4"
        if event:
            st["publish_after"] = event["publish_after"]
            st["publish_before"] = event["publish_before"]
        st["cost_usd"] = round(self.client.ledger.spent(creation_id=st["creation_id"]), 4)
        st["ready_at"] = now_iso()
        st["status"] = READY
        self._cleanup(keep_final=True)
        shutil.rmtree(frames_dir, ignore_errors=True)
        self.history.save_card({
            "creation_id": st["creation_id"], "animation": self.name, "status": READY,
            "created_at": st["created_at"], "title": metadata["title"],
            "working_title": st["concept"].get("title"), "summary": card.get("summary", ""),
            "pitch": st["concept"].get("pitch", ""), "scenario": st["concept"].get("scenario", []),
            "mechanisms": card.get("mechanisms", []), "objects": card.get("objects", []),
            "palette": card.get("palette", []), "tags": card.get("tags", []),
            "event": (event or {}).get("name", ""), "music": st.get("music", []),
            "counters": st["counters"], "cost_usd": st["cost_usd"], "similar_to": st.get("similar_to", []),
        })
        log(f"✅ {st['creation_id']} PRÊTE : « {metadata['title']} » ({st['cost_usd']:.2f} $)")


# ---------------------------------------------------------------------- #
def _tail(text, n):
    return "\n".join(str(text).strip().splitlines()[-n:])


def _filter_compiler_output(out, max_lines=60):
    """Garde les erreurs du compilateur / de l'éditeur de liens, sans le bruit."""
    lines = str(out).splitlines()
    keep = []
    for i, line in enumerate(lines):
        if re.search(r"error|undefined reference|fatal", line, re.I):
            keep.extend(lines[i:i + 3])
    keep = keep or lines[-max_lines:]
    seen, result = set(), []
    for line in keep:
        line = line.replace("/src/", "").replace("/runtime/", "runtime/")
        if line not in seen:
            seen.add(line)
            result.append(line)
    return "\n".join(result[:max_lines])


def _signature(problem):
    """Empreinte d'un problème sans numéros de ligne / valeurs, pour détecter les répétitions."""
    first = next((l for l in problem.splitlines()[1:] if l.strip()), problem)
    return re.sub(r"\d+", "#", first)[:200]


def _limit_tags(tags, max_chars=480):
    out, total = [], 0
    for t in tags:
        t = str(t).replace("#", "").replace(",", " ").strip()
        if not t or t in out:
            continue
        if total + len(t) + 1 > max_chars:
            break
        out.append(t)
        total += len(t) + 1
    return out
