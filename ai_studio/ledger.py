"""Grand livre des appels à l'API Claude (ai_data/usage.jsonl) et contrôle des budgets."""
import json
from datetime import datetime

from studio_common import ai_data_dir, now_iso


class BudgetExceeded(Exception):
    pass


class UsageLedger:
    def __init__(self, ai_cfg):
        self.ai_cfg = ai_cfg
        self.path = ai_data_dir() / "usage.jsonl"

    # ------------------------------------------------------------------ #
    def cost_usd(self, model, usage):
        prices = self.ai_cfg.get("pricing_usd_per_mtok", {}).get(model)
        if not prices:
            return 0.0
        return (
            usage.get("input_tokens", 0) * prices.get("input", 0)
            + usage.get("output_tokens", 0) * prices.get("output", 0)
            + usage.get("cache_read_input_tokens", 0) * prices.get("cache_read", 0)
            + usage.get("cache_creation_input_tokens", 0) * prices.get("cache_write_1h", 0)
        ) / 1_000_000

    def record(self, *, model, task, usage, animation, creation_id, mock=False, stop_reason=None):
        entry = {
            "at": now_iso(),
            "model": model,
            "task": task,
            "animation": animation,
            "creation_id": creation_id,
            "mock": mock,
            "stop_reason": stop_reason,
            **{k: int(usage.get(k, 0) or 0) for k in
               ("input_tokens", "output_tokens", "cache_read_input_tokens", "cache_creation_input_tokens")},
        }
        entry["cost_usd"] = 0.0 if mock else round(self.cost_usd(model, entry), 6)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, "a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
        return entry

    # ------------------------------------------------------------------ #
    def entries(self, month=None):
        """month = 'YYYY-MM' (défaut : mois en cours) ; '*' = tout."""
        month = month or datetime.now().strftime("%Y-%m")
        if not self.path.exists():
            return []
        out = []
        with open(self.path, "r", encoding="utf-8") as f:
            for line in f:
                try:
                    e = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if month == "*" or e.get("at", "").startswith(month):
                    out.append(e)
        return out

    def spent(self, *, animation=None, creation_id=None, month=None):
        total = 0.0
        for e in self.entries("*" if creation_id else month):
            if animation and e.get("animation") != animation:
                continue
            if creation_id and e.get("creation_id") != creation_id:
                continue
            total += e.get("cost_usd", 0.0)
        return total

    def check_budgets(self, animation, creation_id, per_creation_override=None):
        """Lève BudgetExceeded si un des trois plafonds est atteint."""
        budgets = self.ai_cfg.get("budgets_usd", {})
        per_creation = per_creation_override or budgets.get("per_creation", 1.5)
        checks = [
            (self.spent(creation_id=creation_id), per_creation, f"budget par création ({creation_id})"),
            (self.spent(animation=animation), budgets.get("per_animation_monthly", 60), f"budget mensuel de '{animation}'"),
            (self.spent(), budgets.get("global_monthly", 150), "budget mensuel global"),
        ]
        for spent, limit, label in checks:
            if spent >= limit:
                raise BudgetExceeded(f"{label} atteint : {spent:.2f} $ / {limit:.2f} $")
