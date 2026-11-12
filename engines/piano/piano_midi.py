"""
piano_midi.py — Lecture d'un fichier MIDI de piano en données exploitables :
notes (secondes, pitch, vélocité, main), pédale de sustain, mesures.
Puis : choix de l'extrait (refrain), humanisation des nuances, pédale automatique.
"""
import bisect
import hashlib
from dataclasses import dataclass, field

import mido
import numpy as np


@dataclass
class Note:
    start: float
    end: float
    pitch: int
    velocity: int
    track: int
    hand: int = 0          # 0 = main droite, 1 = main gauche


@dataclass
class Song:
    notes: list
    pedal: list            # [(temps_s, valeur 0..127)] triés
    bars: list             # débuts de mesure en secondes
    length: float          # fin de la dernière note
    has_pedal: bool = False
    flat_velocity: bool = False
    info: dict = field(default_factory=dict)


# --------------------------------------------------------------------------- #
# Lecture
# --------------------------------------------------------------------------- #
class TempoMap:
    """Conversion ticks -> secondes, valable pour toutes les pistes (tempos du fichier entier)."""

    def __init__(self, mid):
        self.tpb = mid.ticks_per_beat
        changes = {}
        for track in mid.tracks:
            tick = 0
            for msg in track:
                tick += msg.time
                if msg.type == "set_tempo":
                    changes[tick] = msg.tempo
        if 0 not in changes:
            changes[0] = 500000
        self.ticks = sorted(changes)
        self.tempos = [changes[t] for t in self.ticks]
        self.secs = [0.0]
        for i in range(1, len(self.ticks)):
            dt = self.ticks[i] - self.ticks[i - 1]
            self.secs.append(self.secs[-1] + dt * self.tempos[i - 1] / 1e6 / self.tpb)

    def sec(self, tick):
        i = bisect.bisect_right(self.ticks, tick) - 1
        return self.secs[i] + (tick - self.ticks[i]) * self.tempos[i] / 1e6 / self.tpb


def load_song(path):
    mid = mido.MidiFile(path, clip=True)
    tmap = TempoMap(mid)
    notes, pedal, time_sigs = [], [], {}
    max_tick = 0

    for ti, track in enumerate(mid.tracks):
        tick = 0
        open_notes = {}   # (canal, pitch) -> [(tick_debut, vélocité)]
        for msg in track:
            tick += msg.time
            if msg.type == "time_signature":
                time_sigs[tick] = (msg.numerator, msg.denominator)
            if not hasattr(msg, "channel") or msg.channel == 9:   # canal 10 = batterie
                continue
            if msg.type == "note_on" and msg.velocity > 0:
                open_notes.setdefault((msg.channel, msg.note), []).append((tick, msg.velocity))
            elif msg.type in ("note_off", "note_on"):
                stack = open_notes.get((msg.channel, msg.note))
                if stack:
                    t0, vel = stack.pop(0)
                    notes.append((t0, tick, msg.note, vel, ti))
            elif msg.type == "control_change" and msg.control == 64:
                pedal.append((tmap.sec(tick), msg.value))
        max_tick = max(max_tick, tick)
        for (ch, pitch), stack in open_notes.items():   # notes jamais relâchées
            for t0, vel in stack:
                notes.append((t0, t0 + tmap.tpb, pitch, vel, ti))

    out = []
    for t0, t1, pitch, vel, ti in notes:
        s0, s1 = tmap.sec(t0), tmap.sec(t1)
        if s1 - s0 < 0.03:
            s1 = s0 + 0.08
        out.append(Note(s0, s1, pitch, vel, ti))
    out.sort(key=lambda n: (n.start, n.pitch))
    if not out:
        raise ValueError(f"Aucune note dans {path}")

    length = max(n.end for n in out)
    bars = _bar_starts(time_sigs, tmap, length)
    _assign_hands(out)
    pedal.sort()
    distinct = len({n.velocity for n in out})
    return Song(notes=out, pedal=pedal, bars=bars, length=length,
                has_pedal=any(v >= 64 for _, v in pedal),
                flat_velocity=distinct <= 3,
                info={"tracks": len(mid.tracks), "distinct_velocities": distinct})


def _bar_starts(time_sigs, tmap, length):
    sigs = sorted(time_sigs.items()) or [(0, (4, 4))]
    if sigs[0][0] != 0:
        sigs.insert(0, (0, (4, 4)))
    bars, tick, i = [], 0, 0
    while True:
        while i + 1 < len(sigs) and sigs[i + 1][0] <= tick:
            i += 1
        num, den = sigs[i][1]
        bar_ticks = max(1, int(num * tmap.tpb * 4 / den))
        sec = tmap.sec(tick)
        if sec > length + 0.01:
            break
        bars.append(sec)
        nxt = tick + bar_ticks
        if i + 1 < len(sigs) and sigs[i + 1][0] < nxt:
            nxt = sigs[i + 1][0]
        tick = nxt
        if len(bars) > 5000:
            break
    return bars


