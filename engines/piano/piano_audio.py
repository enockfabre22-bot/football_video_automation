"""
piano_audio.py — Rendu audio d'un vrai piano échantillonné :
MIDI (vélocités + pédale) -> FluidSynth + SoundFont Salamander Grand Piano
-> réverbération de salle par convolution -> fondus -> normalisation -14 LUFS (TikTok / YouTube).
"""
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

import mido
import numpy as np
from scipy.io import wavfile
from scipy.signal import oaconvolve

BASE_DIR = Path(__file__).resolve().parents[2]
SR = 48000


def find_soundfont():
    env = os.environ.get("PIANO_SOUNDFONT")
    if env and Path(env).exists():
        return Path(env)
    sf_dir = BASE_DIR / "assets" / "soundfonts"
    found = sorted(sf_dir.rglob("*.sf2"), key=lambda p: ("salamander" not in p.name.lower(), p.name))
    return found[0] if found else None


def find_fluidsynth():
    exe = shutil.which("fluidsynth")
    if exe:
        return exe
    env = os.environ.get("FLUIDSYNTH_PATH")
    return env if env and Path(env).exists() else None


def write_render_midi(path, notes, pedal, offset):
    """
    MIDI simplifié pour FluidSynth : 1 tick = 1 ms, un seul canal de piano.
    `offset` (s) est ajouté à tous les temps (temps vidéo = temps morceau + offset).
    """
    mid = mido.MidiFile(ticks_per_beat=1000)
    tr = mido.MidiTrack()
    mid.tracks.append(tr)
    tr.append(mido.MetaMessage("set_tempo", tempo=1_000_000, time=0))
    tr.append(mido.Message("program_change", program=0, channel=0, time=0))

    events = []   # (ms, ordre, message) : à temps égal, relâcher avant de rejouer
    for n in notes:
        t0 = max(0, int(round((n.start + offset) * 1000)))
        t1 = max(t0 + 20, int(round((n.end + offset) * 1000)))
        events.append((t0, 2, mido.Message("note_on", note=n.pitch, velocity=n.velocity, channel=0)))
        events.append((t1, 0, mido.Message("note_off", note=n.pitch, velocity=0, channel=0)))
    for t, v in pedal:
        ms = int(round((t + offset) * 1000))
        events.append((max(0, ms), 1, mido.Message("control_change", control=64, value=int(v), channel=0)))
    events.sort(key=lambda e: (e[0], e[1]))

    now = 0
    for ms, _, msg in events:
        tr.append(msg.copy(time=ms - now))
        now = ms
    mid.save(path)


def render_fluidsynth(midi_path, wav_path, soundfont, gain=0.45):
    exe = find_fluidsynth()
    if not exe:
        raise RuntimeError("fluidsynth introuvable (apt install fluidsynth, ou FLUIDSYNTH_PATH)")
    cmd = [exe, "-ni", "-q", "-g", str(gain), "-r", str(SR),
           "-R", "0", "-C", "0",                       # réverbération / chorus internes coupés
           "-o", "synth.polyphony=512",
           "-o", "audio.file.format=float",
           "-F", str(wav_path), str(soundfont), str(midi_path)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0 or not Path(wav_path).exists():
        raise RuntimeError(f"fluidsynth a échoué : {res.stderr.strip()[-500:]}")


def _hall_ir(seconds=2.4, rt60=1.8, seed=7):
    """Réponse impulsionnelle synthétique de petite salle de concert (stéréo décorrélée)."""
    rng = np.random.default_rng(seed)
    n = int(SR * seconds)
    t = np.arange(n) / SR
    decay = np.exp(-6.91 * t / rt60)
    ir = np.zeros((n, 2))
    pre = int(0.016 * SR)
    early = [(0.007, 0.55), (0.011, 0.42), (0.017, 0.35), (0.023, 0.30), (0.031, 0.24),
             (0.043, 0.18), (0.057, 0.14), (0.071, 0.10)]
    for ch in range(2):
        noise = rng.standard_normal(n)
        k = 9
        dark = np.convolve(noise, np.ones(k) / k, mode="same") * 2.2   # aigus amortis en fin de queue
        w = np.clip(t / 1.0, 0, 1)
        tail = (noise * (1 - w) * 0.6 + dark * w) * decay
        tail[:pre] = 0
        tail[pre:pre + int(0.04 * SR)] *= np.linspace(0, 1, int(0.04 * SR))
        ir[:, ch] = tail * 0.5
        for dt, g in early:
            j = int((dt + 0.004 * ch) * SR)
            if j < n:
                ir[j, ch] += g * (1 if (int(dt * 1000) + ch) % 2 else -1)
    ir /= np.sqrt(np.sum(ir ** 2) / 2)
    return ir


def _read_wav(path):
    sr, raw = wavfile.read(path)
    data = raw.astype(np.float32)
    if raw.dtype.kind in "iu":
        data /= float(np.iinfo(raw.dtype).max)
    if data.ndim == 1:
        data = np.stack([data, data], axis=1)
    if sr != SR:
        raise RuntimeError(f"Fréquence inattendue {sr} Hz")
    return data


def process(dry_wav, out_wav, duration, fade_in=0.0, fade_out=2.5, wet=0.20,
            target_lufs=-14.0, work_dir=None):
    """Réverbération + mise à la durée exacte de la vidéo + fondus + normalisation loudnorm (2 passes)."""
    dry = _read_wav(dry_wav)
    ir = _hall_ir()
    rev = np.stack([oaconvolve(dry[:, c], ir[:, c])[:len(dry)] for c in range(2)], axis=1)
    mix = dry * (1.0 - wet * 0.5) + rev * wet

    n = int(round(duration * SR))
    if len(mix) < n:
        mix = np.concatenate([mix, np.zeros((n - len(mix), 2), np.float32)])
    mix = mix[:n]
    if fade_in > 0:
        k = min(n, int(fade_in * SR))
        mix[:k] *= np.linspace(0, 1, k)[:, None]
    if fade_out > 0:
        k = min(n, int(fade_out * SR))
        curve = np.cos(np.linspace(0, np.pi / 2, k)) ** 2          # fondu doux (pas linéaire)
        mix[n - k:] *= curve[:, None]
    peak = float(np.abs(mix).max()) or 1.0
    mix = (mix / peak * 0.5).astype(np.float32)

    tmp = Path(work_dir or Path(out_wav).parent) / "audio_pre_norm.wav"
    wavfile.write(tmp, SR, mix)
    _loudnorm(tmp, out_wav, target_lufs)
    tmp.unlink(missing_ok=True)


def _loudnorm(src, dst, target):
    base = f"loudnorm=I={target}:TP=-1.0:LRA=11"
    res = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(src), "-af",
                          base + ":print_format=json", "-f", "null", "-"],
                         capture_output=True, text=True, encoding="utf-8", errors="replace")
    m = re.search(r"\{[^{}]*\"input_i\"[^{}]*\}", res.stderr, re.S)
    af = base
    if m:
        st = json.loads(m.group(0))
        af = (f"{base}:measured_I={st['input_i']}:measured_TP={st['input_tp']}:"
              f"measured_LRA={st['input_lra']}:measured_thresh={st['input_thresh']}:"
              f"offset={st['target_offset']}:linear=true")
    res = subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(src),
                          "-af", af, "-ar", str(SR), "-c:a", "pcm_s16le", str(dst)],
                         capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"ffmpeg loudnorm a échoué : {res.stderr[-400:]}")
