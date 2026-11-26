// ============================================================================
//  liquid_scene.cpp — Style "liquid" : Falling Notes minimaliste et lumineux (Shorts et 16:9),
//  modelé sur les références envoyées par l'utilisateur :
//   - fond NOIR pur ;
//   - notes en barres arrondies remplies d'une matière LIQUIDE lumineuse (veines qui ondulent,
//     SHAPE_FS style 13), une couleur par main, halo, nom de la note en bas de la note ;
//   - piano COMPLET 88 touches en vue fixe, lettres sous toutes les touches blanches, touches jouées
//     entièrement colorées, ligne de frappe lumineuse avec halo au-dessus du clavier ;
//   - volutes de fumée / flamme qui montent des touches jouées ;
//   - titre du morceau au centre (serif) et niveau "Beginner / Intermediate / Advanced Piano Tutorial"
//     sous le clavier.
// ============================================================================
#include "piano_common.h"

struct LNote { Rectangle r; Color c; int idx; };
struct Wisp { float x, y, vx, vy, life, max_life, size, phase; Color col; };

static const char* WHITE_LETTERS = "CDEFGAB";

class LiquidScene : public Scene {
    Settings cfg;
    float W = 1080, H = 1920, sc = 1, zoom = 1;
    bool portrait = true;
    float duration = 30, lookahead = 2.3f, fade_out = 0.35f;
    float hitY = 1380, pps = 600, kbLeft = 40, kbWidth = 1000;
    Color col_hand[2] = {{255, 196, 70, 255}, {60, 220, 230, 255}};
    Color line_col = {255, 196, 70, 255};
    const char** names = NOTE_NAMES;
    std::string title, level;

    Keyboard kb;
    NoteTrack track;
    std::vector<Wisp> wisps;
    size_t next_hit = 0;

    Gfx g;
    Font font{}, serif{};
    bool has_font = false, has_serif = false;

public:
    void init(AnimContext& ctx) override {
        std::string dir = piano_dir();
        W = (float)ctx.width;
        H = (float)ctx.height;
        cfg.load(dir + "/scene.txt");
        portrait = cfg.str("layout", "portrait") != "landscape";
        sc = portrait ? W / 1080.0f : H / 1080.0f;
        zoom = cfg.num("render_scale", 1.0f);
        duration = cfg.num("duration", duration);
        lookahead = cfg.num("lookahead", lookahead);
        fade_out = cfg.num("fade_out", fade_out);
        if (cfg.has("color_right")) col_hand[0] = parse_color(cfg.str("color_right"));
        if (cfg.has("color_left")) col_hand[1] = parse_color(cfg.str("color_left"));
        line_col = cfg.has("line_color") ? parse_color(cfg.str("line_color")) : col_hand[0];
        if (cfg.integer("key_fifths", 0) < 0) names = NOTE_NAMES_FLAT;
        title = cfg.str("title");
        level = cfg.str("level");

        // piano complet 88 touches (A0 -> C8), vue fixe
        kb.lo = 21;
        kb.hi = 108;
        track.load(dir + "/notes.txt", kb.lo, kb.hi);
        kbLeft = W * (portrait ? 0.06f : 0.04f);
        kbWidth = W - 2 * kbLeft;
        hitY = portrait ? H * 0.715f : H * 0.80f;
        float keyH = portrait ? 118 * sc : 120 * sc;
        kb.layout(kbWidth, hitY, keyH, sc);
        kb.felt = 3 * sc;
        for (int p = kb.lo; p <= kb.hi; ++p) kb.x[p] += kbLeft;
        pps = hitY / lookahead;

        g.init(W, H);
        g.spreads = {1.0f, 2.2f, 4.5f};
        font = load_font_opt(dir + "/font.ttf", 48, has_font);
        serif = load_font_opt(dir + "/font_serif.ttf", 72, has_serif);
        wisps.reserve(4000);
    }

