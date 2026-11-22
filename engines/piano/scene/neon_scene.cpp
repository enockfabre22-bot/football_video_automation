// ============================================================================
//  neon_scene.cpp — Style "neon" : Falling Notes néon façon grandes chaînes YouTube/TikTok.
//  Inspiré des références fournies par l'utilisateur (sans les mains) :
//   - fond noir étoilé, fines lignes d'octave ;
//   - notes PLEINES saturées (une couleur par main), liseré clair, halo néon intense,
//     nom de la note (centré sur les notes courtes, en bas sur les longues) ;
//   - clavier 2D plat et net (touches noires posées à plat), vue fixe ;
//   - impacts : ligne de frappe éclatante, éclat au contact, GERBES DE POUSSIÈRE lumineuse
//     qui montent des touches (et continuent tant que la note est tenue) ;
// ============================================================================
#include "piano_common.h"

struct NeonNote { Rectangle r; Color c; int idx; bool active; };
struct Mote { float x, y, vx, vy, life, max_life, size; Color col; };
struct NStar { float x, y, r, a, phase; };

class NeonScene : public Scene {
    Settings cfg;
    float W = 1080, H = 1920, sc = 1;
    bool portrait = true;
    float duration = 30, lookahead = 2.2f, fade_out = 0.35f, title_hold = 3.5f;
    float hitY = 1120, pps = 500, kbBottom = 1400;
    float kbWidth = 1080;              // vue fixe : tout le clavier tient à l'écran
    Color col_hand[2] = {{255, 60, 80, 255}, {100, 245, 90, 255}};
    const char** names = NOTE_NAMES;   // dièses ou bémols selon la tonalité détectée

    Keyboard kb;
    NoteTrack track;
    std::vector<Mote> motes;
    std::vector<NStar> stars;
    size_t next_hit = 0;

    Gfx g;
    Texture2D title{};
    bool has_title = false, has_font = false;
    Font font{};

public:
    // ================================================================ initialisation
    void init(AnimContext& ctx) override {
        std::string dir = piano_dir();
        W = (float)ctx.width;
        H = (float)ctx.height;
        cfg.load(dir + "/scene.txt");
        portrait = cfg.str("layout", "portrait") != "landscape";
        sc = portrait ? W / 1080.0f : H / 1080.0f;
        duration = cfg.num("duration", duration);
        lookahead = cfg.num("lookahead", lookahead);
        fade_out = cfg.num("fade_out", fade_out);
        title_hold = cfg.num("title_hold", title_hold);
        kb.lo = cfg.integer("key_lo", kb.lo);
        kb.hi = cfg.integer("key_hi", kb.hi);
        if (cfg.has("color_right")) col_hand[0] = parse_color(cfg.str("color_right"));
        if (cfg.has("color_left")) col_hand[1] = parse_color(cfg.str("color_left"));
        track.load(dir + "/notes.txt", kb.lo, kb.hi);
        if (cfg.integer("key_fifths", 0) < 0) names = NOTE_NAMES_FLAT;

        hitY = portrait ? H * 0.585f : H * 0.68f;
        float keyH = portrait ? 250 * sc : 210 * sc;
        int whites = 0;
        for (int p = kb.lo; p <= kb.hi; ++p) if (!is_black(p)) whites++;
        (void)whites;
        kbWidth = W;                                       // vue fixe : tout le clavier tient à l'écran (plus lisible)
        kb.layout(kbWidth, hitY, keyH, sc);
        kb.felt = 4 * sc;
        kbBottom = hitY + kb.felt + keyH;
        pps = hitY / lookahead;

        g.init(W, H);
        g.spreads = {1.0f, 2.2f, 4.5f};
        title = load_tex_opt(dir + "/title.png", has_title);
        font = load_font_opt(dir + "/font.ttf", 64, has_font);
        for (int i = 0; i < 260; ++i)
            stars.push_back({ctx.randf(0, W), ctx.randf(0, H), ctx.randf(0.7f, 2.2f) * sc, ctx.randf(0.25f, 0.9f), ctx.randf(0, 6.28f)});
        motes.reserve(6000);
    }

