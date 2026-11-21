// ============================================================================
//  piano_scene.cpp — Style "falling" : notes qui tombent sur un clavier (néon).
//  Données lues dans $PIANO_DIR (par défaut /work/piano), écrites par engines/piano/prepare.py :
//    scene.txt (réglages), notes.txt ("début fin pitch vélocité main" en secondes VIDÉO),
//    background.png, title.png.
//  Le son n'est pas géré ici : finalize.py ajoute audio.wav (vrai piano) à la vidéo muette.
//  Ce fichier contient aussi create_scene(), qui choisit le style (falling | bounce).
// ============================================================================
#include "piano_common.h"

struct DrawNote { Rectangle r; Color c; bool active; float vel; };

class FallingScene : public Scene {
    Settings cfg;
    bool portrait = true;
    float W = 1080, H = 1920;
    float duration = 30, lookahead = 2.6f, fade_out = 0.35f;
    float title_hold = 0;          // 0 = titre permanent ; sinon disparaît après N secondes
    Color col_hand[2] = {{0, 225, 255, 255}, {255, 70, 170, 255}};

    Keyboard kb;
    float hitY = 1370, pps = 500, beamH = 380;

    NoteTrack track;
    size_t next_hit = 0;
    SparkSystem sparks;
    DustField dust;
    float energy[2] = {0, 0};

    Gfx g;
    Texture2D bg{}, title{};
    bool has_bg = false, has_title = false;

public:
    void init(AnimContext& ctx) override {
        std::string dir = piano_dir();
        W = (float)ctx.width;
        H = (float)ctx.height;
        cfg.load(dir + "/scene.txt");
        portrait = cfg.str("layout", "portrait") != "landscape";
        duration = cfg.num("duration", duration);
        lookahead = cfg.num("lookahead", lookahead);
        fade_out = cfg.num("fade_out", fade_out);
        title_hold = cfg.num("title_hold", title_hold);
        kb.lo = cfg.integer("key_lo", kb.lo);
        kb.hi = cfg.integer("key_hi", kb.hi);
        if (cfg.has("color_right")) col_hand[0] = parse_color(cfg.str("color_right"));
        if (cfg.has("color_left")) col_hand[1] = parse_color(cfg.str("color_left"));
        track.load(dir + "/notes.txt", kb.lo, kb.hi);

        float s = W / 1080.0f;
        float keyH;
        if (portrait) { hitY = H * 0.715f; keyH = 245 * s; beamH = 520 * s; }
        else { hitY = H * 0.79f; keyH = H - hitY - 36; beamH = 260; }
        kb.layout(W, hitY, keyH, s);
        pps = hitY / lookahead;
        sparks.gravity = 1500.0f * s;

        g.init(W, H);
        bg = load_tex_opt(dir + "/background.png", has_bg);
        title = load_tex_opt(dir + "/title.png", has_title);
        dust.init(ctx, W, hitY, 46);
        ctx.metric("notes", (double)track.notes.size());
        ctx.metric("key_lo", kb.lo);
        ctx.metric("key_hi", kb.hi);
    }