    // ================================================================ volutes de fumée / flamme
    void spawn(AnimContext& ctx, int pitch, Color c, int n, float strength) {
        if (wisps.size() > 3500) n = std::min(n, 1);
        for (int i = 0; i < n; ++i) {
            Wisp w;
            w.x = kb.center(pitch) + ctx.randf(-0.35f, 0.35f) * kb.w[pitch];
            w.y = hitY - ctx.randf(0, 4) * sc;
            w.vx = ctx.randf(-30, 30) * sc;
            w.vy = -ctx.randf(110, 260) * sc * strength;
            w.max_life = w.life = ctx.randf(0.45f, 1.15f);
            w.size = ctx.randf(2.5f, 5.0f) * sc;
            w.phase = ctx.randf(0, 6.28f);
            w.col = c;
            wisps.push_back(w);
        }
    }

    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        const auto& notes = track.notes;
        while (next_hit < notes.size() && notes[next_hit].t0 < t1) {
            const NoteEv& n = notes[next_hit++];
            if (n.t0 < t0 - 0.05f) continue;
            spawn(ctx, n.pitch, col_hand[n.hand], 6 + (int)(n.vel / 127.0f * 8), 1.2f);
        }
        // fumée continue tant que la note est tenue
        for (size_t i = track.first_visible(t1); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t1) break;
            if (t1 < n.t1 && ctx.randf(0, 1) < 14 * dt * 2.0f) spawn(ctx, n.pitch, col_hand[n.hand], 1, 0.8f);
        }
        float drag = std::exp(-dt * 1.2f);
        for (auto& w : wisps) {
            w.vx = w.vx * drag + std::sin(t1 * 6.0f + w.phase) * 260 * sc * dt;     // ondulation (volute)
            w.vy *= drag;
            w.x += w.vx * dt;
            w.y += w.vy * dt;
            w.size += 6 * sc * dt;
            w.life -= dt;
        }
        wisps.erase(std::remove_if(wisps.begin(), wisps.end(), [](const Wisp& w) { return w.life <= 0; }), wisps.end());
    }

    // ================================================================ notes
    void collect(float t, std::vector<LNote>& wv, std::vector<LNote>& bv) const {
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t + lookahead + 0.1f) break;
            bool blk = is_black(n.pitch);
            float w = kb.w[n.pitch] * (blk ? 0.88f : 0.86f);
            float x = kb.center(n.pitch) - w / 2;
            float yb = hitY - (n.t0 - t) * pps;
            float yt = hitY - (n.t1 - t) * pps;
            if (yb - yt < w * 1.7f) yt = yb - w * 1.7f;            // notes courtes : petites pilules
            if (yt >= hitY - 1 || yb < -60) continue;
            yb = std::min(yb, hitY + 1);
            (blk ? bv : wv).push_back({{x, yt, w, yb - yt}, blk ? scalec(col_hand[n.hand], 0.9f) : col_hand[n.hand], (int)i});
        }
    }

    void draw_note_labels(const std::vector<LNote>& v) const {
        if (!has_font) return;
        for (const auto& d : v) {
            const char* name = names[track.notes[d.idx].pitch % 12];
            float size = std::min(d.r.width * 0.82f, 24 * sc);
            if (size < 7 * sc) continue;
            Vector2 m = MeasureTextEx(font, name, size, 0);
            float y = std::min(d.r.y + d.r.height, hitY) - m.y - 2 * sc;
            if (y < d.r.y) continue;
            Vector2 p{d.r.x + d.r.width / 2 - m.x / 2, y};
            DrawTextEx(font, name, Vector2{p.x + 1, p.y + 1}, size, 0, withA(BLACK, 0.55f));
            DrawTextEx(font, name, p, size, 0, WHITE);
        }
    }

    // ================================================================ clavier 88 touches
    void draw_keyboard(const int* hand, const float* since) {
        float top = hitY, felt = kb.felt, keyH = kb.keyH;
        g.rect(Rectangle{kbLeft - 2, top, kbWidth + 4, kb.bottom() - top + 2}, Color{40, 40, 44, 255});
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (is_black(p)) continue;
            bool on = hand[p] >= 0;
            g.begin_shape(on ? 10 : 11, 2.5f * sc);
            g.rect(Rectangle{kb.x[p] + 0.75f, top + felt, kb.whiteW - 1.5f, keyH}, on ? col_hand[hand[p]] : Color{214, 214, 218, 255});
            g.end_shape();
        }
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (!is_black(p)) continue;
            bool on = hand[p] >= 0;
            g.begin_shape(on ? 10 : 12, 2 * sc);
            g.rect(Rectangle{kb.x[p], top + felt, kb.blackW, kb.blackH}, on ? scalec(col_hand[hand[p]], 0.92f) : Color{10, 10, 12, 255});
            g.end_shape();
        }
        // lettres sous toutes les touches blanches
        if (has_font) {
            float size = std::min(kb.whiteW * 0.72f, 20 * sc);
            for (int p = kb.lo; p <= kb.hi; ++p) {
                if (is_black(p)) continue;
                static const int WI[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
                char s[2] = {WHITE_LETTERS[WI[p % 12]], 0};
                Vector2 m = MeasureTextEx(font, s, size, 0);
                bool on = hand[p] >= 0;
                DrawTextEx(font, s, Vector2{kb.center(p) - m.x / 2, kb.bottom() - m.y - 3 * sc}, size, 0,
                           on ? Color{255, 255, 255, 255} : Color{60, 60, 66, 255});
            }
        }
        BeginBlendMode(BLEND_ADDITIVE);
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (hand[p] < 0) continue;
            float a = 0.35f + 0.45f * std::exp(-since[p] / 0.2f);
            g.vgrad_down(Rectangle{kb.x[p], top + felt, kb.w[p], kb.key_len(p) * 0.75f}, withA(mixc(col_hand[hand[p]], WHITE, 0.35f), a * 0.6f));
        }
        EndBlendMode();
    }

    // ligne de frappe : trait lumineux de la couleur du thème + halo au-dessus du clavier
    void draw_hit_line(const int* hand, const float* since, float strength) const {
        g.rect(Rectangle{kbLeft - 2, hitY - 2 * sc, kbWidth + 4, 3 * sc}, withA(mixc(line_col, WHITE, 0.45f), 0.95f * strength));
        g.tex(g.vgrad, Rectangle{kbLeft, hitY - 46 * sc, kbWidth, 46 * sc}, withA(line_col, 0.30f * strength));
        for (int p = kb.lo; p <= kb.hi; ++p) {
            if (hand[p] < 0) continue;
            float burst = std::exp(-since[p] / 0.18f);
            Color c = col_hand[hand[p]];
            g.glow_at(kb.center(p), hitY, kb.whiteW * (3.0f + 2.5f * burst), 26 * sc * (1 + burst), withA(mixc(c, WHITE, 0.3f), (0.6f + 0.4f * burst) * strength));
        }
    }

    void draw_wisps(float strength) const {
        for (const auto& w : wisps) {
            float k = clamp01(w.life / w.max_life);
            float a = std::pow(k, 1.3f) * 0.55f * strength;
            Color c = mixc(w.col, WHITE, 0.35f * k);
            // volute fine étirée dans le sens du mouvement (fumée / flamme)
            float ang = std::atan2(w.vy, w.vx) * 57.2958f + 90.0f;
            float len = w.size * 5.5f, wid = w.size * 0.9f;
            DrawTexturePro(g.glow, Rectangle{0, 0, (float)g.glow.width, (float)g.glow.height},
                           Rectangle{w.x, w.y, wid, len}, Vector2{wid / 2, len / 2}, ang, withA(c, a));
            DrawTexturePro(g.glow, Rectangle{0, 0, (float)g.glow.width, (float)g.glow.height},
                           Rectangle{w.x, w.y, wid * 0.4f, len * 0.6f}, Vector2{wid * 0.2f, len * 0.3f}, ang, withA(WHITE, a * 0.45f * k));
        }
    }

    void set_liquid(float t) { g.set_liquid(t, t * pps * zoom, zoom); }

    // ================================================================ halo (bloom)
    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<LNote> wv, bv;
        collect(t, wv, bv);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        g.begin_glow();
        for (auto* v : {&wv, &bv})
            for (const auto& d : *v) DrawRectangleRec(d.r, withA(d.c, 0.75f));
        draw_hit_line(hand, since, 1.3f);
        draw_wisps(1.0f);
        for (int p = kb.lo; p <= kb.hi; ++p)
            if (hand[p] >= 0) DrawRectangleRec(Rectangle{kb.x[p], hitY, kb.w[p], kb.key_len(p) * 0.6f}, withA(col_hand[hand[p]], 0.6f));
        g.end_glow();
    }

    void text_center(const Font& f, const std::string& s, float x, float y, float size, Color c) const {
        Vector2 m = MeasureTextEx(f, s.c_str(), size, 1);
        DrawTextEx(f, s.c_str(), Vector2{x - m.x / 2 + 2 * sc, y + 2 * sc}, size, 1, withA(BLACK, 0.7f * c.a / 255.0f));
        DrawTextEx(f, s.c_str(), Vector2{x - m.x / 2, y}, size, 1, c);
    }

    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<LNote> wv, bv;
        collect(t, wv, bv);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);

        ClearBackground(BLACK);
        set_liquid(t);
        g.begin_shape(13, 999.0f);
        for (const auto& d : wv) g.rect(d.r, d.c);
        for (const auto& d : bv) g.rect(d.r, d.c);
        g.end_shape();
        draw_note_labels(wv);
        draw_note_labels(bv);

        g.composite_glow(1.35f);
        draw_keyboard(hand, since);
        BeginBlendMode(BLEND_ADDITIVE);
        draw_hit_line(hand, since, 1.0f);
        draw_wisps(1.0f);
        EndBlendMode();

        // titre au centre + niveau sous le clavier (serif)
        if (has_serif) {
            if (!title.empty())
                text_center(serif, title, W / 2, portrait ? H * 0.548f : H * 0.40f, (portrait ? 40 : 46) * sc, withA(WHITE, 0.95f));
            if (!level.empty())
                text_center(serif, level, W / 2, kb.bottom() + 22 * sc, (portrait ? 34 : 30) * sc, withA(WHITE, 0.92f));
        }

        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out) DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    void shutdown(AnimContext&) override {
        g.unload();
        if (has_font) UnloadFont(font);
        if (has_serif) UnloadFont(serif);
    }
};

Scene* make_liquid_scene() { return new LiquidScene(); }
