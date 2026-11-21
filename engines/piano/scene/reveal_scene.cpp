// ============================================================================
//  reveal_scene.cpp — Style "reveal" : chaque note construit la réponse.
//  Chaque note jouée lance des particules de lumière depuis sa touche ; elles volent jusqu'à
//  leur place dans le TITRE de la chanson. La forme reste floue (bruit qui diminue avec le temps)
//  et devient nette au moment de la révélation (reveal_time), avec flash + titre net + artiste.
//  Particules entièrement calculées à partir du temps (draw() ne modifie aucun état).
//  Données : particles.txt ("t_émission main pitch x y bruit_x bruit_y"), reveal_title.png,
//  artist.png, guess.png, notes.txt, background.png (écrits par prepare.py --style reveal).
// ============================================================================
#include "piano_common.h"

struct Particle { float te, tx, ty, nx, ny, phase; int hand, pitch; };

class RevealScene : public Scene {
    Settings cfg;
    float W = 1080, H = 1920, sc = 1;
    float duration = 30, fade_out = 0.35f, reveal_t = 27, flight = 0.9f;
    float region_top = 400, artist_y = 1000, fuzz = 230;
    Color col_hand[2] = {{0, 225, 255, 255}, {255, 70, 170, 255}};

    Keyboard kb;
    NoteTrack track;
    std::vector<Particle> parts;
    SparkSystem sparks;
    DustField dust;
    size_t next_note = 0;
    float energy[2] = {0, 0};
    bool burst_done = false;

    Gfx g;
    Texture2D bg{}, header{}, crisp{}, artist{};
    bool has_bg = false, has_header = false, has_crisp = false, has_artist = false;

public:
    void init(AnimContext& ctx) override {
        std::string dir = piano_dir();
        W = (float)ctx.width;
        H = (float)ctx.height;
        sc = W / 1080.0f;
        cfg.load(dir + "/scene.txt");
        duration = cfg.num("duration", duration);
        fade_out = cfg.num("fade_out", fade_out);
        reveal_t = cfg.num("reveal_time", duration - 3.2f);
        flight = cfg.num("flight", flight);
        region_top = cfg.num("region_top", region_top);
        artist_y = cfg.num("artist_y", artist_y);
        kb.lo = cfg.integer("key_lo", kb.lo);
        kb.hi = cfg.integer("key_hi", kb.hi);
        if (cfg.has("color_right")) col_hand[0] = parse_color(cfg.str("color_right"));
        if (cfg.has("color_left")) col_hand[1] = parse_color(cfg.str("color_left"));
        track.load(dir + "/notes.txt", kb.lo, kb.hi);

        float top = H * 0.655f;
        kb.layout(W, top, 250 * sc, sc);
        fuzz = 260 * sc;
        sparks.gravity = 1500 * sc;

        FILE* f = fopen((dir + "/particles.txt").c_str(), "r");
        if (f) {
            Particle p;
            while (fscanf(f, "%f %d %d %f %f %f %f", &p.te, &p.hand, &p.pitch, &p.tx, &p.ty, &p.nx, &p.ny) == 7) {
                p.hand = p.hand ? 1 : 0;
                p.pitch = std::max(kb.lo, std::min(kb.hi, p.pitch));
                p.phase = ctx.randf(0, 6.2832f);
                parts.push_back(p);
            }
            fclose(f);
        }
        std::sort(parts.begin(), parts.end(), [](const Particle& a, const Particle& b) { return a.te < b.te; });

        g.init(W, H);
        bg = load_tex_opt(dir + "/background.png", has_bg);
        header = load_tex_opt(dir + "/guess.png", has_header);
        crisp = load_tex_opt(dir + "/reveal_title.png", has_crisp);
        artist = load_tex_opt(dir + "/artist.png", has_artist);
        dust.init(ctx, W, kb.top, 36);
        ctx.metric("particles", (double)parts.size());
    }

