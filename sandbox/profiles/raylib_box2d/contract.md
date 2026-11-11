# Contrat technique — profil `raylib_box2d` (C++17 + raylib 5.5 + Box2D 2.4.2)

Tu écris UNIQUEMENT la scène. L'application fournit un runtime fiable qui gère : `main()`, la
fenêtre (cachée), la boucle de rendu hors temps réel, la mise à l'échelle, l'encodage vidéo, le son
et les mesures. Les en-têtes exacts du runtime (`anim_runtime.h`, `anim_box2d.h`) sont donnés plus bas.

## Fichiers
- `scene.cpp` est obligatoire. Tu peux ajouter d'autres `.cpp` / `.h` (noms simples, sans dossier).
- Compilation : `g++ -O2 -std=c++17 runtime/*.cpp src/*.cpp -lraylib -lbox2d` sous Linux.
- Includes autorisés : `"anim_runtime.h"`, `"anim_box2d.h"` (inclut `<box2d/box2d.h>`), `"raylib.h"`,
  `"raymath.h"`, et la bibliothèque standard C++17. Rien d'autre.
- INTERDIT : `main()`, `InitWindow`, `BeginDrawing`/`EndDrawing`, `SetTargetFPS`, `DrawFPS`, fichiers
  (`fopen`, `LoadTexture`, `LoadFont`, `LoadImage`…), `system`/`popen`, threads, réseau, `rand`/`srand`,
  `time()`, `GetTime`, `GetFrameTime`, `GetRandomValue`. Le programme tourne sans réseau, en lecture
  seule, avec CPU / mémoire / durée limités.

## Interface à implémenter
```cpp
#include "anim_runtime.h"
#include "anim_box2d.h"

class MaScene : public Scene {
public:
    void init(AnimContext& ctx) override;              // crée le monde, appelé une fois
    void update(AnimContext& ctx, float dt) override;  // avance la simulation d'UN pas fixe dt = 1/fps
    void draw(AnimContext& ctx) override;              // dessine l'état courant, SANS le modifier
    void shutdown(AnimContext& ctx) override;          // libère (delete world)
};
Scene* create_scene() { return new MaScene(); }
```
- Ordre par image : `draw()` puis `update()`. L'image 0 montre donc l'état créé par `init()`.
- `draw()` ne doit JAMAIS modifier l'état : pendant l'aperçu, seules quelques images sont dessinées.
  Toute logique (apparitions, couleurs qui évoluent, moteurs…) va dans `update()`.