    // ---------------------------------------------------------------- état
    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        float k = std::exp(-dt / 0.55f);
        energy[0] *= k;
        energy[1] *= k;
        const auto& notes = track.notes;
        float sc = W / 1080.0f;
        while (next_hit < notes.size() && notes[next_hit].t0 < t1) {
            const NoteEv& n = notes[next_hit++];
            if (n.t0 < t0 - 0.05f) continue;
            float v = n.vel / 127.0f;
            energy[n.hand] += 0.10f + 0.16f * v;
            sparks.burst(ctx, kb.center(n.pitch), hitY - 2, 0.6f * kb.w[n.pitch], col_hand[n.hand],
                         6 + (int)(v * 12), 900 * sc * (0.6f + 0.6f * v), t1 - std::max(n.t0, t0));
        }
        energy[0] = std::min(energy[0], 1.6f);
        energy[1] = std::min(energy[1], 1.6f);
        sparks.update(dt, H);
    }

    // ---------------------------------------------------------------- calculs sans état
    void collect_notes(float t, std::vector<DrawNote>& whites_v, std::vector<DrawNote>& blacks_v) const {
        float gap = std::max(1.5f, kb.whiteW * 0.07f);
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t + lookahead + 0.1f) break;
            float yb = hitY - (n.t0 - t) * pps;
            float yt = hitY - (n.t1 - t) * pps;
            if (yb - yt < 14) yt = yb - 14;
            if (yt >= hitY - 1 || yb < -20) continue;
            yb = std::min(yb, hitY + kb.felt * 0.8f);
            bool blk = is_black(n.pitch);
            float x = kb.x[n.pitch] + (blk ? 1.0f : gap);
            float w = kb.w[n.pitch] - (blk ? 2.0f : 2 * gap);
            bool act = n.t0 <= t && t < n.t1;
            float v = n.vel / 127.0f;
            float bright = (0.80f + 0.25f * v) * (blk ? 0.80f : 1.0f);
            Color c = scalec(col_hand[n.hand], act ? bright * 1.25f : bright);
            if (act) c = mixc(c, WHITE, 0.18f);
            DrawNote d{{x, yt, w, yb - yt}, c, act, v};
            (blk ? blacks_v : whites_v).push_back(d);
        }
    }

    void draw_beams(float t, float strength) const {
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float rel = t - n.t1;
            if (t < n.t0 || rel > 0.25f) continue;
            float attack = std::exp(-(t - n.t0) / 0.22f);
            float a = (0.30f + 0.45f * attack) * (rel > 0 ? 1 - rel / 0.25f : 1) * (0.6f + 0.4f * n.vel / 127.0f);
            float w = kb.w[n.pitch];
            float h = beamH * (0.75f + 0.25f * attack);
            g.tex(g.vgrad, Rectangle{kb.x[n.pitch] + w * 0.08f, hitY - h, w * 0.84f, h},
                  withA(col_hand[n.hand], a * strength));
        }
    }

    void draw_flares(float t, float strength) const {
        float sc = W / 1080.0f;
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float age = t - n.t0;
            if (age < 0 || age > 0.35f) continue;
            float a = std::pow(1 - age / 0.35f, 2.0f) * (0.5f + 0.5f * n.vel / 127.0f);
            float cx = kb.center(n.pitch);
            float fw = std::max(kb.whiteW * 5.5f, 130.0f * sc), fh = std::max(kb.whiteW * 1.6f, 44.0f * sc);
            g.glow_at(cx, hitY, fw, fh, withA(mixc(col_hand[n.hand], WHITE, 0.45f), a * strength));
            float cw = fw * 0.42f;
            g.glow_at(cx, hitY, cw, cw, withA(WHITE, a * 0.9f * strength));
        }
    }

    // ---------------------------------------------------------------- halo
    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<DrawNote> wn, bn;
        collect_notes(t, wn, bn);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        g.begin_glow();
        for (auto* v : {&wn, &bn})
            for (const auto& d : *v)
                DrawRectangleRec(d.r, withA(d.c, d.active ? 1.0f : 0.5f));
        draw_beams(t, 1.3f);
        draw_flares(t, 1.4f);
        sparks.draw(g, 1.0f);
        kb.draw_glow(g, hand, col_hand);
        g.end_glow();
    }

    // ---------------------------------------------------------------- image
    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        float sc = W / 1080.0f;
        std::vector<DrawNote> wn, bn;
        collect_notes(t, wn, bn);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);

        if (has_bg) g.tex(bg, Rectangle{0, 0, W, H}, WHITE);
        else ClearBackground(Color{8, 8, 16, 255});

        // lueur d'ambiance qui respire avec la musique + poussières
        BeginBlendMode(BLEND_ADDITIVE);
        float e = std::min(1.0f, (energy[0] + energy[1]) * 0.6f);
        Color amb = mixc(col_hand[1], col_hand[0], energy[0] / std::max(0.01f, energy[0] + energy[1]));
        float aw = W * 1.7f, ah = H * 0.55f;
        g.tex(g.glow, Rectangle{W / 2 - aw / 2, hitY - ah * 0.62f, aw, ah}, withA(amb, 0.10f + 0.22f * e));
        dust.draw(g, t, amb);
        EndBlendMode();
        for (int p = kb.lo; p <= kb.hi; ++p)       // repères d'octave
            if (p % 12 == 0) g.rect(Rectangle{kb.x[p] - 0.75f, 0, 1.5f, hitY}, withA(WHITE, 0.06f));
        BeginBlendMode(BLEND_ADDITIVE);
        draw_beams(t, 0.8f);
        EndBlendMode();

        float radius = std::min(10.0f * sc, kb.whiteW * 0.28f);
        g.begin_shape(0, radius);
        for (const auto& d : wn) g.rect(d.r, d.c);
        for (const auto& d : bn) g.rect(d.r, d.c);
        g.end_shape();

        g.composite_glow(1.3f);

        kb.draw(g, hand, since, col_hand, sc);
        BeginBlendMode(BLEND_ADDITIVE);
        draw_flares(t, 1.0f);
        sparks.draw(g, 1.0f);
        EndBlendMode();
        kb.draw_floor(g, H, hand, col_hand, sc);

        float title_a = title_hold > 0 ? clamp01(title_hold + 1.0f - t) : 1.0f;
        if (has_title && title_a > 0) {
            float tw = (float)title.width, th = (float)title.height;
            Vector2 pos = portrait ? Vector2{W / 2 - tw / 2, 150 * sc} : Vector2{24, 18};
            g.tex(title, Rectangle{pos.x, pos.y, tw, th}, withA(WHITE, title_a));
        }

        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out)
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    void shutdown(AnimContext&) override {
        g.unload();
        if (has_bg) UnloadTexture(bg);
        if (has_title) UnloadTexture(title);
    }
};

Scene* make_falling_scene() { return new FallingScene(); }

Scene* create_scene() {
    Settings s;
    s.load(piano_dir() + "/scene.txt");
    std::string style = s.str("style", "falling");
    if (style == "bounce") return make_bounce_scene();
    if (style == "reveal") return make_reveal_scene();
    if (style == "highway") return make_highway_scene();
    return make_falling_scene();
}
