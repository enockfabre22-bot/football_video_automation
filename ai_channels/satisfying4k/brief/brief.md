# Cahier des charges — Chaîne « Satisfying 4K »

> Ce fichier est lu par l'IA (Claude) avant CHAQUE création pour cette chaîne.
> Écris-le en français ou en anglais, avec des phrases claires et concrètes.
> Remplace chaque bloc « À COMPLÉTER » ; supprime les exemples qui ne te conviennent pas.
> Plus c'est précis (chiffres, couleurs, exemples), meilleures seront les créations.

---

## 1. Identité de la chaîne
- **Concept** : simulations physiques 2D en 4K, satisfaisantes et relaxantes. Des animations
  « colorful chaos » (des centaines d'objets colorés qui s'entrechoquent) et « satisfying order »
  (le chaos qui se range, se trie, s'aligne ou remplit parfaitement une forme).
- **Public / ressenti recherché** :  (ex. détente, fascination, envie de regarder jusqu'à la fin,)
- **Références** : vidéos 4K « satisfying physics simulation, unity 4k animation ,unity 4k animation factory,Physics Simulation unity 4K, Tetris Shredding Machine 2 4K » de YouTube (captures dans `images/`).



## 2. Formats
| | Vidéo longue | Short |
|---|---|---|
| Résolution | 3840×2160 (16:9) | 2160×3840 (9:16) |
| Durée | 1 à 10 min (actuellement ~2 min) | 30 à 60 s |
| FPS | 60 | 60 |

- **Vidéo longue** :  (ex. plusieurs phases qui s'enchaînent ? une seule évolution lente ?, usine )
- **Short** :  ( accroche dans les 2 premières secondes, fin qui boucle avec le début, vidéo satisfaissante, Tetris Shredding Machine 2 4K, )

## 3. Thèmes et mécanismes autorisés
À COMPLÉTER. Exemples à garder, modifier ou supprimer :
- billes qui tombent dans des formes, entonnoirs, labyrinthes, planches à clous ;
- objets qui se trient par couleur ou par taille ;
- remplissage progressif d'une forme (cercle, lettre, logo) jusqu'à la perfection ;
- courses de billes, éliminations, « dernier survivant » ;
- rotations, engrenages, portes qui s'ouvrent, obstacles mobiles ;
- explosions / dispersions suivies d'un retour à l'ordre.
- rangements (color filter, forme filter, )
- Marble Hue Cycle Factory
- satisfying machine 
- Tetris Shredding Machine 2 4K
- satisfyng évolution machine ( évolution simulate)
- Machine de construction 
-  particles simulations 
- etc...

## 4. Style visuel
- **Couleurs** : À COMPLÉTER (ex. dégradés arc-en-ciel, néons sur fond sombre, palettes pastel, fond sombre)
- **Fond** : À COMPLÉTER (ex. uni sombre, dégradé, jamais d'image)
- **Formes** : À COMPLÉTER (cercles surtout, carrés/polygones autorisés, formes sec, partcules, tout autres forme, piston, vis, engrenage)
- **Effets** :  ( vaut mieux éviter les effet il faut une animation SEC, les effet peuvent gacher l'animation)
- **Texte à l'écran** :  (aucun texte, sauf si l'animation le demande bien sûr, si le texte permet de suivre l'animation (course etc...))

## 5. Mouvement, interactions et physique
- À COMPLÉTER (ex. mouvements fluides, jamais saccadés ; collisions crédibles ; pas d'objets qui
  traversent les murs ; rebonds modérés ; gravité réaliste ou réduite)
- Nombre d'objets : voir les limites techniques (section 8).

## 6. Déroulement (le « scénario » de la vidéo)
. Exemple :
1. Début calme et lisible (l'idée se comprend en 3 secondes).
2. Montée en intensité (de plus en plus d'objets, de mouvement, de couleurs).
3. Moment fort / point culminant.
4. Fin satisfaisante : ordre parfait, forme complétée, ou image finale nette.

## 7. Son
- Le programme ne produit **aucun son**. Une **musique de fond relaxante** est ajoutée
  automatiquement après le rendu (tirée au hasard dans `assets/music/satisfying4k/`).
- Les événements visuels n'ont donc pas besoin d'être synchronisés avec un son.

## 8. Technologies et contraintes techniques
- **C++17 + raylib 5.5 + Box2D 2.4.2 (API C++ : b2World, CreateFixture…)**, en respectant le contrat du runtime (`anim_runtime.h`) :
  scène avec `init / update(dt) / draw`, pas de temps fixe, coordonnées logiques, `ctx.randf()`.
- **Limites actuelles du serveur** (rendu logiciel, 3 cœurs) : À COMPLÉTER / ajuster
  (proposition : 1 500 corps Box2D maximum, pas de shaders lourds, pas de flou plein écran).
- Les dessins doivent rester nets en 4K : épaisseurs de traits et tailles pensées pour 3840 px.

## 9. Critères de réussite
À COMPLÉTER. Exemples :
- l'idée se comprend sans texte en moins de 3 secondes ;
- l'écran est bien rempli, rien d'important ne sort du cadre ;
- aucun objet coincé, qui tremble ou qui traverse un mur ;
- la fin est clairement satisfaisante (ordre, remplissage, symétrie) ;
- chaque vidéo est différente des précédentes par son MÉCANISME, pas seulement ses couleurs.


## 10. À éviter absolument
À COMPLÉTER. Exemples :
- simple changement de couleurs d'une animation déjà faite ;
- écran vide ou presque pendant plus de 2 secondes ;
- objets trop petits pour être vus sur téléphone (Shorts) ;
- clignotements rapides / flashs ;
- texte inutile, logos, marques, personnages existants.
- animation avec trop d'effet trainé ( i faut des objet sec, et nette )

## 11. Exemples fournis
Décris ici chaque fichier de `examples/` et `images/` et ce que l'IA doit en retenir :
- `examples/....cpp` : À COMPLÉTER (ex. « bon exemple de remplissage progressif, garder ce rythme »)
- `images/....png` : À COMPLÉTER (ex. « palette et densité d'objets à viser »)
