// ============================================================================
//  anim_runtime.h — Contrat entre le runtime (fiable, fourni par l'application)
//  et une scène d'animation (écrite à la main ou générée par l'IA).
//
//  La scène ne gère NI la fenêtre, NI le temps réel, NI la capture vidéo, NI le son :
//    - elle dessine en coordonnées LOGIQUES (0..ctx.width, 0..ctx.height), y vers le bas ;
//    - elle avance d'un pas de temps FIXE (dt = 1/fps) à chaque update(), jamais avec GetFrameTime() ;
//    - elle tire ses nombres aléatoires avec ctx.randf()/ctx.randi() (seed reproductible) ;
//    - elle déclenche des sons avec ctx.sound("nom", volume) ; le runtime les mixe dans audio.wav.
//  Résultat : l'aperçu (basse résolution) et le rendu final sont exactement la même simulation.
// ============================================================================
#pragma once

#include "raylib.h"

#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

struct AnimSoundEvent {
    double time;          // secondes depuis le début de la vidéo
    std::string sound;    // nom du fichier son sans extension (ex. "bounce" -> bounce.mp3)
    float volume;         // 0..1
};

// Échantillon de surveillance automatique (voir anim_box2d.h : anim_watch_world)
struct AnimWatchSample {
    int bodies = 0;      // corps dynamiques
    int offscreen = 0;   // corps hors du canevas visible
    int lost = 0;        // corps très loin du canevas (sortis du monde, tombés à l'infini)
    int invalid = 0;     // positions / vitesses NaN ou infinies
    int tracked = 0;     // corps déjà présents à l'échantillon précédent
    int moving = 0;      // parmi eux, ceux qui se sont réellement déplacés depuis
};

class AnimContext {
public:
    // --- Paramètres (lecture seule pour la scène) ---
    int width = 1080;          // largeur logique du canevas
    int height = 1920;         // hauteur logique du canevas
    int fps = 60;
    double duration = 30.0;    // secondes
    int total_frames = 0;
    uint32_t seed = 0;
    bool preview = false;      // true pendant l'aperçu basse résolution (la simulation reste identique)

    // --- État courant ---
    int frame = 0;             // index de l'image en cours
    double time = 0.0;         // frame / fps

    // --- Aléatoire reproductible ---
    float randf(float min, float max);
    int randi(int min, int max);   // bornes incluses

    // --- Son : enregistre un événement à l'instant courant ---
    void sound(const std::string& name, float volume = 1.0f);

    // --- Métriques personnalisées (écrites dans metrics.json) ---
    void metric(const std::string& key, double value);

    // --- Surveillance automatique, installée par anim_watch_world() (anim_box2d.h) ---
    std::function<AnimWatchSample()> watcher;

    // ---- Interne au runtime ----
    std::mt19937 rng;
    std::vector<AnimSoundEvent> events;
    std::map<std::string, double> metrics;
    std::map<std::string, double> last_sound_time;
    int sounds_this_frame = 0;
    int dropped_sounds = 0;
};

class Scene {
public:
    virtual ~Scene() {}
    virtual void init(AnimContext& ctx) = 0;              // création du monde (appelé une fois)
    virtual void update(AnimContext& ctx, float dt) = 0;  // avance la simulation de dt secondes
    virtual void draw(AnimContext& ctx) = 0;              // dessine l'état courant (sans BeginDrawing) ;
                                                          // ne doit JAMAIS modifier l'état (en aperçu,
                                                          // seules les images capturées sont dessinées)
    virtual void shutdown(AnimContext&) {}                // libération des ressources
    // Optionnel : passes de rendu hors écran (RenderTexture, shaders de flou...) exécutées juste
    // AVANT draw(), hors de tout BeginTextureMode. Mêmes règles que draw() : ne modifie pas l'état.
    virtual void render_offscreen(AnimContext&) {}
};

// À implémenter par la scène : retourne une nouvelle instance.
Scene* create_scene();
