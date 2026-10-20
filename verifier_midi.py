import os
import glob
from pathlib import Path

# Chemin vers ton dossier contenant les 400 fichiers MIDI
dossier_midi = Path("assets/midi")

if not dossier_midi.exists():
    print(f"❌ Le dossier {dossier_midi} n'existe pas.")
    exit()

fichiers_midi = list(dossier_midi.glob("*.mid"))
fichiers_invalides = []

for fichier in fichiers_midi:
    nom_sans_ext = fichier.stem
    
    # On vérifie s'il y a un tiret dans le nom
    if "-" not in nom_sans_ext:
        fichiers_invalides.append(fichier.name)

print("==================================================")
print(f"🔍 ANALYSE DE {len(fichiers_midi)} FICHIERS MIDI")
print("==================================================")

if len(fichiers_invalides) > 0:
    print(f"⚠️ ATTENTION : {len(fichiers_invalides)} fichier(s) n'ont pas de tiret (-) dans leur nom :\n")
    for nom in fichiers_invalides:
        print(f"  -> {nom}")
    print("\n👉 Renomme ces fichiers sous le format 'Titre - Auteur.mid' pour que la miniature soit parfaite.")
else:
    print(f"✅ PARFAIT ! Les {len(fichiers_midi)} fichiers respectent tous le format avec un tiret.")
    print("👉 Tu peux tout envoyer sur ton serveur l'esprit tranquille !")