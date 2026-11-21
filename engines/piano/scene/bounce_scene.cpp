// ============================================================================
//  bounce_scene.cpp — Style "bounce" : deux balles lumineuses rebondissent de touche en touche.
//  La balle de la main droite suit la mélodie (note la plus haute), celle de la main gauche
//  la basse (note la plus basse). Chaque atterrissage tombe EXACTEMENT sur l'attaque de la note :
//  trajectoires paraboliques calculées (gravité constante), pas de simulation qui dérive.
//
//  Mode "guess" (scene.txt guess=1) : titre caché, "Can you guess the song?" + barre de
//  progression, réponse révélée à reveal_time (answer.png), pour faire rester et commenter.
// ============================================================================
#include "piano_common.h"

struct BallEv { float t; int pitch; float vel; };

struct BallPos {
    float x = 0, y = 0;        // centre de la balle
    float sx = 1, sy = 1;      // écrasement à l'impact
    float alpha = 0;
    int target = -1;           // indice du prochain atterrissage
};

class BounceScene : public Scene {
    Settings cfg;
    float W = 1080, H = 1920, sc = 1;
    float duration = 30, fade_out = 0.35f;
    bool guess = false;
    float reveal_t = 1e9f;
    Color col_hand[2] = {{0, 225, 255, 255}, {255, 70, 170, 255}};

    Keyboard kb;
    NoteTrack track;
    std::vector<BallEv> balls[2];
    float radius = 20, grav = 7600, max_h = 800, min_h = 18;

    SparkSystem sparks;
    DustField dust;
    size_t next_ev[2] = {0, 0};
    float energy[2] = {0, 0};
    bool revealed_burst = false;

    Gfx g;
    Texture2D bg{}, title{}, header{}, answer{}, ring{};
    bool has_bg = false, has_title = false, has_header = false, has_answer = false;
    float header_y = 150, bar_y = 300;

public:
    void init(AnimContext& ctx) override {
        std::string dir = piano_dir();
        W = (float)ctx.width;
        H = (float)ctx.height;
        sc = W / 1080.0f;
        cfg.load(dir + "/scene.txt");
        duration = cfg.num("duration", duration);
        fade_out = cfg.num("fade_out", fade_out);
        guess = cfg.integer("guess", 0) != 0;
        reveal_t = guess ? cfg.num("reveal_time", duration - 3.2f) : 1e9f;
        kb.lo = cfg.integer("key_lo", kb.lo);
        kb.hi = cfg.integer("key_hi", kb.hi);
        if (cfg.has("color_right")) col_hand[0] = parse_color(cfg.str("color_right"));
        if (cfg.has("color_left")) col_hand[1] = parse_color(cfg.str("color_left"));
        track.load(dir + "/notes.txt", kb.lo, kb.hi);

        bool portrait = cfg.str("layout", "portrait") != "landscape";
        float top = portrait ? H * 0.655f : H * 0.74f;
        float keyH = portrait ? 250 * sc : H - top - 30;
        kb.layout(W, top, keyH, sc);
        radius = std::max(20 * sc, std::min(30 * sc, kb.whiteW * 0.6f));
        grav = 7600 * sc * (portrait ? 1.0f : 0.75f);
        min_h = 40 * sc;
        sparks.gravity = 1500 * sc;

        g.init(W, H);
        bg = load_tex_opt(dir + "/background.png", has_bg);
        title = load_tex_opt(dir + "/title.png", has_title);
        header = load_tex_opt(dir + "/guess.png", has_header);
        answer = load_tex_opt(dir + "/answer.png", has_answer);
        ring = make_ring_texture(128);
        header_y = portrait ? 140 * sc : 30;
        float head_h = guess && has_header ? header.height : (has_title ? title.height : 0);
        bar_y = header_y + head_h + 14 * sc;
        max_h = std::max(200 * sc, kb.top - (bar_y + 60 * sc) - radius * 2);

        build_ball_events();
        dust.init(ctx, W, kb.top, 40);
        ctx.metric("ball_events_right", (double)balls[0].size());
        ctx.metric("ball_events_left", (double)balls[1].size());
    }

    static Texture2D make_ring_texture(int n) {
        Color* px = (Color*)MemAlloc(n * n * sizeof(Color));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
                float r = std::sqrt(dx * dx + dy * dy);
                float a = std::exp(-std::pow((r - 0.82f) / 0.09f, 2.0f));
                px[y * n + x] = Color{255, 255, 255, (unsigned char)(255 * a)};
            }
        Image img = {px, n, n, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D t = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        return t;
    }