def _assign_hands(notes):
    """Main droite / gauche : par piste quand le fichier sépare les mains, sinon par hauteur."""
    by_track = {}
    for n in notes:
        by_track.setdefault(n.track, []).append(n.pitch)
    tracks = [t for t, p in by_track.items() if len(p) >= 8]
    if len(tracks) >= 2:
        means = {t: float(np.mean(by_track[t])) for t in by_track}
        lo, hi = min(means[t] for t in tracks), max(means[t] for t in tracks)
        if hi - lo >= 5:
            mid = (lo + hi) / 2
            for n in notes:
                n.hand = 0 if means[n.track] >= mid else 1
            return
    _split_by_pitch(notes)


def _split_by_pitch(notes):
    """Une seule piste : point de séparation glissant (médiane locale), borné autour du do central."""
    pitches = np.array([n.pitch for n in notes])
    starts = np.array([n.start for n in notes])
    for i, n in enumerate(notes):
        lo, hi = np.searchsorted(starts, [n.start - 2.0, n.start + 2.0])
        split = float(np.clip(np.median(pitches[lo:hi]), 55, 65))
        n.hand = 0 if n.pitch >= split else 1


# --------------------------------------------------------------------------- #
# Choix de l'extrait : le passage le plus répété et le plus intense (le refrain)
# --------------------------------------------------------------------------- #
def _bar_features(song, bars):
    nb = len(bars) - 1
    feats = np.zeros((nb, 12 + 12 + 16))
    energy = np.zeros(nb)
    rh_pitch = np.zeros(nb)
    for n in song.notes:
        b = bisect.bisect_right(bars, n.start) - 1
        if b < 0 or b >= nb:
            continue
        dur = bars[b + 1] - bars[b]
        w = min(n.end, bars[b + 1]) - n.start
        if n.hand == 0:
            feats[b, n.pitch % 12] += w
            rh_pitch[b] = max(rh_pitch[b], n.pitch)
        else:
            feats[b, 12 + n.pitch % 12] += 0.5 * w
        pos = int(16 * (n.start - bars[b]) / max(dur, 1e-6))
        feats[b, 24 + min(15, max(0, pos))] += 0.35
        energy[b] += n.velocity / 127.0
    norms = np.linalg.norm(feats, axis=1, keepdims=True)
    feats = feats / np.maximum(norms, 1e-9)
    durs = np.diff(bars)
    energy = energy / np.maximum(durs, 1e-6)
    return feats, energy, rh_pitch


def choose_excerpt(song, max_seconds, min_seconds=25.0):
    """
    Retourne (début, fin, explication). Le début tombe sur une barre de mesure :
    l'extrait commence directement sur le passage qui revient le plus (le refrain),
    pour que la vidéo accroche dès la première seconde.
    """
    first = song.notes[0].start
    if song.length - first <= max_seconds:
        return first, song.length, "morceau complet (plus court que la limite)"

    bars = [b for b in song.bars if b < song.length]
    bars.append(song.length)
    if len(bars) < 10:
        return first, first + max_seconds, "trop peu de mesures : début du morceau"

    feats, energy, rh_pitch = _bar_features(song, bars)
    nb = len(feats)
    bar_dur = float(np.median(np.diff(bars)))
    L = int(np.clip(round(10.0 / max(bar_dur, 0.3)), 2, 8))     # segment d'environ 10 s
    C = feats @ feats.T
    ns = nb - L + 1
    S = np.zeros((ns, ns))
    for k in range(L):
        S += C[k:k + ns, k:k + ns]
    S /= L

    active = energy > 0
    seg_energy = np.array([energy[i:i + L].mean() for i in range(ns)])
    seg_active = np.array([active[i:i + L].mean() for i in range(ns)])
    seg_pitch = np.array([rh_pitch[i:i + L].mean() for i in range(ns)])

    thr = 0.88
    rep = np.zeros(ns)
    for i in range(ns):
        others = [j for j in range(ns) if abs(i - j) >= L and S[i, j] >= thr]
        # occurrences distinctes (pas des segments qui se chevauchent)
        distinct, last = 0, -10 ** 9
        for j in others:
            if j - last >= L:
                distinct += 1
                last = j
        rep[i] = distinct

    def norm(x):
        x = np.asarray(x, dtype=float)
        rng = x.max() - x.min()
        return (x - x.min()) / rng if rng > 1e-9 else np.zeros_like(x)

    score = 0.55 * norm(np.minimum(rep, 4)) + 0.30 * norm(seg_energy) + 0.15 * norm(seg_pitch)
    score[seg_active < 0.75] -= 1.0                                   # pas de silences dans le crochet
    latest_start = song.length - min_seconds
    for i in range(ns):
        if bars[i] > latest_start or bars[i] < first - 0.01:
            score[i] -= 2.0
        else:
            # pénalité douce si l'extrait complet ne tient plus avant la fin du morceau
            fit = (song.length - bars[i]) / max_seconds
            if fit < 1:
                score[i] -= 0.3 * (1 - fit)

    best = int(np.argmax(score))
    # première occurrence de ce passage (le premier refrain plutôt que le dernier),
    # tant qu'elle est presque aussi intense et sans trou
    cluster = [j for j in range(ns)
               if (j == best or S[best, j] >= thr) and bars[j] <= latest_start and bars[j] >= first - 0.01
               and seg_active[j] >= 0.75 and seg_energy[j] >= 0.7 * seg_energy[best]]
    start_bar = min(cluster) if cluster else best
    reason = (f"refrain détecté (mesure {start_bar + 1}, répété {int(rep[best])}x, "
              f"segment {L} mesures)" if rep[best] >= 1 else "passage le plus intense")

    s0 = bars[start_bar]
    # première note du passage (pas d'attente si la mesure commence par un silence)
    following = [n.start for n in song.notes if n.start >= s0 - 0.01]
    if following and following[0] - s0 > 0.25:
        s0 = following[0]
    s1 = min(song.length, s0 + max_seconds)
    end_bars = [b for b in bars if s0 + min_seconds <= b <= s1 + 0.01]
    if end_bars and s1 < song.length:
        s1 = end_bars[-1]
    # fin de morceau trop proche : on recule le début pour remplir la durée
    if s1 - s0 < max_seconds * 0.8 and s1 >= song.length - 0.01:
        cands = [b for b in bars if b >= first and s1 - b <= max_seconds]
        if cands:
            s0 = max(first, cands[0])
    return s0, s1, reason


