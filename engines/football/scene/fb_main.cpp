// ============================================================================
//  fb_main.cpp — Point d'entrée du moteur foot : un seul binaire pour tous les formats,
//  create_scene() choisit la scène d'après scene.txt
//  (format=royale|rings|colorwar|marble|knockout|duel = Shorts 9:16, quiz = vidéo longue 16:9).
// ============================================================================
#include "fb_common.h"

Scene* make_quiz_scene();

Scene* create_scene() {
    Settings s;
    s.load(fb_dir() + "/scene.txt");
    std::string f = s.str("format", "royale");
    printf("[FOOT] Format : %s\n", f.c_str());
    if (f == "quiz") {                 // vidéo longue : espace de conception paysage
        DW = 1920.0f;
        DH = 1080.0f;
        return make_quiz_scene();
    }
    if (f == "rings") return make_rings_scene();
    if (f == "colorwar") return make_colorwar_scene();
    if (f == "marble") return make_marble_scene();
    if (f == "knockout") return make_knockout_scene();
    if (f == "duel") return make_duel_scene();
    return make_royale_scene();
}
