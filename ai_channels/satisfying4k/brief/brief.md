# Cahier des charges — Chaîne « Satisfying 4K »

> Ce fichier est lu par l'IA (Claude) avant CHAQUE création pour cette chaîne.
> Les règles marquées **OBLIGATOIRE** ne se discutent pas ; le reste est une direction artistique.

---

## 0. RÈGLE N°1 — TENIR LE SPECTATEUR JUSQU'AU BOUT (OBLIGATOIRE)
Les statistiques de la chaîne sont claires : les vidéos qui marchent (« Can MARBLES solve the
maze! », « Ball Simulation: Colors Filter » : ~50 % regardés, 80 % des vues) ont **un objectif
visible et une issue qu'on veut connaître**. Les machines qui tournent en boucle sans enjeu
(« usines » de 3-5 min) ne sont regardées qu'à 20 % : au bout de 30 secondes, on a tout vu.
Chaque vidéo DOIT donc avoir :
- **une accroche dès la première seconde** : l'objectif ou la question se comprend
  immédiatement (un labyrinthe à résoudre, une forme à remplir, des couleurs à trier, une tour
  qui va-t-elle tenir, une course, un dernier survivant, un compteur à atteindre…) ;
- **une issue incertaine** jusqu'à la fin : le spectateur se demande « est-ce que ça va réussir ?
  qui va gagner ? quand ça va-t-il être plein ? » ;
- **une progression visible** : on voit qu'on se rapproche du but (niveau qui monte, étapes
  franchies, objets restants qui diminuent, chaos qui se range peu à peu) ;
- **des rebondissements** réguliers (nouvelle phase, obstacle, accélération, retournement) pour
  relancer l'attention toutes les 15-30 s dans les vidéos longues ;
- **une récompense finale** claire et satisfaisante (but atteint, ordre parfait, victoire,
  forme complétée) — puis la vidéo se termine (`ctx.finish()`).
**Interdit** : une boucle qui tourne indéfiniment sans enjeu, une machine qui « fait juste
quelque chose » sans but, une fin qui ressemble au début sans que rien n'ait été accompli.

## 1. Identité de la chaîne — À LIRE EN PREMIER
- **Le but, et le seul but** : des **animations / simulations 2D satisfaisantes, relaxantes et de
  très haute qualité**, en 4K. **TOUT est permis** pour y arriver : n'importe quel phénomène
  physique, objet, forme, palette, composition, histoire ou durée. Chaque création part d'une
  **feuille blanche**.
- Les machines à billes, les usines et l'arc-en-ciel ne sont qu'**UNE** possibilité parmi des
  centaines (et elles ont déjà été beaucoup faites sur la chaîne). Exemples d'autres univers tout
  aussi bienvenus : dominos géants, pendules, cordes et ponts suspendus, gelée / corps mous,
  sable qui dessine, tissu qui ondule, tours qui s'empilent puis s'effondrent au ralenti,
  horlogerie, orbites, aimants, mosaïques qui se composent, eau imitée par des milliers de grains,
  équilibres fragiles, chaînes de réactions… et tout ce que tu peux imaginer d'autre.
- Ressentis qui marchent : la **tension puis le soulagement**, « satisfying order » (le chaos qui
  se range, s'aligne, se construit ou remplit parfaitement une forme), la compétition (course,
  équipes de couleurs, dernier survivant), le défi réussi de justesse.
- **Public / ressenti recherché** : détente et fascination. Le spectateur doit pouvoir regarder sans
  rien comprendre de technique, suivre un objet des yeux, et avoir envie de voir « ce qui se passe
  ensuite » jusqu'à la fin. Vidéos aussi utilisées en fond (étude, relaxation).
- **Références** : vidéos YouTube « satisfying physics simulation 4K », « oddly satisfying
  animation », « relaxing simulation » (quelques captures dans `images/` montrent le niveau de
  finition visé, pas des thèmes à reprendre).

