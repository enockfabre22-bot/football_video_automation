// ============================================================================
//  premium_scene.cpp — Style "premium" ("Aurora Keys") : Falling Notes sobre et haut de gamme.
//  Signature : la MUSIQUE colore l'image — une aurore en fond dont la teinte suit l'harmonie
//  (cercle des quintes : tonalités voisines = couleurs voisines), lente et discrète.
//  Notes en verre (corps sombre teinté + contour lumineux) avec leur nom ; clavier éclairé de
//  l'intérieur, noms des touches, lumière qui déborde sur les touches voisines ; impact sobre
//  (anneau fin + éclat + quelques micro-étincelles). Rien d'autre : pas de HUD, pas de photo.
// ============================================================================
#include "piano_common.h"

struct GlassNote { Rectangle r; Color c; bool active; int pitch; };

class PremiumScene : public Scene {
    Settings cfg;
    float W = 1080, H = 1920, sc = 1;
    float duration = 30, lookahead = 2.4f, fade_out = 0.35f, title_hold = 0;
    bool portrait = true;
    Color col_hand[2] = {{110, 210, 255, 255}, {255, 150, 120, 255}};

    Keyboard kb;
    float hitY = 1340, pps = 500;

    NoteTrack track;
    size_t next_hit = 0;
    SparkSystem sparks;
    DustField dust;
    float energy[2] = {0, 0};

    Gfx g;
    Texture2D bg{}, title{}, ring{};
    bool has_bg = false, has_title = false, has_font = false;
    Font font{};

public:
    void init(AnimContext& ctx) override {
        std::string dir = piano_dir();
        W = (float)ctx.width;
        H = (float)ctx.height;
        sc = W / 1080.0f;
        cfg.load(dir + "/scene.txt");
        duration = cfg.num("duration", duration);
        lookahead = cfg.num("lookahead", lookahead);
        fade_out = cfg.num("fade_out", fade_out);
        kb.lo = cfg.integer("key_lo", kb.lo);
        kb.hi = cfg.integer("key_hi", kb.hi);
        if (cfg.has("color_right")) col_hand[0] = parse_color(cfg.str("color_right"));
        if (cfg.has("color_left")) col_hand[1] = parse_color(cfg.str("color_left"));
        track.load(dir + "/notes.txt", kb.lo, kb.hi);

        portrait = cfg.str("layout", "portrait") != "landscape";
        title_hold = cfg.num("title_hold", 0);
        hitY = portrait ? H * 0.70f : H * 0.78f;
        float keyH = portrait ? 260 * sc : H - hitY - 30;
        kb.layout(W, hitY, keyH, sc);
        pps = hitY / lookahead;
        sparks.gravity = 1100 * sc;

        g.init(W, H);
        ring = Gfx::make_ring_texture(128);
        bg = load_tex_opt(dir + "/background.png", has_bg);
        title = load_tex_opt(dir + "/title.png", has_title);
        font = load_font_opt(dir + "/font.ttf", 64, has_font);
        dust.init(ctx, W, hitY, 28);
        ctx.metric("notes", (double)track.notes.size());
    }