# --------------------------------------------------------------------------- #
# Interprétation : nuances humaines + pédale automatique
# --------------------------------------------------------------------------- #
def _chords(notes, tol=0.03):
    groups, cur = [], []
    for n in notes:
        if cur and n.start - cur[0].start > tol:
            groups.append(cur)
            cur = []
        cur.append(n)
    if cur:
        groups.append(cur)
    return groups


def humanize_velocities(song, seed_text):
    """
    Les MIDI de tutoriels ont souvent toutes les notes à la même force (80) : un vrai piano
    sonne alors mécanique. On recrée une interprétation : mélodie en avant, accords intérieurs
    plus doux, accents de temps forts, phrasé selon la densité, légère variation aléatoire.
    """
    rng = np.random.default_rng(int(hashlib.md5(seed_text.encode()).hexdigest()[:8], 16))
    notes = song.notes
    base_vel = np.median([n.velocity for n in notes])
    starts = np.array([n.start for n in notes])
    # densité locale -> phrasé (passages riches un peu plus forts)
    dens = np.array([np.searchsorted(starts, s + 2.0) - np.searchsorted(starts, s - 2.0) for s in starts], float)
    lo, hi = np.percentile(dens, 10), np.percentile(dens, 90)
    phr = np.clip((dens - lo) / max(hi - lo, 1e-6), 0, 1) * 14 - 7
    index = {id(n): i for i, n in enumerate(notes)}
    bars = song.bars

    for group in _chords(notes):
        rh = sorted([n for n in group if n.hand == 0], key=lambda n: n.pitch)
        lh = sorted([n for n in group if n.hand == 1], key=lambda n: n.pitch)
        b = max(0, bisect.bisect_right(bars, group[0].start) - 1)
        bar_len = (bars[b + 1] - bars[b]) if b + 1 < len(bars) else 2.0
        frac = (group[0].start - bars[b]) / max(bar_len, 1e-6) if bars else 0.5
        if frac < 0.03 or frac > 0.97:
            accent = 6
        elif min(abs(frac * 4 - round(frac * 4)), 1) < 0.06:
            accent = 2
        else:
            accent = -2
        for n in group:
            v = 68.0 + accent + phr[index[id(n)]]
            if n.hand == 0:
                v += 13 if rh and n is rh[-1] else -7
            else:
                v += 2 if lh and n is lh[0] else -8
            v += 0.5 * (n.velocity - base_vel)          # garde les petites nuances d'origine
            v += rng.normal(0, 3)
            n.velocity = int(np.clip(round(v), 28, 115))


def auto_pedal(song):
    """
    Pédale de sustain « qui suit l'harmonie » : relevée puis reprise à chaque nouvelle basse
    (main gauche) et au minimum à chaque mesure. Donne le liant d'un vrai pianiste.
    """
    changes = set()
    for group in _chords(song.notes):
        if any(n.hand == 1 for n in group):
            changes.add(round(group[0].start, 3))
    for b in song.bars:
        changes.add(round(b, 3))
    ev = []
    for t in sorted(changes):
        ev.append((max(0.0, t - 0.005), 0))
        ev.append((t + 0.06, 100))
    # relâche aux longs silences
    starts = [n.start for n in song.notes]
    for a, b in zip(starts, starts[1:]):
        if b - a > 3.0:
            ev.append((a + 2.0, 0))
    ev.append((song.length + 0.5, 0))
    ev.sort()
    song.pedal = ev
    song.has_pedal = True


def pedal_state_at(pedal, t):
    v = 0
    for pt, pv in pedal:
        if pt > t:
            break
        v = pv
    return v