## 2. Formats
| | Vidéo longue | Short |
|---|---|---|
| Résolution | 3840×2160 (16:9) | 2160×3840 (9:16) |
| Durée | **1 min 30 à 4 min** (selon l'histoire) | **15 à 35 s** |
| FPS | 60 | 60 |

- **La durée n'est pas fixe** : la vidéo dure le temps que l'histoire (objectif → progression →
  récompense) se déroule, et s'arrête dès que le but est atteint (`ctx.finish()`). Jamais de
  remplissage : une minute de trop fait chuter le pourcentage regardé.
- **Vidéo longue** : occupe bien l'écran 16:9 ; plusieurs phases ou rebondissements vers un but
  final (comme « Can MARBLES solve the maze! », 2 min 56, regardée en moyenne 1 min 23).
- **Short** : un défi simple et lisible, objets plus gros, composition verticale. Accroche dans la
  première seconde, récompense à la fin, et **dernière image qui raccorde avec la première** pour
  que la vidéo soit revue en boucle (les Shorts de 46 s n'étaient regardés qu'à 60 %).

## 3. Thèmes et mécanismes
**Aucune liste fermée.** Quelques familles possibles parmi d'autres, à mélanger librement :
- réactions en chaîne (dominos, objets qui déclenchent des mécanismes), pendules, balanciers ;
- cordes, chaînes, ponts, filets, tissus (joints Box2D), corps mous (ressorts entre particules) ;
- construction / empilement / effondrement au ralenti ;
- remplissage parfait d'une forme, d'une silhouette, d'un contenant ; strates ;
- tri et rangement (couleur, taille, forme) ; mélange puis séparation ;
- sable, grains, « liquides » imités par des milliers de petites particules ;
- mécanismes : engrenages, cames, pistons, roues, convoyeurs, ascenseurs, horlogerie ;
- champs de force, aimants, orbites, tourbillons ; rebonds qui dessinent des motifs ;
- parcours, labyrinthes, courses (texte seulement s'il aide à suivre) ;
- machines à billes et usines (déjà beaucoup faites : seulement avec une idée vraiment nouvelle).

## 4. Style visuel
- **Couleurs** : **palette choisie pour chaque vidéo**, cohérente et belle : pastels, néons sur
  noir, monochrome, tons terre, couleurs primaires, or et noir, océan, coucher de soleil…
  **L'arc-en-ciel n'est qu'une palette parmi d'autres** : à éviter s'il a été utilisé récemment.
  Les couleurs doivent être lisibles et harmonieuses ; les éléments fixes (murs, structures)
  restent généralement plus sobres que les objets en mouvement.
- **Fond** : uni ou très léger dégradé. Deux familles qui marchent :
  - sombre : `{12,16,22}`, `{15,18,22}`, `{24,27,34}`, gris moyen `{104,106,110}` ;
  - clair : crème `{252,250,246}`, gris clair `{235,240,245}`, `{245,245,245}`.
  Jamais d'image de fond, jamais de motif chargé.
- **Formes** : toutes (cercles, polygones, rectangles, formes composées, segments, cordes,
  particules…), toujours pleines et nettes, avec des contours propres.
- **Effets** : **aucun** ou presque. Animation SÈCHE et NETTE : pas de traînées, pas de flou, pas
  de glow, pas de particules décoratives, pas de tremblement de caméra. Un petit reflet sur les
  billes (point clair) est autorisé.
- **Caméra** : fixe par défaut, toute la scène importante visible.
- **Texte à l'écran** : aucun par défaut. Autorisé — et même conseillé — s'il renforce l'enjeu
  (compteur d'objets restants, pourcentage de remplissage, score d'une course, chronomètre),
  alors lisible mais discret, police simple. Jamais de compteur de debug
  (FPS, « BALLS 120/1800 », « ENTITIES »…).

## 5. Mouvement, interactions et physique
L'objectif n°1 est une animation **ultra propre, sans aucun défaut visible**. La liberté créative
est totale sur le concept, le scénario et la manière dont les objets arrivent ; la qualité
d'exécution, elle, est **OBLIGATOIRE** :
- **aucun objet ne traverse un mur**, une pièce de machine ou un autre objet (murs assez épais,
  pas de vitesses énormes, `bullet` pour les petits objets rapides) ;
- **aucun objet bloqué** : pas de bourrage dans un goulot, pas de bille coincée entre deux pièces,
  pas de tas immobile qui arrête le flux (goulots assez larges, débit maîtrisé, pentes suffisantes) ;
- **aucun objet qui tremble**, vibre ou s'agite sans raison quand il est au repos ;
- **aucun objet qui sort du cadre ou du circuit** par erreur ; ce qui est important reste visible ;
- mouvement fluide et continu, jamais saccadé ; pièces mobiles à vitesse douce et régulière
  (pas d'accélérations brusques ni de pièces qui s'emballent) ;
- collisions crédibles : rebonds plutôt faibles, friction réaliste, gravité réaliste
  (≈ -10 m/s²) ou légèrement réduite.

Libre choix (selon le concept) :
- les objets peuvent être présents dès le début, **ou apparaître au cours de la vidéo** (émetteur,
  tuyau d'arrivée, pluie de billes…) — si c'est propre et cohérent visuellement ;
- circuit en boucle fermée, ou parcours linéaire avec un début et une fin ;
- les déplacements « mis en scène » (objet replacé, téléporté) sont acceptables s'ils sont
  invisibles ou assumés, mais tout ce qui se voit doit sembler physique.
- Nombre d'objets : voir les limites techniques (section 8).

## 6. Déroulement (le « scénario » de la vidéo)
Le scénario est **libre** et doit varier d'une vidéo à l'autre, mais suit toujours l'arc de la
règle n°1 (§0) :
1. **Accroche** (0-3 s) : l'objectif est posé et ça bouge déjà.
2. **Progression** : on se rapproche visiblement du but, avec des rebondissements réguliers
   (nouvelle phase, obstacle, accélération, retournement de situation).
3. **Climax** : le moment où tout se joue (dernier objet, dernière case, ligne d'arrivée).
4. **Récompense** : le but est atteint, image finale satisfaisante tenue 2 s, puis `ctx.finish()`.
Jamais d'arrêt brutal, d'écran vide, ni de « machine qui tourne encore » sans conclusion.

## 7. Son
- Le programme ne produit **aucun son** (pas d'appel à `ctx.sound`). Une **musique de fond
  relaxante** est ajoutée automatiquement après le rendu (tirée au hasard dans
  `assets/music/satisfying4k/`).
- Les événements visuels n'ont donc pas besoin d'être synchronisés avec un son.

## 8. Technologies et contraintes techniques — OBLIGATOIRE
- **C++17 + raylib 5.5 + Box2D 2.4.2 (API C++ : b2World, CreateFixture, b2RevoluteJoint…)**,
  en respectant le contrat du runtime (`anim_runtime.h`) :
  - une scène avec `init / update(dt) / draw / shutdown` et `create_scene()` ;
  - pas de temps fixe (`dt` fourni, jamais `GetFrameTime()`), `world->Step(dt, 8, 3)` ;
  - aléatoire uniquement via `ctx.randf()` / `ctx.randi()` (même seed = même vidéo) ;
  - coordonnées logiques (la taille de rendu peut être réduite pour l'aperçu).
- **Les exemples de `examples/` sont des programmes autonomes** (avec `main()`, `InitWindow`,
  export ffmpeg, `SetTargetFPS`, `DrawFPS`). Il faut en reprendre le **niveau de soin, les techniques
  de dessin et les réglages physiques**, PAS les thèmes ni la structure : pas de `main()`, pas de fenêtre, pas
  d'ffmpeg, pas de `DrawFPS` — le runtime s'en charge.
- **Limites actuelles du serveur** (rendu logiciel sur CPU, 3 cœurs) :
  - **2 000 corps Box2D dynamiques au maximum en même temps** (billes + débris) ;
  - pas de shaders, pas de flou plein écran, pas de dessin pixel par pixel ;
  - éviter des dizaines de milliers d'appels de dessin par image (les billes = `DrawCircleV`).
- **Netteté en 4K** : épaisseurs et tailles pensées pour 3840 px de large. Billes d'au moins
  ~20 px de diamètre à l'écran en vidéo longue, ~40 px en Short ; contours de tubes ≥ 6 px.
- Les fluides (peinture, eau) n'existent pas dans Box2D : on les **imite** avec beaucoup de petites
  billes de la même couleur, serrées.

## 9. Critères de réussite
- l'objectif se comprend sans texte en moins de 3 secondes, et on veut voir la fin ;
- la composition est soignée (écran bien occupé ou minimalisme assumé), rien d'important hors cadre ;
- aucun objet coincé, qui tremble, qui traverse un mur ou qui sort du circuit ;
- rien ne se bloque par erreur ; l'animation avance jusqu'à sa fin naturelle ;
- les couleurs restent lisibles et belles du début à la fin ;
- la fin est clairement satisfaisante (ordre, remplissage, strates, symétrie) ou la boucle continue ;
- chaque vidéo est différente des précédentes par son **IDÉE** (phénomène, objets, histoire),
  sa **palette** et sa **durée**, pas seulement par ses couleurs.

## 10. À éviter absolument
- simple changement de couleurs ou de disposition d'une animation déjà faite ;
- **boucle sans enjeu** : machine ou circuit qui tourne sans objectif ni conclusion (le
  spectateur a tout vu au bout de 30 s et quitte la vidéo) ;
- **copier une autre chaîne** : ne jamais reproduire un nom, un filigrane (ex. « SIMULATIONGRID »,
  « Lafikobra » visibles sur certaines captures), ni une machine identique à une vidéo existante.
  Les captures servent d'inspiration de style uniquement ;
- écran vide ou presque pendant plus de 2 secondes ;
- défauts physiques : objets qui traversent les murs, qui restent bloqués, qui tremblent ou qui
  disparaissent sans raison visible ;
- objets trop petits pour être vus sur téléphone (Shorts) ;
- clignotements rapides / flashs, couleurs qui changent brutalement ;
- effets de traînée, flou, glow : il faut des objets secs et nets ;
- texte inutile, compteurs de debug, logos, marques, personnages existants.

## 11. Créativité et événements de l'actualité
- **Ne pas se restreindre** : les exemples et captures montrent des possibilités, pas des limites.
  Tout concept de simulation 2D satisfaisante et relaxante est bienvenu, du moment qu'il est
  ultra propre (§5) et réalisable (§8).
- **Événements** : quand un événement très populaire approche (la liste et les dates sont fournies
  par l'application avec la date du jour), une partie des créations peut s'en inspirer, en restant
  dans l'esprit de la chaîne. Exemples :
  - Noël : sapin qui se remplit de billes, boules de Noël, flocons qui tombent dans des formes ;
  - Nouvel An / fin d'année : chiffres de l'année qui se remplissent, feux d'artifice de billes ;
  - Halloween : citrouille qui se remplit de billes orange, palette orange / violet / noir ;
  - Coupe du monde / grand événement sportif : ballons, couleurs des drapeaux, tournoi de billes ;
  - Saint-Valentin, Pâques, été, rentrée…
  Toujours sans logo officiel, sans marque, sans personnage protégé (pas de logo FIFA, etc.).
- Ne pas faire QUE des animations à thème : la majorité reste des simulations « intemporelles ».

## 12. Titres, descriptions et tags
- Générés par l'IA pour **chaque** création, en fonction de ce qui a réellement été produit.
- **Langue : anglais** (public international des vidéos satisfying).
- **Titre** : court et accrocheur (≤ 70 caractères), met en avant l'enjeu ou le défi de la vidéo
  (question, défi, chiffre, affirmation… forme libre), peut contenir
  « 4K », « Satisfying », « Relaxing », 0 ou 1 emoji en rapport avec la vidéo. **Chaque titre a
  sa propre structure** : jamais la même formule d'une vidéo à l'autre. Exemples de styles variés :
  « 10,000 Dominoes Fall in Perfect Silence », « Watch This Jelly Tower Slowly Collapse | 4K »,
  « Sand Draws a Mandala 🌀 », « Can 500 Pendulums Find Harmony? ».
  Les Shorts ont un titre plus court et peuvent finir par #shorts.
- **Description** : 2 à 4 phrases qui décrivent la machine et invitent à se détendre, puis 3 à 5
  hashtags (#satisfying #asmr #physics #4k #relaxing…).
- **Tags** : 15 à 25 mots-clés YouTube pertinents (satisfying simulation, marble run, 4k
  animation, physics simulation, relaxing video, oddly satisfying…), plus ceux du thème si
  événement (christmas, halloween…).
- Pas de titre « piège à clic » trompeur : le titre doit correspondre à la vidéo.

## 13. Exemples fournis
Tous les codes sont des créations du propriétaire de la chaîne (C++ / raylib / Box2D 2.4, export 4K
3840×2160). Ils montrent **le niveau de qualité et des techniques utiles** (filtres de collision, corps
cinématiques, netteté 4K…). Ils tournent presque tous autour des billes et des usines : **ce n'est
PAS la ligne de la chaîne**, juste ce qui a été fait au début. Chaque nouvelle création doit
proposer son propre concept. Seul point à ne pas reprendre : les compteurs de debug affichés à
l'écran (« BALLS x / y », `DrawFPS`).

**Codes (`examples/`)**
- `main1.cpp` — **tri par couleur** : 380 billes de 7 couleurs sortent d'un tambour, descendent
  une rampe et ne traversent que le filtre de leur couleur (catégories / masques de collision
  Box2D). Palette « Clean Light » (fond crème, murs noirs, 7 couleurs vives). À retenir : la
  technique des filtres de collision pour trier proprement.
- `main2.cpp` — **presse hydraulique** : un piston descend, écrase physiquement les billes contre
  une grille et les réduit en débris. Fond clair. À retenir : un broyage crédible (bille remplacée
  par des débris au même endroit), corps cinématique pour le piston.
- `main3.cpp` — **cœur mécanique symétrique** en dark mode : roues géantes, rampes convergentes,
  composition parfaitement symétrique. À retenir : la symétrie et le contraste fond sombre /
  billes colorées.
- `main4.cpp` — **complexe industriel « Grand Cascade »** : réservoir → valves à engrenages →
  pyramide → moulins broyeurs → Plinko → bassin → 2 ascenseurs à godets → retour. 1200 billes
  posées dès l'image 0, boucle fermée à 100 % visible. À retenir : un exemple de grande usine en
  boucle fermée qui peut tourner pendant toute une vidéo longue.
- `main5.cpp` — **« Great Grain Machine »** : trémie → tamis d'engrenages → sablier → piston à
  vilebrequin → tube en S → grand tambour à pales → ascenseur à courroie → goulotte → retour.
  La teinte de chaque grain avance à chaque passage dans le goulot (couleurs qui évoluent en
  continu). À retenir : l'évolution lente des couleurs, le piston à vilebrequin.
- `main6.cpp` — **« Rainbow Circuit »** : grande cuve ronde → 2 roues en étoile (sas doseurs) →
  tube sinueux → anneau à bras rotatifs → cascade → ascenseur à godets → retour. 1800 particules
  hexagonales ; une « porte de peinture » colore chaque particule : la cuve se remplit de strates
  arc-en-ciel. À retenir : les strates colorées, très satisfaisantes.
- `main7.cpp` — **méga-usine en « S »** : roue → broyeur → convoyeur à rouleaux → Plinko géant,
  utilisant tout l'espace 4K (64 m × 36 m). À retenir : remplir tout l'écran avec un parcours long.

**Captures (`images/`)** — inspiration de style seulement, ne pas copier (voir §10)
- `usine_tricolore_tambour_ascenseur_tubes.png` — palette limitée rouge / blanc / bleu sur fond
  gris anthracite, contours blancs épais ; tambour à bras rotatif, ascenseur, roues en étoile, tube
  en serpentin, cuve où se forment des strates. Montre qu'**une palette de 3 couleurs** suffit.
- `circuit_rectangle_arc_en_ciel.png` — circuit rectangulaire fait de bacs/godets qui font le tour
  de l'écran, billes en dégradé arc-en-ciel continu. Montre l'effet « hue cycle ».
- `machine_grains_roue_pales_piston.png` — fond noir, machine grise, grains minuscules multicolores :
  ascenseur à courroie, grande roue à 3 pales, tamis, piston à vilebrequin rouge. Densité de grains
  et sobriété de la machine à viser (inspiration de `main5`).
- `circuit_spirales_entonnoirs.png` — spirales géantes que les particules remplissent en anneaux
  colorés, entonnoirs triangulaires, tube en double hélice, convoyeurs à rouleaux. Montre des
  formes géométriques fortes (spirale, triangle) remplies de couleurs.
- `broyeur_tetris_piston_tambour.png` — fond clair, pièces type Tetris faites de petits carrés qui
  se brisent dans un broyeur, piston, tube en S, tambour à bras. Référence pour les **Shorts
  « shredder »** (grosse pièce soudée → débris).
- `tri_couleurs_ascenseur_cuve.png` — grande cuve qui se remplit de **bandes verticales de couleurs
  nettes** (vert / orange / bleu / rose) grâce à un ascenseur incliné. L'ordre parfait à viser.
- `cuve_arc_en_ciel_etoiles_anneau.png` — cuve ronde en roue chromatique complète, 2 roues en
  étoile, tube sinueux, anneau, grand triangle de chute. Inspiration de `main6`.
- `labyrinthe_peinture.png` — peinture de 3 couleurs qui coule dans un labyrinthe. Concept
  intéressant, mais vrai fluide impossible : à imiter avec de nombreuses petites billes (voir §8).