- Temps : `ctx.frame`, `ctx.time` (s), `ctx.duration` (s), `ctx.total_frames`, `ctx.fps` (60).
  Tout ce qui dépend du temps se base sur ces valeurs (jamais sur l'horloge réelle).
- Aléatoire : uniquement `ctx.randf(min, max)` et `ctx.randi(min, max)` (même seed = même vidéo ;
  l'aperçu et le rendu final doivent être identiques).

## Coordonnées et dessin
- Canevas LOGIQUE : `ctx.width` × `ctx.height` pixels (ex. 3840×2160 ou 2160×3840), origine en
  haut à gauche, y vers le bas. Dessine toujours dans ces unités ; le runtime met à l'échelle
  (l'aperçu est rendu en plus petit). Ne code pas la taille en dur : utilise `ctx.width/height`.
- Le runtime efface en noir : dessine ton propre fond en premier (`DrawRectangle` ou dégradé).
- Primitives raylib conseillées : `DrawCircleV`, `DrawCircleSector`, `DrawRing`, `DrawRectanglePro`,
  `DrawRectangleRounded`, `DrawLineEx`, `DrawTriangle`, `DrawPoly`, `DrawPolyLinesEx`,
  `DrawTriangleFan`, `ColorFromHSV`, `Fade`. Texte : police par défaut (`DrawText`, `MeasureText`).
- Pas de shaders, pas de `RenderTexture`, pas de dessin pixel par pixel, pas de flou.
- raylib définit déjà des MACROS de couleur : `LIGHTGRAY GRAY DARKGRAY YELLOW GOLD ORANGE PINK RED
  MAROON GREEN LIME DARKGREEN SKYBLUE BLUE DARKBLUE PURPLE VIOLET DARKPURPLE BEIGE BROWN DARKBROWN
  WHITE BLACK BLANK MAGENTA RAYWHITE`, ainsi que `PI DEG2RAD RAD2DEG`. Ne déclare JAMAIS une
  constante, variable ou fonction portant un de ces noms (erreur de compilation) : préfixe tes
  couleurs, ex. `COL_GOLD`, `COL_ORANGE`.
- Convertis le monde physique avec une fonction `toScreen(b2Vec2)` (mètres → pixels logiques,
  inversion de y si ta gravité est vers le bas en y négatif).

## Box2D 2.4.2 (API C++)
- `b2World* world = new b2World(b2Vec2(0, -10));` puis `world->Step(dt, 8, 3);` dans `update()`.
- Corps : `b2BodyDef bd; bd.type = b2_dynamicBody|b2_kinematicBody|b2_staticBody; bd.position.Set(x, y);`
  `b2Body* b = world->CreateBody(&bd); b->CreateFixture(&fixtureDef);`
- Formes : `b2CircleShape` (`m_radius`, `m_p`), `b2PolygonShape` (`SetAsBox(hx, hy)`,
  `SetAsBox(hx, hy, center, angle)`, `Set(verts, n)` avec n ≤ 8, convexe), `b2EdgeShape`
  (`SetTwoSided(a, b)`), `b2ChainShape` (`CreateLoop(verts, n)`, `CreateChain(verts, n, prev, next)`).
- Pièces mobiles : corps cinématiques (`SetLinearVelocity`, `SetAngularVelocity`, ou
  `SetTransform` progressif) ou joints moteurs (`b2RevoluteJointDef` avec `enableMotor`,
  `motorSpeed`, `maxMotorTorque`; `b2PrismaticJointDef`).
- Filtres de collision : `fixtureDef.filter.categoryBits / maskBits / groupIndex`.
- Données utilisateur : `bd.userData.pointer = (uintptr_t)ptr;` (pas de `SetUserData` en 2.4).
- Contacts : `class L : public b2ContactListener { void BeginContact(b2Contact*) override; }` +
  `world->SetContactListener(&l)`. Ne JAMAIS créer/détruire de corps pendant `Step` ou dans un
  callback : mets en file d'attente et applique après `Step`.
- Unités en mètres : objets de 0,05 à 10 m, ~30 à 80 pixels logiques par mètre. Petits objets
  rapides : `bd.bullet = true`. Murs épais (polygones plutôt qu'arêtes fines) pour éviter les
  traversées. Évite de superposer des corps à la création (explosion numérique).
- Performances : pas de boucle O(n²) sur les corps dans `update()` ; le nombre de corps dynamiques
  simultanés ne doit pas dépasser la limite donnée dans la demande.

## Surveillance automatique (OBLIGATOIRE)
Dans `init()`, juste après avoir créé le monde :
```cpp
anim_watch_world(ctx, world, [this](b2Vec2 p) { return toScreen(p); });
```
Le runtime mesure alors 2 fois par seconde : nombre de corps dynamiques, corps hors écran, corps
« perdus » (très loin du canevas), positions invalides (NaN), part des corps en mouvement.
L'application rejette automatiquement une animation où des corps sont perdus ou invalides, où la
limite de corps est dépassée, ou qui se fige (plus rien ne bouge) avant la fin.
Tu peux ajouter tes propres mesures avec `ctx.metric("nom", valeur)` (dernière valeur conservée).

## Son
`ctx.sound("nom", volume)` déclenche un son du dossier de sons de l'animation (si l'animation en a
un). Si la demande indique que le son est une musique ajoutée après coup, n'appelle jamais
`ctx.sound`.
