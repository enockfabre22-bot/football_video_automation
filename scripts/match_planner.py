import json
import subprocess
import tkinter as tk
from tkinter import ttk, messagebox
from pathlib import Path
from datetime import datetime, timedelta

try:
    from PIL import Image, ImageTk
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

BASE_DIR = Path(__file__).resolve().parent.parent
LOGOS_DIR = BASE_DIR / "assets" / "logos"
MATCHES_FILE = BASE_DIR / "data" / "matches.json"
ANIMATIONS_DIR = BASE_DIR / "animations"


class MatchPlannerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("⚽ Planificateur de Matchs — Football Video Automation")
        self.root.geometry("1020x650")
        self.root.configure(bg="#1e1e2e")

        # Chargement des ressources
        self.teams = self.load_available_teams()
        self.animations = self.load_available_animations()
        self.raw_data, self.matches_list, self.schema_info = self.load_matches_data()

        self.selected_date = (datetime.now() + timedelta(days=1)).strftime("%Y-%m-%d")
        self.logo_cache = {}

        self.build_ui()
        self.refresh_calendar_list()
        self.select_date_in_list(self.selected_date)

    def load_available_teams(self):
        """Lit tous les fichiers .png dans assets/logos/."""
        if not LOGOS_DIR.exists():
            return []
        return sorted([p.stem for p in LOGOS_DIR.glob("*.png")], key=lambda s: s.lower())

    def load_available_animations(self):
        """Lit tous les dossiers d'animations disponibles."""
        if not ANIMATIONS_DIR.exists():
            return ["animation_01"]
        anims = sorted([d.name for d in ANIMATIONS_DIR.iterdir() if d.is_dir() and not d.name.startswith("_")])
        return anims if anims else ["animation_01"]

    def load_matches_data(self):
        """Charge data/matches.json et détecte automatiquement ses clés."""
        default_schema = {
            "root_type": "dict_matches",
            "date_key": "date",
            "team1_key": "team1",
            "team2_key": "team2",
            "anim_key": "animation",
            "comp_key": "competition",
            "template": {
                "date": "2026-09-27",
                "team1": "Nantes",
                "team2": "PSG",
                "competition": "Ligue 1",
                "animation": "animation_01"
            }
        }

        if not MATCHES_FILE.exists():
            return {"matches": []}, [], default_schema

        try:
            with open(MATCHES_FILE, "r", encoding="utf-8") as f:
                raw = json.load(f)
        except Exception:
            return {"matches": []}, [], default_schema

        if isinstance(raw, list):
            matches = raw
            default_schema["root_type"] = "list"
        elif isinstance(raw, dict) and "matches" in raw:
            matches = raw["matches"]
            default_schema["root_type"] = "dict_matches"
        else:
            matches = []

        # Détection automatique des clés à partir du 1er match existant
        if matches and isinstance(matches[0], dict):
            sample = matches[0]
            default_schema["template"] = dict(sample)
            for k in ["home_team", "team1", "team_1", "team_a", "home"]:
                if k in sample:
                    default_schema["team1_key"] = k
                    break
            for k in ["away_team", "team2", "team_2", "team_b", "away"]:
                if k in sample:
                    default_schema["team2_key"] = k
                    break
            for k in ["animation", "animation_id"]:
                if k in sample:
                    default_schema["anim_key"] = k
                    break
            for k in ["competition", "league"]:
                if k in sample:
                    default_schema["comp_key"] = k
                    break

        return raw, matches, default_schema

    def save_matches_to_disk(self):
        """Enregistre immédiatement les modifications dans data/matches.json."""
        MATCHES_FILE.parent.mkdir(parents=True, exist_ok=True)
        # Tri par date
        date_k = self.schema_info["date_key"]
        self.matches_list.sort(key=lambda m: m.get(date_k, ""))

        if self.schema_info["root_type"] == "list":
            data_to_save = self.matches_list
        else:
            if not isinstance(self.raw_data, dict):
                self.raw_data = {}
            self.raw_data["matches"] = self.matches_list
            data_to_save = self.raw_data

        with open(MATCHES_FILE, "w", encoding="utf-8") as f:
            json.dump(data_to_save, f, indent=2, ensure_ascii=False)

    def build_ui(self):
        style = ttk.Style()
        style.theme_use("clam")

        # --- COLONNE GAUCHE : CALENDRIER (30 JOURS) ---
        left_frame = tk.Frame(self.root, bg="#252538", width=280, padx=10, pady=10)
        left_frame.pack(side=tk.LEFT, fill=tk.Y)

        tk.Label(
            left_frame, text="📅 CALENDRIER DES JOURS",
            bg="#252538", fg="#ffffff", font=("Segoe UI", 11, "bold")
        ).pack(anchor="w", pady=(0, 5))

        tk.Label(
            left_frame, text="Rappel : le bot prépare à J-1 les\nmatchs de la date du lendemain (J+1).",
            bg="#252538", fg="#a6adc8", font=("Segoe UI", 8), justify="left"
        ).pack(anchor="w", pady=(0, 8))

        self.calendar_listbox = tk.Listbox(
            left_frame, width=32, height=25, bg="#1e1e2e", fg="#cdd6f4",
            selectbackground="#89b4fa", selectforeground="#11111b",
            font=("Consolas", 10), bd=0, highlightthickness=0
        )
        self.calendar_listbox.pack(fill=tk.BOTH, expand=True)
        self.calendar_listbox.bind("<<ListboxSelect>>", self.on_date_selected)

        # Champ date personnalisée
        custom_date_frame = tk.Frame(left_frame, bg="#252538")
        custom_date_frame.pack(fill=tk.X, pady=(8, 0))
        tk.Label(custom_date_frame, text="Date (YYYY-MM-DD):", bg="#252538", fg="#cdd6f4", font=("Segoe UI", 9)).pack(side=tk.LEFT)
        self.custom_date_var = tk.StringVar(value=self.selected_date)
        tk.Entry(custom_date_frame, textvariable=self.custom_date_var, width=12).pack(side=tk.LEFT, padx=5)
        tk.Button(
            custom_date_frame, text="Aller", bg="#45475a", fg="white", bd=0, padx=6,
            command=self.go_to_custom_date
        ).pack(side=tk.LEFT)

        # --- COLONNE DROITE : AJOUT & LISTE DES MATCHS ---
        right_frame = tk.Frame(self.root, bg="#1e1e2e", padx=15, pady=10)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)

        self.header_label = tk.Label(
            right_frame, text=f"Matchs du {self.selected_date}",
            bg="#1e1e2e", fg="#89b4fa", font=("Segoe UI", 14, "bold")
        )
        self.header_label.pack(anchor="w", pady=(0, 10))

        # --- ZONE DE SÉLECTION DES ÉQUIPES ---
        selector_frame = tk.LabelFrame(
            right_frame, text=f" ➕ Ajouter un match ({len(self.teams)} logos détectés dans assets/logos/) ",
            bg="#252538", fg="#ffffff", font=("Segoe UI", 10, "bold"), padx=12, pady=12
        )
        selector_frame.pack(fill=tk.X, pady=(0, 15))

        teams_grid = tk.Frame(selector_frame, bg="#252538")
        teams_grid.pack(fill=tk.X)

        # Équipe 1 (Domicile)
        col1 = tk.Frame(teams_grid, bg="#252538")
        col1.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=5)
        tk.Label(col1, text="🏠 Équipe 1 (Domicile)", bg="#252538", fg="#a6e3a1", font=("Segoe UI", 10, "bold")).pack()
        self.search1_var = tk.StringVar()
        self.search1_var.trace_add("write", lambda *_: self.filter_teams(1))
        tk.Entry(col1, textvariable=self.search1_var, font=("Segoe UI", 10)).pack(fill=tk.X, pady=3)
        self.team1_combo = ttk.Combobox(col1, values=self.teams, state="readonly", font=("Segoe UI", 10))
        self.team1_combo.pack(fill=tk.X, pady=3)
        self.team1_combo.bind("<<ComboboxSelected>>", lambda e: self.update_logo_preview(1))
        self.logo1_label = tk.Label(col1, text="[Logo 1]", bg="#1e1e2e", fg="#6c7086", width=12, height=4)
        self.logo1_label.pack(pady=5)

        # VS
        tk.Label(teams_grid, text="⚡ VS ⚡", bg="#252538", fg="#f9e2af", font=("Segoe UI", 13, "bold")).pack(side=tk.LEFT, padx=10)

        # Équipe 2 (Extérieur)
        col2 = tk.Frame(teams_grid, bg="#252538")
        col2.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=5)
        tk.Label(col2, text="✈️ Équipe 2 (Extérieur)", bg="#252538", fg="#f38ba8", font=("Segoe UI", 10, "bold")).pack()
        self.search2_var = tk.StringVar()
        self.search2_var.trace_add("write", lambda *_: self.filter_teams(2))
        tk.Entry(col2, textvariable=self.search2_var, font=("Segoe UI", 10)).pack(fill=tk.X, pady=3)
        self.team2_combo = ttk.Combobox(col2, values=self.teams, state="readonly", font=("Segoe UI", 10))
        self.team2_combo.pack(fill=tk.X, pady=3)
        self.team2_combo.bind("<<ComboboxSelected>>", lambda e: self.update_logo_preview(2))
        self.logo2_label = tk.Label(col2, text="[Logo 2]", bg="#1e1e2e", fg="#6c7086", width=12, height=4)
        self.logo2_label.pack(pady=5)

        # Options supplémentaires (Compétition + Animation)
        opts_frame = tk.Frame(selector_frame, bg="#252538")
        opts_frame.pack(fill=tk.X, pady=(8, 0))

        tk.Label(opts_frame, text="Compétition :", bg="#252538", fg="#cdd6f4").pack(side=tk.LEFT)
        self.comp_var = tk.StringVar(value="Ligue 1")
        comp_combo = ttk.Combobox(
            opts_frame, textvariable=self.comp_var, width=18,
            values=["Ligue 1", "Premier League", "La Liga", "Serie A", "Bundesliga", "Champions League", "Football"]
        )
        comp_combo.pack(side=tk.LEFT, padx=(5, 15))

        tk.Label(opts_frame, text="Animation :", bg="#252538", fg="#cdd6f4").pack(side=tk.LEFT)
        self.anim_var = tk.StringVar(value=self.animations[0])
        anim_combo = ttk.Combobox(opts_frame, textvariable=self.anim_var, values=self.animations, state="readonly", width=15)
        anim_combo.pack(side=tk.LEFT, padx=5)

        tk.Button(
            opts_frame, text="➕ AJOUTER CE MATCH", bg="#a6e3a1", fg="#11111b",
            font=("Segoe UI", 10, "bold"), bd=0, padx=15, pady=6, cursor="hand2",
            command=self.add_match
        ).pack(side=tk.RIGHT)

        # Sélection par défaut
        if len(self.teams) >= 2:
            self.team1_combo.current(0)
            self.team2_combo.current(1)
            self.update_logo_preview(1)
            self.update_logo_preview(2)

        # --- LISTE DES MATCHS PROGRAMMÉS CE JOUR ---
        list_frame = tk.LabelFrame(
            right_frame, text=" 📋 Matchs programmés pour la date sélectionnée ",
            bg="#252538", fg="#ffffff", font=("Segoe UI", 10, "bold"), padx=10, pady=10
        )
        list_frame.pack(fill=tk.BOTH, expand=True)

        self.day_matches_listbox = tk.Listbox(
            list_frame, bg="#1e1e2e", fg="#cdd6f4", selectbackground="#f38ba8",
            font=("Consolas", 11), bd=0, highlightthickness=0
        )
        self.day_matches_listbox.pack(fill=tk.BOTH, expand=True, pady=(0, 8))

        bottom_btns = tk.Frame(list_frame, bg="#252538")
        bottom_btns.pack(fill=tk.X)

        tk.Button(
            bottom_btns, text="🗑️ Supprimer le match sélectionné", bg="#f38ba8", fg="#11111b",
            font=("Segoe UI", 9, "bold"), bd=0, padx=12, pady=6, cursor="hand2",
            command=self.delete_selected_match
        ).pack(side=tk.LEFT)

        tk.Button(
            bottom_btns, text="🚀 Sauvegarder & Push sur GitHub", bg="#89b4fa", fg="#11111b",
            font=("Segoe UI", 9, "bold"), bd=0, padx=14, pady=6, cursor="hand2",
            command=self.git_push_changes
        ).pack(side=tk.RIGHT)

    def filter_teams(self, side):
        query = (self.search1_var.get() if side == 1 else self.search2_var.get()).strip().lower()
        filtered = [t for t in self.teams if query in t.lower()] if query else self.teams
        combo = self.team1_combo if side == 1 else self.team2_combo
        combo["values"] = filtered
        if filtered:
            combo.current(0)
            self.update_logo_preview(side)

    def update_logo_preview(self, side):
        team = self.team1_combo.get() if side == 1 else self.team2_combo.get()
        label = self.logo1_label if side == 1 else self.logo2_label
        logo_path = LOGOS_DIR / f"{team}.png"

        if not HAS_PIL or not logo_path.exists():
            label.config(image="", text=f"{team}.png", width=15, height=4)
            return

        try:
            img = Image.open(logo_path).convert("RGBA")
            img.thumbnail((64, 64))
            tk_img = ImageTk.PhotoImage(img)
            self.logo_cache[f"{side}_{team}"] = tk_img
            label.config(image=tk_img, text="", width=70, height=70)
        except Exception:
            label.config(image="", text=f"{team}.png")

    def refresh_calendar_list(self):
        """Génère les 30 prochains jours avec le compteur de matchs par jour."""
        self.calendar_listbox.delete(0, tk.END)
        self.calendar_dates = []
        date_k = self.schema_info["date_key"]

        days_fr = ["Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim"]
        today = datetime.now().date()

        for offset in range(-1, 30):
            d = today + timedelta(days=offset)
            d_str = d.strftime("%Y-%m-%d")
            self.calendar_dates.append(d_str)

            count = sum(1 for m in self.matches_list if m.get(date_k) == d_str)
            day_name = days_fr[d.weekday()]
            badge = f"🟢 [{count}]" if count > 0 else "⚪ [0]"
            tag = " (Demain)" if offset == 1 else (" (Auj.)" if offset == 0 else "")
            self.calendar_listbox.insert(tk.END, f"{badge} {d_str} ({day_name}){tag}")

    def select_date_in_list(self, date_str):
        self.selected_date = date_str
        self.custom_date_var.set(date_str)
        if date_str in self.calendar_dates:
            idx = self.calendar_dates.index(date_str)
            self.calendar_listbox.selection_clear(0, tk.END)
            self.calendar_listbox.selection_set(idx)
            self.calendar_listbox.see(idx)
        self.refresh_day_matches()

    def on_date_selected(self, event):
        sel = self.calendar_listbox.curselection()
        if not sel:
            return
        self.selected_date = self.calendar_dates[sel[0]]
        self.custom_date_var.set(self.selected_date)
        self.refresh_day_matches()

    def go_to_custom_date(self):
        val = self.custom_date_var.get().strip()
        try:
            datetime.strptime(val, "%Y-%m-%d")
            self.select_date_in_list(val)
        except ValueError:
            messagebox.showerror("Format invalide", "Utilise le format YYYY-MM-DD (ex: 2026-10-04).")

    def refresh_day_matches(self):
        self.header_label.config(text=f"📅 Matchs programmés pour le : {self.selected_date}")
        self.day_matches_listbox.delete(0, tk.END)
        self.current_day_indices = []

        date_k = self.schema_info["date_key"]
        t1_k = self.schema_info["team1_key"]
        t2_k = self.schema_info["team2_key"]
        anim_k = self.schema_info["anim_key"]
        comp_k = self.schema_info["comp_key"]

        for idx, m in enumerate(self.matches_list):
            if m.get(date_k) == self.selected_date:
                self.current_day_indices.append(idx)
                t1 = m.get(t1_k, "?")
                t2 = m.get(t2_k, "?")
                comp = m.get(comp_k, "")
                anim = m.get(anim_k, "animation_01")
                comp_str = f" | 🏆 {comp}" if comp else ""
                self.day_matches_listbox.insert(tk.END, f"⚽ {t1}  VS  {t2}{comp_str}  ({anim})")

    def add_match(self):
        t1 = self.team1_combo.get().strip()
        t2 = self.team2_combo.get().strip()
        if not t1 or not t2:
            messagebox.showwarning("Équipe manquante", "Sélectionne bien les deux équipes.")
            return
        if t1 == t2:
            messagebox.showwarning("Même équipe", "Choisis deux équipes différentes !")
            return

        date_k = self.schema_info["date_key"]
        t1_k = self.schema_info["team1_key"]
        t2_k = self.schema_info["team2_key"]
        anim_k = self.schema_info["anim_key"]
        comp_k = self.schema_info["comp_key"]

        # Vérifier si le match existe déjà ce jour-là
        for m in self.matches_list:
            if m.get(date_k) == self.selected_date and m.get(t1_k) == t1 and m.get(t2_k) == t2:
                messagebox.showinfo("Déjà présent", f"Le match {t1} VS {t2} est déjà programmé le {self.selected_date}.")
                return

        new_match = dict(self.schema_info["template"])
        new_match[date_k] = self.selected_date
        new_match[t1_k] = t1
        new_match[t2_k] = t2
        new_match[anim_k] = self.anim_var.get()
        if comp_k in new_match or self.comp_var.get():
            new_match[comp_k] = self.comp_var.get()

        # Si le template contient un ID ou des chemins de logos, on les met à jour proprement
        if "id" in new_match:
            new_match["id"] = f"{self.selected_date}_{t1}_{t2}"
        for lk1 in ["logo1", "home_logo", "team1_logo"]:
            if lk1 in new_match:
                new_match[lk1] = f"assets/logos/{t1}.png"
        for lk2 in ["logo2", "away_logo", "team2_logo"]:
            if lk2 in new_match:
                new_match[lk2] = f"assets/logos/{t2}.png"

        self.matches_list.append(new_match)
        self.save_matches_to_disk()
        self.refresh_calendar_list()
        self.select_date_in_list(self.selected_date)

    def delete_selected_match(self):
        sel = self.day_matches_listbox.curselection()
        if not sel:
            messagebox.showinfo("Sélection", "Clique d'abord sur un match à supprimer dans la liste.")
            return
        real_idx = self.current_day_indices[sel[0]]
        removed = self.matches_list.pop(real_idx)
        self.save_matches_to_disk()
        self.refresh_calendar_list()
        self.select_date_in_list(self.selected_date)

    def git_push_changes(self):
        """Envoie data/matches.json et assets/logos/ sur GitHub en 1 clic."""
        self.save_matches_to_disk()
        try:
            subprocess.run(["git", "add", "data/matches.json", "assets/logos/"], cwd=BASE_DIR, check=True)
            subprocess.run(["git", "commit", "-m", "chore: mise a jour du calendrier des matchs et logos"], cwd=BASE_DIR)
            subprocess.run(["git", "push", "origin", "main"], cwd=BASE_DIR, check=True)
            messagebox.showinfo(
                "✅ Synchronisé sur GitHub !",
                "Ton calendrier data/matches.json et tes logos ont bien été envoyés sur GitHub.\n\n"
                "Il te suffit de faire 'git pull origin main' sur ton VPS !"
            )
        except Exception as e:
            messagebox.showwarning(
                "Sauvegardé localement",
                f"Le fichier data/matches.json est bien enregistré sur ton PC.\n(Info Git : {e})"
            )


if __name__ == "__main__":
    root = tk.Tk()
    app = MatchPlannerApp(root)
    root.mainloop()