    // ================================================================ état : poussière lumineuse
    void spawn(AnimContext& ctx, float x, float spread, Color c, int n, float vmin, float vmax, float life_max) {
        if (motes.size() > 5500) n = std::min(n, 2);
        for (int i = 0; i < n; ++i) {
            Mote m;
            m.x = x + ctx.randf(-0.5f, 0.5f) * spread;
            m.y = hitY - ctx.randf(0, 6) * sc;
            m.vx = ctx.randf(-150, 150) * sc;
            m.vy = -ctx.randf(vmin, vmax) * sc;
            m.max_life = m.life = ctx.randf(0.35f, life_max);
            m.size = ctx.randf(1.1f, 2.8f) * sc;
            m.col = ctx.randf(0, 1) < 0.4f ? Color{255, 255, 255, 255} : mixc(c, WHITE, 0.3f);
            motes.push_back(m);
        }
    }

    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        const auto& notes = track.notes;
        while (next_hit < notes.size() && notes[next_hit].t0 < t1) {
            const NoteEv& n = notes[next_hit++];
            if (n.t0 < t0 - 0.05f) continue;
            float v = n.vel / 127.0f;
            spawn(ctx, kb.center(n.pitch), kb.w[n.pitch] * 0.9f, col_hand[n.hand], 30 + (int)(v * 30), 200, 760, 1.35f);
        }
        // gerbe continue tant que la note est tenue
        for (size_t i = track.first_visible(t1); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t1) break;
            if (n.t0 <= t1 && t1 < n.t1 && ctx.randf(0, 1) < 22 * dt * 2.5f)
                spawn(ctx, kb.center(n.pitch), kb.w[n.pitch] * 0.8f, col_hand[n.hand], 3, 60, 320, 1.2f);
        }
        float drag = std::exp(-dt * 1.6f);
        for (auto& m : motes) {
            m.vx *= drag;
            m.vy = m.vy * drag + 260 * sc * dt;
            m.x += m.vx * dt;
            m.y += m.vy * dt;
            m.life -= dt;
        }
        motes.erase(std::remove_if(motes.begin(), motes.end(), [&](const Mote& m) { return m.life <= 0 || m.y > hitY + 4; }), motes.end());
    }

    // ================================================================ calculs sans état
    void collect(float t, std::vector<NeonNote>& wv, std::vector<NeonNote>& bv) const {
        float gap = std::max(2.5f, kb.whiteW * 0.09f);
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t + lookahead + 0.1f) break;
            float yb = hitY - (n.t0 - t) * pps;
            float yt = hitY - (n.t1 - t) * pps;
            float minh = std::min(kb.whiteW * 0.95f, 44 * sc);           // les notes courtes restent des pavés lisibles
            if (yb - yt < minh) yt = yb - minh;
            if (yt >= hitY - 1 || yb < -60) continue;
            yb = std::min(yb, hitY + 2);
            bool blk = is_black(n.pitch);
            float x = kb.x[n.pitch] + (blk ? 1.5f : gap), w = kb.w[n.pitch] - (blk ? 3.0f : 2 * gap);
            bool act = n.t0 <= t && t < n.t1;
            Color c = col_hand[n.hand];
            if (blk) c = scalec(c, 0.86f);
            (blk ? bv : wv).push_back({{x, yt, w, yb - yt}, c, (int)i, act});
        }
    }

    void key_states(float t, int* hand, float* since) const {
        float vel[128];
        track.key_states(t, hand, since, vel);
    }

    // ================================================================ dessin
    void draw_stars(float t, float y0, float y1, float k) const {
        for (const auto& s : stars) {
            if (s.y < y0 || s.y > y1) continue;
            float a = s.a * (0.75f + 0.25f * std::sin(t * 1.3f + s.phase)) * k;
            g.rect(Rectangle{s.x - s.r * 0.5f, s.y - s.r * 0.5f, s.r, s.r}, withA(WHITE, a));
            if (s.r > 1.8f * sc) g.glow_at(s.x, s.y, s.r * 5, s.r * 5, withA(WHITE, 0.25f * a));
        }
    }

    void draw_labels(const std::vector<NeonNote>& v) const {
        if (!has_font) return;
        for (const auto& d : v) {
            const NoteEv& n = track.notes[d.idx];
            const char* name = names[n.pitch % 12];
            float size = std::min(d.r.width * 0.66f, 34 * sc);
            if (size < 10 * sc) continue;
            Vector2 m = MeasureTextEx(font, name, size, 0);
            float lum = (0.3f * d.c.r + 0.59f * d.c.g + 0.11f * d.c.b) / 255.0f;
            Color tc = lum > 0.62f ? Color{20, 40, 20, 255} : Color{255, 255, 255, 255};
            float yb = std::min(d.r.y + d.r.height, hitY);
            float y = d.r.height < size * 2.4f ? d.r.y + (yb - d.r.y) / 2 - m.y / 2 : yb - m.y - 6 * sc;
            if (y < d.r.y + 1) continue;
            DrawTextEx(font, name, Vector2{d.r.x + d.r.width / 2 - m.x / 2, y}, size, 0, tc);
        }
    }

    void draw_notes(const std::vector<NeonNote>& wv, const std::vector<NeonNote>& bv) {
        float radius = std::min(8.0f * sc, kb.whiteW * 0.22f);
        g.begin_shape(9, radius);
        for (const auto& d : wv) g.rect(d.r, d.c);
        for (const auto& d : bv) g.rect(d.r, d.c);
        g.end_shape();
        draw_labels(wv);
        draw_labels(bv);
    }

    // clavier 2D plat, net : séparations fines, touches noires posées sur le haut du clavier
    void draw_keyboard(float t, const int* hand, const float* since) {
        (void)t;
        float top = hitY, keyH = kb.keyH, felt = kb.felt;
        g.rect(Rectangle{0, top, kbWidth, kbBottom - top}, Color{16, 16, 20, 255});      // fond = séparations
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (is_black(p)) continue;
            bool on = hand[p] >= 0;
            g.begin_shape(on ? 10 : 11, 4 * sc);
            g.rect(Rectangle{kb.x[p] + 1.0f, top + felt, kb.whiteW - 2.0f, keyH}, on ? col_hand[hand[p]] : Color{246, 246, 249, 255});
            g.end_shape();
        }
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (!is_black(p)) continue;
            bool on = hand[p] >= 0;
            g.begin_shape(on ? 10 : 12, 3 * sc);
            g.rect(Rectangle{kb.x[p], top + felt, kb.blackW, kb.blackH}, on ? scalec(col_hand[hand[p]], 0.92f) : Color{20, 20, 24, 255});
            g.end_shape();
        }
        g.rect(Rectangle{0, top, kbWidth, felt}, Color{24, 24, 30, 255});                  // bande fine en haut
        // noms des C
        if (has_font) {
            float size = std::min(20 * sc, kb.whiteW * 0.42f);
            for (int p = kb.lo; p <= kb.hi; ++p) {
                if (p % 12 != 0) continue;
                char buf[8];
                snprintf(buf, sizeof(buf), "C%d", p / 12 - 1);
                Vector2 m = MeasureTextEx(font, buf, size, 0);
                DrawTextEx(font, buf, Vector2{kb.center(p) - m.x / 2, top + felt + keyH - m.y - 10 * sc}, size, 0,
                           hand[p] >= 0 ? Color{255, 255, 255, 230} : Color{110, 110, 120, 255});
            }
        }
        // lumière dans les touches jouées + ligne de frappe éclatante
        BeginBlendMode(BLEND_ADDITIVE);
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (hand[p] < 0) continue;
            float a = 0.45f + 0.4f * std::exp(-since[p] / 0.2f);
            g.vgrad_down(Rectangle{kb.x[p], top, kb.w[p], kb.key_len(p) * 0.8f}, withA(mixc(col_hand[hand[p]], WHITE, 0.35f), a * 0.6f));
            g.glow_at(kb.center(p), top + keyH * 0.15f, kb.whiteW * 2.2f, keyH * 0.45f, withA(col_hand[hand[p]], 0.18f));
        }
        g.rect(Rectangle{0, top - 1.5f * sc, kbWidth, 3 * sc}, withA(WHITE, 0.55f));
        EndBlendMode();
    }

    void draw_hit_flares(float t, const int* hand, const float* since, float strength) const {
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (hand[p] < 0) continue;
            Color c = col_hand[hand[p]];
            float burst = std::exp(-since[p] / 0.18f);
            float a = (0.55f + 0.45f * burst) * strength;
            g.glow_at(kb.center(p), hitY, kb.whiteW * (3.2f + 3.0f * burst), 34 * sc * (1 + burst), withA(mixc(c, WHITE, 0.3f), a));
            g.glow_at(kb.center(p), hitY, kb.whiteW * 1.3f, kb.whiteW * 1.3f, withA(WHITE, 0.75f * a));
        }
        (void)t;
    }

    void draw_motes(float strength, float size_k) const {
        for (const auto& m : motes) {
            float k = clamp01(m.life / m.max_life);
            float a = std::min(1.0f, k * 1.6f) * strength;
            float r = m.size * size_k;
            g.rect(Rectangle{m.x - r * 0.5f, m.y - r * 0.5f, r, r}, withA(m.col, a));
            g.glow_at(m.x, m.y, r * 4, r * 4, withA(m.col, 0.35f * a));
        }
    }

    // sol brillant : reflet des touches et des notes (profondeur)
    void draw_reflection(float t, const std::vector<NeonNote>& wv, const std::vector<NeonNote>& bv, const int* hand) {
        float y0 = kbBottom;
        (void)t;
        BeginBlendMode(BLEND_ADDITIVE);
        (void)wv; (void)bv;                 // pas de reflet des notes : image 2D nette
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (hand[p] < 0) continue;
            g.vgrad_down(Rectangle{kb.x[p], y0, kb.w[p], 110 * sc}, withA(col_hand[hand[p]], 0.28f));
        }
        EndBlendMode();
    }

    // ================================================================ halo (bloom)
    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<NeonNote> wv, bv;
        collect(t, wv, bv);
        int hand[128]; float since[128];
        key_states(t, hand, since);
        g.begin_glow();
        for (auto* v : {&wv, &bv})
            for (const auto& d : *v) DrawRectangleRec(d.r, withA(d.c, 0.95f));
        draw_hit_flares(t, hand, since, 1.4f);
        draw_motes(1.0f, 1.6f);
        for (int p = kb.lo; p <= kb.hi; ++p)
            if (hand[p] >= 0)
                DrawRectangleRec(Rectangle{kb.x[p], hitY, kb.w[p], kb.key_len(p)}, withA(col_hand[hand[p]], 0.7f));
        g.end_glow();
    }

    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<NeonNote> wv, bv;
        collect(t, wv, bv);
        int hand[128]; float since[128];
        key_states(t, hand, since);

        ClearBackground(Color{4, 4, 7, 255});
        draw_stars(t, 0, hitY, 1.0f);
        g.rect(Rectangle{0, kbBottom, W, H - kbBottom}, Color{4, 4, 6, 255});
        draw_stars(t, kbBottom, H, 0.45f);
        for (int p = kb.lo; p <= kb.hi; ++p)           // lignes d'octave
            if (p % 12 == 0) g.rect(Rectangle{kb.x[p] - 0.75f, 0, 1.5f * sc, hitY}, withA(WHITE, 0.08f));
        draw_notes(wv, bv);
        g.composite_glow(1.55f);
        draw_keyboard(t, hand, since);
        draw_reflection(t, wv, bv, hand);
        BeginBlendMode(BLEND_ADDITIVE);
        draw_hit_flares(t, hand, since, 1.0f);
        draw_motes(1.0f, 1.0f);
        EndBlendMode();

        if (has_title && title_hold > 0) {
            float a = clamp01(t / 0.3f) * clamp01((title_hold + 0.8f - t) / 0.8f);
            if (a > 0) {
                float tw = (float)title.width, th = (float)title.height;
                Vector2 pos = portrait ? Vector2{W / 2 - tw / 2, H * 0.08f} : Vector2{36 * sc, 28 * sc};
                g.tex(title, Rectangle{pos.x, pos.y, tw, th}, withA(WHITE, a));
            }
        }
        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out) DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    void shutdown(AnimContext&) override {
        g.unload();
        if (has_title) UnloadTexture(title);
        if (has_font) UnloadFont(font);
    }
};

Scene* make_neon_scene() { return new NeonScene(); }
