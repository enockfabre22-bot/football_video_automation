// ============================================================================
//  fb_main.cpp — Point d'entrée du moteur foot : un seul binaire pour les six formats,
//  create_scene() choisit la scène d'après scene.txt (format=royale|rings|colorwar|marble|knockout|duel).
// ============================================================================
#include "fb_common.h"

Scene* create_scene() {
    Settings s;
    s.load(fb_dir() + "/scene.txt");
    std::string f = s.str("format", "royale");
    printf("[FOOT] Format : %s\n", f.c_str());
    if (f == "rings") return make_rings_scene();
    if (f == "colorwar") return make_colorwar_scene();
    if (f == "marble") return make_marble_scene();
    if (f == "knockout") return make_knockout_scene();
    if (f == "duel") return make_duel_scene();
    return make_royale_scene();
}