    // Une balle par main : mélodie (note la plus haute de chaque accord) / basse (la plus basse)
    void build_ball_events() {
        for (int h = 0; h < 2; ++h) {
            std::vector<BallEv>& out = balls[h];
            size_t i = 0;
            const auto& notes = track.notes;
            while (i < notes.size()) {
                if (notes[i].hand != h) { ++i; continue; }
                float t0 = notes[i].t0;
                BallEv ev{t0, notes[i].pitch, notes[i].vel / 127.0f};
                size_t j = i;
                while (j < notes.size() && notes[j].t0 - t0 < 0.035f) {
                    if (notes[j].hand == h) {
                        if ((h == 0 && notes[j].pitch > ev.pitch) || (h == 1 && notes[j].pitch < ev.pitch)) ev.pitch = notes[j].pitch;
                        ev.vel = std::max(ev.vel, notes[j].vel / 127.0f);
                    }
                    ++j;
                }
                if (t0 < duration - 0.05f && (out.empty() || t0 - out.back().t >= 0.075f)) out.push_back(ev);
                i = j;
            }
        }
        // main gauche quasi absente : une seule balle
        if (balls[1].size() < 4) balls[1].clear();
        if (balls[0].size() < 4 && !balls[1].empty()) std::swap(balls[0], balls[1]);
    }

    // ---------------------------------------------------------------- trajectoire (sans état)
    // Hauteur proportionnelle à la durée du saut (chaque saut reste une parabole) : avec une gravité
    // unique, les notes rapides donnaient des sauts de 50 px et laissaient l'écran vide.
    float hop_height(float T) const { return std::max(min_h, max_h * std::min(1.0f, T / 0.9f)); }

    BallPos ball_at(int h, float t) const {
        BallPos b;
        const auto& ev = balls[h];
        if (ev.empty()) return b;
        float land = kb.top;                         // le bas de la balle touche le haut des touches
        auto it = std::upper_bound(ev.begin(), ev.end(), t, [](float v, const BallEv& e) { return v < e.t; });
        int next = (int)(it - ev.begin());
        b.target = next < (int)ev.size() ? next : -1;
        float base_y;
        if (next == 0) {
            // chute d'entrée depuis le haut
            float T = std::min(ev[0].t, 0.75f);
            float start = ev[0].t - T;
            if (t < start) return b;
            float hgt = std::min(max_h, 0.5f * grav * T * T);
            float u = (t - start) / std::max(T, 1e-3f);
            b.x = kb.center(ev[0].pitch);
            base_y = land - hgt * (1 - u * u);
            b.alpha = clamp01((t - start) / 0.15f);
        } else {
            const BallEv& a = ev[next - 1];
            b.alpha = 1;
            if (next >= (int)ev.size()) {
                b.x = kb.center(a.pitch);
                base_y = land;
            } else {
                const BallEv& c = ev[next];
                float T = c.t - a.t;
                float Th = std::min(T, 0.95f);       // longue pause : la balle attend sur sa touche
                float ts = c.t - Th;
                if (t < ts) {
                    b.x = kb.center(a.pitch);
                    base_y = land;
                } else {
                    float u = (t - ts) / Th;
                    b.x = kb.center(a.pitch) + (kb.center(c.pitch) - kb.center(a.pitch)) * u;
                    base_y = land - 4 * hop_height(Th) * u * (1 - u);
                }
            }
            float since = t - a.t;                    // écrasement juste après l'impact
            float squash = 0.38f * std::exp(-since / 0.05f) * (0.6f + 0.4f * a.vel);
            b.sy = 1 - squash;
            b.sx = 1 + squash * 0.8f;
        }
        b.y = base_y - radius * b.sy;
        return b;
    }

