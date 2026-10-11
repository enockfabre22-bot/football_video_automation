import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

def generate_custom_thumbnail(source_image_path, output_image_path, text, font_path=None):
    """
    Génère une miniature en écrivant un texte centré sur une image source.
    """
    try:
        # 1. Charger l'image de fond
        img = Image.open(source_image_path).convert("RGBA")
        draw = ImageDraw.Draw(img)
        img_w, img_h = img.size

        # 2. Charger la police
        # Si aucune police n'est fournie, on essaie de charger Arial, sinon police par défaut
        try:
            if font_path and os.path.exists(font_path):
                font = ImageFont.truetype(font_path, int(img_h * 0.12)) # Taille dynamique (12% de la hauteur)
            else:
                font = ImageFont.truetype("arialbd.ttf", int(img_h * 0.12))
        except IOError:
            font = ImageFont.load_default()

        # 3. Calculer la position du texte pour le centrer
        try:
            # Méthode pour les versions récentes de Pillow
            bbox = draw.textbbox((0, 0), text, font=font)
            text_w = bbox[2] - bbox[0]
            text_h = bbox[3] - bbox[1]
        except AttributeError:
            # Fallback pour les anciennes versions
            text_w, text_h = draw.textsize(text, font=font)

        x = (img_w - text_w) / 2
        y = (img_h - text_h) / 2

        # 4. Dessiner le contour/ombre pour la lisibilité
        outline_color = "black"
        outline_width = 4
        for dx in range(-outline_width, outline_width + 1):
            for dy in range(-outline_width, outline_width + 1):
                draw.text((x + dx, y + dy), text, font=font, fill=outline_color)

        # 5. Dessiner le texte principal
        draw.text((x, y), text, font=font, fill="white")

        # 6. Sauvegarder l'image finale en JPEG (format attendu par YouTube)
        final_img = img.convert("RGB")
        final_img.save(output_image_path, "JPEG", quality=90)
        
        return str(output_image_path)

    except Exception as e:
        print(f"❌ [ERREUR MINIATURE] Échec de la génération : {e}")
        return None

if __name__ == "__main__":
    # Petit test local si tu exécutes ce fichier directement
    print("Test du générateur de miniature...")
    # Il faudra une image test.png dans le même dossier pour que ça marche
    # generate_custom_thumbnail("test.png", "test_out.jpg", "Arsenal VS Chelsea")