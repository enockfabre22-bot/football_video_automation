import os
from contextlib import contextmanager
from pathlib import Path

LOCK_PATH = Path(__file__).resolve().parent.parent / "output" / ".render.lock"

if os.name == "nt":
    import msvcrt

    def _try_lock(fd, blocking):
        mode = msvcrt.LK_LOCK if blocking else msvcrt.LK_NBLCK
        while True:
            try:
                msvcrt.locking(fd, mode, 1)
                return True
            except OSError:
                if not blocking:
                    return False
                # LK_LOCK abandonne après ~10 s : on recommence tant que blocking=True

    def _unlock(fd):
        try:
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
else:
    import fcntl

    def _try_lock(fd, blocking):
        try:
            fcntl.flock(fd, fcntl.LOCK_EX if blocking else fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False

    def _unlock(fd):
        fcntl.flock(fd, fcntl.LOCK_UN)


@contextmanager
def render_lock(owner, blocking):
    """
    Verrou partagé entre le scheduler et le studio IA pour éviter deux rendus lourds simultanés.

    - Le scheduler l'utilise avec blocking=False : il ne bloque JAMAIS (la publication est prioritaire).
      S'il ne l'obtient pas, il rend quand même (plus lentement) et le signale dans les logs.
    - Le studio IA l'utilise avec blocking=True : il attend que le scheduler ait fini ses rendus.

    Renvoie True si le verrou a été obtenu.
    """
    LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(str(LOCK_PATH), os.O_RDWR | os.O_CREAT)
    acquired = False
    try:
        acquired = _try_lock(fd, blocking)
        if not acquired:
            print(f"  [RENDER LOCK] Un autre rendu est en cours ; '{owner}' continue sans attendre.")
        yield acquired
    finally:
        if acquired:
            _unlock(fd)
        os.close(fd)