    // ---------------------------------------------------------------- harmonie -> couleur
    // teinte moyenne des notes autour de t sur le cercle des quintes ; force = netteté de l'accord
    void harmony(float t, float& hue, float& strength) const {
        float vx = 0, vy = 0, ws = 0;
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t - 1.5f); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t + 0.6f) break;
            float d = (n.t0 - t) / 1.1f;
            float w = std::exp(-d * d) * (0.4f + n.vel / 127.0f);
            float ang = ((n.pitch * 7) % 12) / 12.0f * 6.2832f;
            vx += w * std::cos(ang);
            vy += w * std::sin(ang);
            ws += w;
        }
        if (ws < 1e-3f) { hue = 210; strength = 0; return; }
        hue = std::fmod(std::atan2(vy, vx) * 57.2958f + 360.0f + 200.0f, 360.0f);
        strength = std::sqrt(vx * vx + vy * vy) / ws;
    }

    // ---------------------------------------------------------------- état
    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        float k = std::exp(-dt / 0.6f);
        energy[0] *= k;
        energy[1] *= k;
        const auto& notes = track.notes;
        while (next_hit < notes.size() && notes[next_hit].t0 < t1) {
            const NoteEv& n = notes[next_hit++];
            if (n.t0 < t0 - 0.05f) continue;
            float v = n.vel / 127.0f;
            energy[n.hand] = std::min(1.5f, energy[n.hand] + 0.08f + 0.12f * v);
            // quelques micro-étincelles seulement : sobre
            sparks.burst(ctx, kb.center(n.pitch), hitY - 2, 0.5f * kb.w[n.pitch], mixc(col_hand[n.hand], WHITE, 0.3f),
                         2 + (int)(v * 4), 520 * sc, t1 - std::max(n.t0, t0), -2.3f, -0.85f);
        }
        sparks.update(dt, H);
    }

    // ---------------------------------------------------------------- notes
    void collect(float t, std::vector<GlassNote>& wv, std::vector<GlassNote>& bv) const {
        float gap = std::max(2.0f, kb.whiteW * 0.09f);
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t + lookahead + 0.1f) break;
            float yb = hitY - (n.t0 - t) * pps;
            float yt = hitY - (n.t1 - t) * pps;
            if (yb - yt < 18 * sc) yt = yb - 18 * sc;
            if (yt >= hitY - 1 || yb < -30) continue;
            yb = std::min(yb, hitY + kb.felt * 0.8f);
            bool blk = is_black(n.pitch);
            float x = kb.x[n.pitch] + (blk ? 1.5f : gap);
            float w = kb.w[n.pitch] - (blk ? 3.0f : 2 * gap);
            bool act = n.t0 <= t && t < n.t1;
            float v = n.vel / 127.0f;
            Color c = scalec(col_hand[n.hand], (0.9f + 0.15f * v) * (act ? 1.9f : 1.0f) * (blk ? 0.85f : 1.0f));
            (blk ? bv : wv).push_back({{x, yt, w, yb - yt}, c, act, n.pitch});
        }
    }

    void draw_note_labels(const std::vector<GlassNote>& v) const {
        if (!has_font) return;
        for (const auto& d : v) {
            float size = std::min(26 * sc, d.r.width * 0.62f);
            if (size < 11 * sc || d.r.height < size + 14 * sc) continue;
            const char* name = NOTE_NAMES[d.pitch % 12];
            Vector2 m = MeasureTextEx(font, name, size, 0);
            float y = std::min(d.r.y + d.r.height, hitY) - m.y - 7 * sc;
            if (y < d.r.y + 3) continue;
            DrawTextEx(font, name, Vector2{d.r.x + d.r.width / 2 - m.x / 2, y}, size, 0,
                       withA(WHITE, d.active ? 1.0f : 0.82f));
        }
    }

    // ---------------------------------------------------------------- impacts sobres
    void draw_impacts(float t, float strength) const {
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float age = t - n.t0;
            if (age < 0 || age > 0.5f) continue;
            float u = age / 0.5f;
            float v = 0.5f + 0.5f * n.vel / 127.0f;
            float cx = kb.center(n.pitch);
            Color c = mixc(col_hand[n.hand], WHITE, 0.35f);
            float R = (kb.whiteW * 0.8f + 120 * sc * (1 - (1 - u) * (1 - u))) * v;
            g.tex(ring, Rectangle{cx - R, hitY - R * 0.18f, 2 * R, R * 0.36f}, withA(c, 0.7f * (1 - u) * strength));
            if (age < 0.22f) {
                float a = std::pow(1 - age / 0.22f, 2.0f) * v;
                g.glow_at(cx, hitY, kb.whiteW * 3.2f, kb.whiteW * 1.2f, withA(c, a * strength));
                g.glow_at(cx, hitY, kb.whiteW * 1.2f, kb.whiteW * 1.2f, withA(WHITE, 0.8f * a * strength));
            }
        }
    }

    // rideau de lumière discret au-dessus des touches jouées
    void draw_curtains(float t, const int* hand, const float* since, float strength) const {
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (hand[p] < 0) continue;
            float a = (0.16f + 0.22f * std::exp(-since[p] / 0.25f)) * strength;
            float h = 230 * sc;
            g.tex(g.vgrad, Rectangle{kb.x[p] + kb.w[p] * 0.12f, hitY - h, kb.w[p] * 0.76f, h}, withA(col_hand[hand[p]], a));
        }
        (void)t;
    }

    // ---------------------------------------------------------------- clavier éclairé
    void draw_keyboard(const int* hand, const float* since) {
        float top = kb.top, felt = kb.felt, keyH = kb.keyH;
        g.rect(Rectangle{0, top, W, kb.bottom() - top + 4}, Color{10, 10, 14, 255});
        g.begin_shape(1, 7.0f * sc);
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (is_black(p)) continue;
            Color base = Color{246, 246, 250, 255};
            float press = 0;
            if (hand[p] >= 0) { press = 1; base = mixc(base, col_hand[hand[p]], 0.55f); }
            g.rect(Rectangle{kb.x[p] + 1.0f, top + press * 3 * sc, kb.whiteW - 2.0f, felt + keyH - press * 3 * sc}, base);
        }
        g.end_shape();
        // lumière intérieure + débordement doux sur les touches voisines
        BeginBlendMode(BLEND_ADDITIVE);
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (hand[p] < 0) continue;
            float a = 0.30f + 0.35f * std::exp(-since[p] / 0.2f);
            Color c = col_hand[hand[p]];
            g.vgrad_down(Rectangle{kb.x[p], top + felt, kb.w[p], kb.key_len(p) * 0.85f}, withA(mixc(c, WHITE, 0.4f), a * 0.7f));
            g.glow_at(kb.center(p), top + felt + keyH * 0.18f, kb.whiteW * 3.4f, keyH * 0.55f, withA(c, a * 0.35f));
        }
        EndBlendMode();
        // noms des touches blanches (C avec l'octave), discrets ; en blanc quand la touche est jouée
        if (has_font) {
            float size = std::min(24 * sc, kb.whiteW * 0.5f);
            for (int p = kb.lo; p <= kb.hi; ++p) {
                if (is_black(p)) continue;
                char buf[8];
                if (p % 12 == 0) snprintf(buf, sizeof(buf), "C%d", p / 12 - 1);
                else snprintf(buf, sizeof(buf), "%s", NOTE_NAMES[p % 12]);
                float s = p % 12 == 0 ? size : size * 0.9f;
                Vector2 m = MeasureTextEx(font, buf, s, 0);
                bool on = hand[p] >= 0;
                // touche jouée : nom dans une teinte foncée de la couleur de la main (lisible sur la touche teintée)
                Color c = on ? scalec(col_hand[hand[p]], 0.38f) : Color{120, 122, 135, 255};
                DrawTextEx(font, buf, Vector2{kb.center(p) - m.x / 2, kb.bottom() - m.y - 16 * sc + (on ? 3 * sc : 0)}, s, 0, c);
            }
        }
        for (int p = kb.lo; p <= kb.hi; ++p)            // ombre portée des touches noires
            if (is_black(p))
                g.tex(g.glow, Rectangle{kb.x[p] - kb.blackW * 0.15f, top + felt + kb.blackH - kb.blackW * 0.5f,
                                        kb.blackW * 1.4f, kb.blackW * 1.2f}, withA(BLACK, 0.45f));
        g.begin_shape(2, 4.0f * sc);
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (!is_black(p)) continue;
            Color base = Color{20, 20, 26, 255};
            float press = 0;
            if (hand[p] >= 0) { press = 1; base = scalec(col_hand[hand[p]], 0.7f); }
            g.rect(Rectangle{kb.x[p], top + press * 3 * sc, kb.blackW, felt + kb.blackH - press * 2 * sc}, base);
        }
        g.end_shape();
        g.rect(Rectangle{0, top, W, felt}, Color{30, 8, 16, 255});
        g.rect(Rectangle{0, top + felt, W, 4 * sc}, withA(BLACK, 0.35f));
        BeginBlendMode(BLEND_ADDITIVE);
        g.rect(Rectangle{0, top - 1, W, 2}, withA(WHITE, 0.22f));
        for (int p = kb.lo; p <= kb.hi; ++p)
            if (hand[p] >= 0)
                g.glow_at(kb.center(p), top, kb.w[p] * 2.4f, 12 * sc, withA(col_hand[hand[p]], 0.9f));
        EndBlendMode();
    }

    // ---------------------------------------------------------------- halo
    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<GlassNote> wv, bv;
        collect(t, wv, bv);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        g.begin_glow();
        for (auto* v : {&wv, &bv})
            for (const auto& d : *v) {
                // contour seulement (le verre ne brille pas en son centre, sauf note jouée)
                float e = 3 * sc;
                Color c = withA(d.c, d.active ? 0.9f : 0.45f);
                if (d.active) DrawRectangleRec(d.r, c);
                else {
                    DrawRectangleRec(Rectangle{d.r.x, d.r.y, e, d.r.height}, c);
                    DrawRectangleRec(Rectangle{d.r.x + d.r.width - e, d.r.y, e, d.r.height}, c);
                    DrawRectangleRec(Rectangle{d.r.x, d.r.y + d.r.height - e * 2, d.r.width, e * 2}, c);
                }
            }
        draw_curtains(t, hand, since, 1.2f);
        draw_impacts(t, 1.2f);
        sparks.draw(g, 0.9f);
        kb.draw_glow(g, hand, col_hand);
        g.end_glow();
    }

    // ---------------------------------------------------------------- image
    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<GlassNote> wv, bv;
        collect(t, wv, bv);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        float e = std::min(1.0f, (energy[0] + energy[1]) * 0.55f);

        if (has_bg) g.tex(bg, Rectangle{0, 0, W, H}, WHITE);
        else ClearBackground(Color{6, 7, 14, 255});

        // aurore pilotée par l'harmonie : 3 nappes lentes
        float hue, strength;
        harmony(t, hue, strength);
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 3; ++i) {
            float x = W * (0.5f + 0.32f * std::sin(t * 0.13f + i * 2.1f));
            float y = hitY * (0.38f + 0.16f * std::sin(t * 0.09f + i * 1.7f));
            Color c = ColorFromHSV(std::fmod(hue + (i - 1) * 26.0f + 360.0f, 360.0f), 0.62f, 1.0f);
            float a = (0.07f + 0.10f * e) * (0.55f + 0.45f * strength);
            g.glow_at(x, y, W * 1.25f, H * 0.42f, withA(c, a));
        }
        dust.draw(g, t, ColorFromHSV(hue, 0.4f, 1.0f));
        // brume lumineuse juste au-dessus du clavier
        g.glow_at(W / 2, hitY, W * 1.5f, 220 * sc, withA(mixc(col_hand[0], col_hand[1], 0.5f), 0.06f + 0.10f * e));
        EndBlendMode();
        for (int p = kb.lo; p <= kb.hi; ++p)     // repères d'octave très discrets
            if (p % 12 == 0) g.rect(Rectangle{kb.x[p] - 0.5f, 0, 1.0f, hitY}, withA(WHITE, 0.045f));

        BeginBlendMode(BLEND_ADDITIVE);
        draw_curtains(t, hand, since, 1.0f);
        EndBlendMode();

        float radius = std::min(9.0f * sc, kb.whiteW * 0.26f);
        g.begin_shape(5, radius);
        for (const auto& d : wv) g.rect(d.r, d.c);
        for (const auto& d : bv) g.rect(d.r, d.c);
        g.end_shape();
        draw_note_labels(wv);
        draw_note_labels(bv);

        g.composite_glow(1.05f);

        draw_keyboard(hand, since);
        BeginBlendMode(BLEND_ADDITIVE);
        draw_impacts(t, 1.0f);
        sparks.draw(g, 0.9f);
        EndBlendMode();
        kb.draw_floor(g, H, hand, col_hand, sc);

        // portrait : titre permanent centré ; paysage : en haut à gauche, disparaît après title_hold s
        float title_a = title_hold > 0 ? clamp01(title_hold + 1.0f - t) : 1.0f;
        if (has_title && title_a > 0) {
            float tw = (float)title.width, th = (float)title.height;
            Vector2 pos = portrait ? Vector2{W / 2 - tw / 2, 165 * sc} : Vector2{24, 18};
            g.tex(title, Rectangle{pos.x, pos.y, tw, th}, withA(WHITE, title_a));
        }

        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out)
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    void shutdown(AnimContext&) override {
        g.unload();
        UnloadTexture(ring);
        if (has_bg) UnloadTexture(bg);
        if (has_title) UnloadTexture(title);
        if (has_font) UnloadFont(font);
    }
};

Scene* make_premium_scene() { return new PremiumScene(); }