    // ---------------------------------------------------------------- état
    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        float k = std::exp(-dt / 0.55f);
        for (int h = 0; h < 2; ++h) {
            energy[h] *= k;
            const auto& ev = balls[h];
            while (next_ev[h] < ev.size() && ev[next_ev[h]].t < t1) {
                const BallEv& e = ev[next_ev[h]++];
                if (e.t < t0 - 0.05f) continue;
                energy[h] = std::min(1.6f, energy[h] + 0.18f + 0.2f * e.vel);
                sparks.burst(ctx, kb.center(e.pitch), kb.top - 2, radius, col_hand[h],
                             10 + (int)(e.vel * 14), 1150 * sc * (0.6f + 0.6f * e.vel), t1 - std::max(e.t, t0));
            }
        }
        if (guess && !revealed_burst && reveal_t < t1) {
            revealed_burst = true;
            float cy = reveal_center_y();
            for (int h = 0; h < 2; ++h)
                sparks.burst(ctx, W / 2, cy, W * 0.5f, col_hand[h], 45, 1400 * sc, t1 - reveal_t, -3.1f, -0.05f);
        }
        sparks.update(dt, H);
    }

    float reveal_center_y() const { return H * 0.36f; }

    // ---------------------------------------------------------------- dessin des balles
    void draw_trail(int h, float t, float strength) const {
        const int N = 90;
        const float step = 1.0f / 110.0f;
        for (int i = N; i >= 1; --i) {
            float tt = t - i * step;
            if (tt < 0) continue;
            BallPos p = ball_at(h, tt);
            if (p.alpha <= 0) continue;
            float k = 1.0f - (float)i / N;
            float r = radius * (0.35f + 0.9f * k);
            g.glow_at(p.x, p.y, r * 2.6f, r * 2.6f, withA(col_hand[h], 0.28f * k * k * strength * p.alpha));
            g.glow_at(p.x, p.y, r * 0.9f, r * 0.9f, withA(mixc(col_hand[h], WHITE, 0.6f), 0.35f * k * k * k * strength * p.alpha));
        }
    }

    void draw_impacts(int h, float t, float strength) const {
        for (const auto& e : balls[h]) {
            if (e.t > t) break;
            float age = t - e.t;
            if (age > 0.55f) continue;
            float cx = kb.center(e.pitch);
            float k = 1 - age / 0.55f;
            // onde de choc aplatie sur le clavier
            float R = (30 + 300 * (1 - (1 - age / 0.55f) * (1 - age / 0.55f))) * sc * (0.7f + 0.5f * e.vel);
            g.tex(ring, Rectangle{cx - R, kb.top - R * 0.22f, 2 * R, R * 0.44f}, withA(mixc(col_hand[h], WHITE, 0.3f), 0.8f * k * k * strength));
            if (age < 0.32f) {
                float a = std::pow(1 - age / 0.32f, 2.0f) * (0.6f + 0.4f * e.vel);
                float fw = std::max(kb.whiteW * 6.0f, 170 * sc);
                g.glow_at(cx, kb.top, fw, fw * 0.32f, withA(mixc(col_hand[h], WHITE, 0.4f), a * strength));
                g.glow_at(cx, kb.top, fw * 0.4f, fw * 0.4f, withA(WHITE, a * 0.9f * strength));
            }
        }
    }

    // cible : la prochaine touche s'illumine à mesure que la balle approche
    void draw_target(int h, float t, const BallPos& p) const {
        if (p.target < 0 || p.alpha <= 0) return;
        const BallEv& e = balls[h][p.target];
        float remain = e.t - t;
        if (remain > 0.9f) return;
        float k = 1 - remain / 0.9f;
        float cx = kb.center(e.pitch);
        float R = kb.w[e.pitch] * (1.6f - 0.6f * k);
        g.tex(ring, Rectangle{cx - R, kb.top - R * 0.25f, 2 * R, R * 0.5f}, withA(col_hand[h], 0.55f * k * k));
    }

    void draw_ball(int h, const BallPos& p) {
        if (p.alpha <= 0) return;
        float rx = radius * p.sx, ry = radius * p.sy;
        BeginBlendMode(BLEND_ADDITIVE);
        g.glow_at(p.x, p.y, radius * 6, radius * 6, withA(col_hand[h], 0.40f * p.alpha));
        // lumière projetée sur le clavier quand la balle est proche
        float near = clamp01(1 - (kb.top - (p.y + ry)) / (260 * sc));
        g.glow_at(p.x, kb.top + kb.felt + 30 * sc, radius * 7, radius * 3, withA(col_hand[h], 0.45f * near * p.alpha));
        EndBlendMode();
        g.begin_shape(3, 999.0f);
        g.rect(Rectangle{p.x - rx, p.y - ry, 2 * rx, 2 * ry}, withA(mixc(col_hand[h], WHITE, 0.15f), p.alpha));
        g.end_shape();
        BeginBlendMode(BLEND_ADDITIVE);
        g.glow_at(p.x, p.y, radius * 1.6f, radius * 1.6f, withA(WHITE, 0.55f * p.alpha));
        EndBlendMode();
    }

    // ---------------------------------------------------------------- halo
    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        g.begin_glow();
        for (int h = 0; h < 2; ++h) {
            draw_trail(h, t, 1.4f);
            draw_impacts(h, t, 1.3f);
            BallPos p = ball_at(h, t);
            if (p.alpha > 0) g.glow_at(p.x, p.y, radius * 4, radius * 4, withA(col_hand[h], p.alpha));
        }
        sparks.draw(g, 1.0f);
        kb.draw_glow(g, hand, col_hand);
        g.end_glow();
    }

    // ---------------------------------------------------------------- image
    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);

        if (has_bg) g.tex(bg, Rectangle{0, 0, W, H}, WHITE);
        else ClearBackground(Color{8, 8, 16, 255});

        BeginBlendMode(BLEND_ADDITIVE);
        float e = std::min(1.0f, (energy[0] + energy[1]) * 0.6f);
        Color amb = mixc(col_hand[1], col_hand[0], energy[0] / std::max(0.01f, energy[0] + energy[1]));
        float aw = W * 1.8f, ah = H * 0.6f;
        g.tex(g.glow, Rectangle{W / 2 - aw / 2, kb.top - ah * 0.6f, aw, ah}, withA(amb, 0.10f + 0.24f * e));
        dust.draw(g, t, amb);
        for (int h = 0; h < 2; ++h) draw_trail(h, t, 1.0f);
        EndBlendMode();

        g.composite_glow(1.3f);
        kb.draw(g, hand, since, col_hand, sc);

        BallPos pos[2] = {ball_at(0, t), ball_at(1, t)};
        BeginBlendMode(BLEND_ADDITIVE);
        for (int h = 0; h < 2; ++h) { draw_target(h, t, pos[h]); draw_impacts(h, t, 1.0f); }
        EndBlendMode();
        for (int h = 1; h >= 0; --h) draw_ball(h, pos[h]);
        BeginBlendMode(BLEND_ADDITIVE);
        sparks.draw(g, 1.0f);
        EndBlendMode();
        kb.draw_floor(g, H, hand, col_hand, sc);

        draw_overlay(t);

        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out)
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    // ---------------------------------------------------------------- textes
    void draw_overlay(float t) {
        if (!guess) {
            if (has_title) g.tex(title, Rectangle{W / 2 - title.width / 2.0f, header_y, (float)title.width, (float)title.height}, WHITE);
            return;
        }
        float hide = clamp01((t - reveal_t) / 0.3f);
        if (has_header && hide < 1)
            g.tex(header, Rectangle{W / 2 - header.width / 2.0f, header_y, (float)header.width, (float)header.height}, withA(WHITE, 1 - hide));

        // barre de progression vers la réponse
        float bw = W * 0.62f, bh = 10 * sc, bx = W / 2 - bw / 2;
        float p = clamp01(t / std::max(0.1f, reveal_t));
        float a = 1 - hide;
        if (a > 0) {
            g.begin_shape(0, bh / 2);
            g.rect(Rectangle{bx, bar_y, bw, bh}, withA(Color{60, 60, 75, 255}, 0.85f * a));
            g.end_shape();
            if (p > 0.01f) {
                Color c = mixc(col_hand[0], col_hand[1], p);
                g.begin_shape(0, bh / 2);
                g.rect(Rectangle{bx, bar_y, bw * p, bh}, withA(c, a));
                g.end_shape();
                BeginBlendMode(BLEND_ADDITIVE);
                g.glow_at(bx + bw * p, bar_y + bh / 2, 60 * sc, 60 * sc, withA(c, 0.9f * a));
                EndBlendMode();
            }
        }

        // révélation : carte qui « pop » avec halo et flash
        if (t >= reveal_t && has_answer) {
            float u = clamp01((t - reveal_t) / 0.45f);
            const float c1 = 1.70158f, c3 = c1 + 1;
            float s = 1 + c3 * std::pow(u - 1, 3.0f) + c1 * std::pow(u - 1, 2.0f);   // easeOutBack
            float cy = reveal_center_y();
            BeginBlendMode(BLEND_ADDITIVE);
            float flash = std::exp(-(t - reveal_t) / 0.18f);
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(WHITE, 0.22f * flash));
            float gw = W * 1.3f * (0.6f + 0.4f * u);
            g.glow_at(W / 2, cy, gw, gw * 0.55f, withA(mixc(col_hand[0], col_hand[1], 0.5f), 0.55f * u));
            EndBlendMode();
            float aw = answer.width * s, ah = answer.height * s;
            g.tex(answer, Rectangle{W / 2 - aw / 2, cy - ah / 2, aw, ah}, withA(WHITE, clamp01(u * 2.5f)));
        }
    }

    void shutdown(AnimContext&) override {
        g.unload();
        UnloadTexture(ring);
        if (has_bg) UnloadTexture(bg);
        if (has_title) UnloadTexture(title);
        if (has_header) UnloadTexture(header);
        if (has_answer) UnloadTexture(answer);
    }
};

Scene* make_bounce_scene() { return new BounceScene(); }