    // ---------------------------------------------------------------- position (sans état)
    // amplitude du flou : grande au début, nulle à la révélation ; décroissance presque linéaire
    // pour que le titre ne devienne lisible que dans les 2 dernières secondes avant la réponse
    float fuzz_at(float t) const {
        float p = clamp01(t / std::max(0.1f, reveal_t));
        return fuzz * std::pow(1 - p, 1.05f);
    }

    // renvoie false si la particule n'est pas encore lancée ; u = avancement du vol (>= 1 : posée)
    bool part_pos(const Particle& p, float t, float& x, float& y, float& u) const {
        if (t < p.te) return false;
        float amp = fuzz_at(t);
        float rx = p.tx + p.nx * amp, ry = p.ty + p.ny * amp * 0.8f;
        u = (t - p.te) / flight;
        if (u >= 1) { x = rx; y = ry; return true; }
        float e = 1 - (1 - u) * (1 - u) * (1 - u);              // départ vif, arrivée douce
        float sx = kb.center(p.pitch), sy = kb.top - 4;
        float cx = sx + (rx - sx) * 0.15f, cy = ry - 90 * sc;   // monte puis s'incurve vers sa place
        float a = (1 - e) * (1 - e), b = 2 * (1 - e) * e, c = e * e;
        x = a * sx + b * cx + c * rx;
        y = a * sy + b * cy + c * ry;
        return true;
    }

    // ---------------------------------------------------------------- état
    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        float k = std::exp(-dt / 0.5f);
        energy[0] *= k;
        energy[1] *= k;
        const auto& notes = track.notes;
        while (next_note < notes.size() && notes[next_note].t0 < t1) {
            const NoteEv& n = notes[next_note++];
            if (n.t0 < t0 - 0.05f) continue;
            float v = n.vel / 127.0f;
            energy[n.hand] = std::min(1.6f, energy[n.hand] + 0.08f + 0.12f * v);
            sparks.burst(ctx, kb.center(n.pitch), kb.top - 2, kb.w[n.pitch], col_hand[n.hand],
                         3 + (int)(v * 5), 700 * sc, t1 - std::max(n.t0, t0));
        }
        if (!burst_done && reveal_t < t1) {
            burst_done = true;
            for (const auto& p : parts)
                if (ctx.randf(0, 1) < 0.08f)
                    sparks.burst(ctx, p.tx, p.ty, 0, col_hand[p.hand], 1, 900 * sc, t1 - reveal_t, -3.14f, 0.0f);
        }
        sparks.update(dt, H);
    }

    // ---------------------------------------------------------------- dessin
    void draw_particles(float t, float strength, bool glow_pass) const {
        float pulse = 1 + 0.35f * std::min(1.0f, energy[0] + energy[1]);
        float rev = clamp01((t - reveal_t) / 0.35f);
        for (const auto& p : parts) {
            if (p.te > t) break;
            float x, y, u;
            if (!part_pos(p, t, x, y, u)) continue;
            Color c = col_hand[p.hand];
            if (u < 1) {
                // en vol : tête brillante + courte traînée
                for (int k = 4; k >= 0; --k) {
                    float px, py, pu;
                    if (!part_pos(p, t - k * 0.016f, px, py, pu)) continue;
                    float f = 1 - k / 5.0f;
                    float r = (glow_pass ? 16 : 9) * sc * f;
                    g.glow_at(px, py, r * 2, r * 2, withA(mixc(c, WHITE, 0.4f * f), 0.8f * f * strength));
                }
                continue;
            }
            float since = t - (p.te + flight);
            float pop = since < 0.25f ? 1 + 1.5f * (1 - since / 0.25f) : 1;   // petit éclat à l'arrivée
            float tw = 0.75f + 0.25f * std::sin(t * 3.1f + p.phase);
            float a = tw * strength * (1 - 0.45f * rev);
            if (glow_pass) {
                g.glow_at(x, y, 26 * sc * pop, 26 * sc * pop, withA(c, 0.55f * a * pulse));
            } else {
                g.glow_at(x, y, 18 * sc * pop, 18 * sc * pop, withA(c, 0.45f * a * pulse));
                g.glow_at(x, y, 7 * sc * pop, 7 * sc * pop, withA(mixc(c, WHITE, 0.65f), a));
            }
        }
    }

    void draw_key_flares(float t, float strength) const {
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float age = t - n.t0;
            if (age < 0 || age > 0.3f) continue;
            float a = std::pow(1 - age / 0.3f, 2.0f) * (0.5f + 0.5f * n.vel / 127.0f);
            float fw = std::max(kb.whiteW * 4.5f, 120 * sc);
            g.glow_at(kb.center(n.pitch), kb.top, fw, fw * 0.3f, withA(mixc(col_hand[n.hand], WHITE, 0.4f), a * strength));
        }
    }

    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        g.begin_glow();
        draw_particles(t, 1.0f, true);
        draw_key_flares(t, 1.3f);
        sparks.draw(g, 1.0f);
        kb.draw_glow(g, hand, col_hand);
        if (t >= reveal_t && has_crisp) {
            float u = clamp01((t - reveal_t) / 0.35f);
            g.tex(crisp, Rectangle{0, region_top, (float)crisp.width, (float)crisp.height}, withA(WHITE, 0.8f * u));
        }
        g.end_glow();
    }

    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);

        if (has_bg) g.tex(bg, Rectangle{0, 0, W, H}, WHITE);
        else ClearBackground(Color{8, 8, 16, 255});

        BeginBlendMode(BLEND_ADDITIVE);
        float e = std::min(1.0f, (energy[0] + energy[1]) * 0.6f);
        Color amb = mixc(col_hand[1], col_hand[0], energy[0] / std::max(0.01f, energy[0] + energy[1]));
        g.glow_at(W / 2, region_top + 300 * sc, W * 1.6f, 900 * sc, withA(amb, 0.07f + 0.12f * e));
        g.glow_at(W / 2, kb.top, W * 1.8f, H * 0.35f, withA(amb, 0.08f + 0.18f * e));
        dust.draw(g, t, amb);
        EndBlendMode();

        g.composite_glow(1.25f);

        BeginBlendMode(BLEND_ADDITIVE);
        draw_particles(t, 1.0f, false);
        EndBlendMode();

        kb.draw(g, hand, since, col_hand, sc);
        BeginBlendMode(BLEND_ADDITIVE);
        draw_key_flares(t, 1.0f);
        sparks.draw(g, 1.0f);
        EndBlendMode();
        kb.draw_floor(g, H, hand, col_hand, sc);

        // question en haut, puis révélation
        float hide = clamp01((t - reveal_t) / 0.3f);
        if (has_header && hide < 1) {
            float hy = 140 * sc;
            g.tex(header, Rectangle{W / 2 - header.width / 2.0f, hy, (float)header.width, (float)header.height}, withA(WHITE, 1 - hide));
        }
        if (t >= reveal_t) {
            float u = clamp01((t - reveal_t) / 0.35f);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(WHITE, 0.28f * std::exp(-(t - reveal_t) / 0.16f)));
            EndBlendMode();
            if (has_crisp)
                g.tex(crisp, Rectangle{0, region_top, (float)crisp.width, (float)crisp.height}, withA(WHITE, u));
            float ua = clamp01((t - reveal_t - 0.35f) / 0.4f);
            if (has_artist && ua > 0)
                g.tex(artist, Rectangle{W / 2 - artist.width / 2.0f, artist_y + 20 * sc * (1 - ua),
                                        (float)artist.width, (float)artist.height}, withA(WHITE, ua));
        }

        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out)
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    void shutdown(AnimContext&) override {
        g.unload();
        if (has_bg) UnloadTexture(bg);
        if (has_header) UnloadTexture(header);
        if (has_crisp) UnloadTexture(crisp);
        if (has_artist) UnloadTexture(artist);
    }
};

Scene* make_reveal_scene() { return new RevealScene(); }
