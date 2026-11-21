// ============================================================================
//  highway_scene.cpp — Style "highway" : jeu de rythme au piano, en perspective.
//  - Les notes foncent vers le spectateur sur une piste 3D (accélération de perspective) et
//    frappent le clavier au moment exact de la note ; grille qui défile AU TEMPO du morceau,
//    traînées de vitesse depuis l'horizon, point de fuite qui suit la mélodie (caméra vivante).
//  - Retour de jeu : piste qui s'illumine, éclats, étincelles, compteur de COMBO, "PERFECT!",
//    paliers "50 COMBO!".
//  - Énigme : le titre est caché en cases "_ _ _" ; des lettres se dévoilent au fil du morceau,
//    tout se révèle à reveal_time avec "FULL COMBO!" + l'artiste.
//  Données (prepare.py --style highway) : notes.txt, beats.txt, font.ttf, scene.txt (title, artist).
// ============================================================================
#include "piano_common.h"
#include "rlgl.h"

struct Beat { float t; bool bar; };
struct Popup { float t, x, y; int kind; Color c; std::string text; };
struct Slot { int cp; float x, y; bool letter; float t_reveal; };
struct Streak { float ang, r0, speed, width; };

static float ease_out_back(float u) {
    u = clamp01(u);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(u - 1, 3.0f) + c1 * std::pow(u - 1, 2.0f);
}

class HighwayScene : public Scene {
    Settings cfg;
    float W = 1080, H = 1920, sc = 1;
    float duration = 30, fade_out = 0.35f, reveal_t = 27, lookahead = 2.2f;
    float horizonY = 500, hitY = 1340, kpersp = 8, s_min = 0.05f;
    Color col_hand[2] = {{0, 225, 255, 255}, {255, 70, 170, 255}};

    Keyboard kb;
    NoteTrack track;
    std::vector<Beat> beats;
    std::vector<Popup> popups;
    std::vector<Slot> slots;
    std::vector<Streak> streaks;
    float letter_size = 80, title_bottom = 300, combo_y = 800;
    std::string artist;

    SparkSystem sparks;
    size_t next_note = 0;
    float energy[2] = {0, 0};
    bool final_burst = false;

    Gfx g;
    Texture2D bg{};
    bool has_bg = false;
    Font font{};
    bool has_font = false;

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
        lookahead = cfg.num("lookahead", lookahead);
        kb.lo = cfg.integer("key_lo", kb.lo);
        kb.hi = cfg.integer("key_hi", kb.hi);
        if (cfg.has("color_right")) col_hand[0] = parse_color(cfg.str("color_right"));
        if (cfg.has("color_left")) col_hand[1] = parse_color(cfg.str("color_left"));
        artist = cfg.str("artist");
        track.load(dir + "/notes.txt", kb.lo, kb.hi);

        hitY = H * 0.70f;
        horizonY = H * 0.265f;
        kb.layout(W, hitY, 230 * sc, sc);
        kpersp = (1.0f / s_min - 1.0f) / lookahead;
        sparks.gravity = 1500 * sc;
        combo_y = horizonY + 0.30f * (hitY - horizonY);

        FILE* f = fopen((dir + "/beats.txt").c_str(), "r");
        if (f) {
            Beat b; int bar;
            while (fscanf(f, "%f %d", &b.t, &bar) == 2) { b.bar = bar != 0; beats.push_back(b); }
            fclose(f);
        }

        g.init(W, H);
        bg = load_tex_opt(dir + "/background.png", has_bg);
        std::string fp = dir + "/font.ttf";
        if (FileExists(fp.c_str())) {
            std::vector<int> cps;
            for (int c = 32; c < 256; ++c) cps.push_back(c);
            for (int c : {0x2019, 0x2018, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026}) cps.push_back(c);
            font = LoadFontEx(fp.c_str(), 112, cps.data(), (int)cps.size());
            has_font = font.texture.id != 0;
            if (has_font) SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        }

        for (int i = 0; i < 90; ++i)
            streaks.push_back({ctx.randf(0, 6.2832f), ctx.randf(0, 1), ctx.randf(0.35f, 0.9f), ctx.randf(1.2f, 3.2f) * sc});

        layout_title(ctx, cfg.str("title", "???"));
        horizonY = std::max(H * 0.265f, title_bottom + 120 * sc);   // la piste commence sous le titre
        combo_y = horizonY + 0.30f * (hitY - horizonY);
        build_popups();
        ctx.metric("slots", (double)slots.size());
        ctx.metric("popups", (double)popups.size());
    }

    // ---------------------------------------------------------------- titre caché ("_ _ _")
    float glyph_adv(int cp, float size) const {
        if (!has_font) return size * 0.6f;
        GlyphInfo gi = GetGlyphInfo(font, cp);
        float adv = gi.advanceX > 0 ? (float)gi.advanceX : (float)font.recs[GetGlyphIndex(font, cp)].width;
        return adv * size / font.baseSize;
    }

    static bool is_letter(int cp) {
        return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0xC0 && cp <= 0x24F);
    }

    void layout_title(AnimContext& ctx, const std::string& title) {
        int count = 0;
        int* cps = LoadCodepoints(title.c_str(), &count);
        std::vector<std::vector<int>> words(1);
        for (int i = 0; i < count; ++i) {
            if (cps[i] == ' ') { if (!words.back().empty()) words.emplace_back(); }
            else words.back().push_back(cps[i]);
        }
        UnloadCodepoints(cps);
        if (words.back().empty()) words.pop_back();
        if (words.empty()) return;

        float top = 250 * sc;                      // sous l'interface du haut de TikTok
        std::vector<std::vector<int>> lines;
        for (float L = 104 * sc; L >= 38 * sc; L -= 4 * sc) {
            float slot_w = L * 0.74f, gap = L * 0.12f, space = L * 0.5f;
            auto word_w = [&](const std::vector<int>& w) { return w.size() * slot_w + (w.size() - 1) * gap; };
            lines.clear();
            std::vector<int> line_words;
            float cur = 0;
            bool ok = true;
            for (size_t wi = 0; wi < words.size(); ++wi) {
                float ww = word_w(words[wi]);
                if (ww > W * 0.92f) { ok = false; break; }
                if (!line_words.empty() && cur + space + ww > W * 0.92f) {
                    std::vector<int> ln;
                    for (int k : line_words) ln.push_back(k);
                    lines.push_back(ln);
                    line_words.clear();
                    cur = 0;
                }
                cur += (line_words.empty() ? 0 : space) + ww;
                line_words.push_back((int)wi);
            }
            if (!ok) continue;
            if (!line_words.empty()) lines.push_back(line_words);
            if (lines.size() > 2 && L > 60 * sc) continue;   // 2 lignes max tant que c'est possible
            if (lines.size() > 3) continue;
            letter_size = L;
            // placement des cases
            for (size_t li = 0; li < lines.size(); ++li) {
                float total = 0;
                for (size_t k = 0; k < lines[li].size(); ++k)
                    total += (k ? space : 0) + word_w(words[lines[li][k]]);
                float x = W / 2 - total / 2, y = top + li * L * 1.3f;
                for (size_t k = 0; k < lines[li].size(); ++k) {
                    if (k) x += space;
                    for (int cp : words[lines[li][k]]) {
                        slots.push_back({cp, x + slot_w / 2, y, is_letter(cp), -1});
                        x += slot_w + gap;
                    }
                    x -= gap;
                }
            }
            title_bottom = top + lines.size() * L * 1.3f;
            break;
        }

        // calendrier de dévoilement : ~45 % des lettres pendant le morceau, le reste à la fin (cascade)
        std::vector<int> idx;
        for (size_t i = 0; i < slots.size(); ++i) if (slots[i].letter) idx.push_back((int)i);
        for (size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[ctx.randi(0, (int)i - 1)]);
        int early = (int)std::floor(idx.size() * 0.45f);
        if (idx.size() <= 4) early = std::min<int>(1, (int)idx.size());
        for (int k = 0; k < (int)idx.size(); ++k) {
            if (k < early)
                slots[idx[k]].t_reveal = reveal_t * (0.22f + 0.68f * (k + 1) / (float)(early + 1));
        }
        int c = 0;
        for (auto& s : slots)
            if (s.letter && s.t_reveal < 0) s.t_reveal = reveal_t + 0.05f * c++;
    }

    // ---------------------------------------------------------------- "PERFECT!" et paliers
    void build_popups() {
        const auto& notes = track.notes;
        static const char* words[] = {"PERFECT!", "GREAT!", "PERFECT!", "NICE!"};
        float last = -10;
        int wcount = 0;
        static const int milestones[] = {25, 50, 100, 150, 200, 250, 300, 400, 500, 750, 1000};
        size_t mi = 0;
        size_t i = 0;
        while (i < notes.size()) {
            size_t j = i;
            float xs = 0, vmax = 0;
            int hand_votes = 0;
            while (j < notes.size() && notes[j].t0 - notes[i].t0 < 0.04f) {
                xs += kb.center(notes[j].pitch);
                vmax = std::max(vmax, notes[j].vel / 127.0f);
                hand_votes += notes[j].hand == 0 ? 1 : -1;
                ++j;
            }
            int n = (int)(j - i);
            float t = notes[i].t0;
            if (t < reveal_t - 0.4f) {
                // paliers de combo
                while (mi < sizeof(milestones) / sizeof(int) && (int)j >= milestones[mi]) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d COMBO!", milestones[mi]);
                    popups.push_back({notes[milestones[mi] - 1].t0, W / 2,
                                      horizonY + 0.55f * (hitY - horizonY), 1, col_hand[0], buf});
                    ++mi;
                }
                if (n >= 3 && t - last >= 1.1f) {
                    const char* w = vmax > 0.8f ? "AMAZING!" : words[wcount++ % 4];
                    popups.push_back({t, xs / n, hitY - 150 * sc, 0, col_hand[hand_votes >= 0 ? 0 : 1], w});
                    last = t;
                }
            } else {
                while (mi < sizeof(milestones) / sizeof(int) && (int)j >= milestones[mi]) ++mi;
            }
            i = j;
        }
        std::sort(popups.begin(), popups.end(), [](const Popup& a, const Popup& b) { return a.t < b.t; });
    }

    // ---------------------------------------------------------------- perspective
    // le point de fuite suit doucement la mélodie à venir (mouvement de caméra)
    float vp_x(float t) const {
        const auto& notes = track.notes;
        float sx = 0, sw = 0;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            float tau = n.t0 - t;
            if (tau > 1.8f) break;
            if (tau < 0) continue;
            float w = std::sin(3.14159f * tau / 1.8f);
            sx += w * kb.center(n.pitch);
            sw += w;
        }
        float avg = sw > 0.01f ? sx / sw : W / 2;
        return W / 2 + 0.33f * (avg - W / 2);
    }

    float depth(float tau) const { return 1.0f / (1.0f + kpersp * std::max(0.0f, tau)); }

    Vector2 proj(float X, float tau, float vpx) const {
        float s = depth(tau);
        return Vector2{vpx + (X - vpx) * s, horizonY + (hitY - horizonY) * s};
    }

    // quadrilatère texturé (pour le shader des notes) : haut = loin, bas = proche
    void quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color ct, Color cb, float v0, float v1) const {
        rlSetTexture(g.white.id);
        rlBegin(RL_QUADS);
        rlNormal3f(0, 0, 1);
        rlColor4ub(ct.r, ct.g, ct.b, ct.a); rlTexCoord2f(0, v0); rlVertex2f(tl.x, tl.y);
        rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlTexCoord2f(0, v1); rlVertex2f(bl.x, bl.y);
        rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlTexCoord2f(1, v1); rlVertex2f(br.x, br.y);
        rlColor4ub(ct.r, ct.g, ct.b, ct.a); rlTexCoord2f(1, v0); rlVertex2f(tr.x, tr.y);
        rlEnd();
        rlSetTexture(0);
    }

    void lane_quad(float X0, float X1, float ta, float tb, float vpx, Color c_near, Color c_far, float v_near, float v_far) const {
        Vector2 nl = proj(X0, ta, vpx), nr = proj(X1, ta, vpx);
        Vector2 fl = proj(X0, tb, vpx), fr = proj(X1, tb, vpx);
        quad(fl, fr, nr, nl, c_far, c_near, v_far, v_near);
    }

    void draw_notes(float t, float vpx, bool glow_pass) const {
        const auto& notes = track.notes;
        float gap = std::max(1.5f, kb.whiteW * 0.08f);
        for (int pass = 0; pass < 2; ++pass) {          // touches blanches puis noires (par-dessus)
            for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
                const NoteEv& n = notes[i];
                if (n.t0 > t + lookahead) break;
                bool blk = is_black(n.pitch);
                if ((pass == 1) != blk) continue;
                float ta = std::max(0.0f, n.t0 - t);
                float tb = std::min(lookahead, n.t1 - t);
                if (tb < ta + 0.07f) tb = ta + 0.07f;
                if (n.t1 - t < 0) continue;
                float X0 = kb.x[n.pitch] + (blk ? 1.0f : gap), X1 = kb.x[n.pitch] + kb.w[n.pitch] - (blk ? 1.0f : gap);
                bool act = n.t0 <= t && t < n.t1;
                float v = n.vel / 127.0f;
                Color c = scalec(col_hand[n.hand], (0.85f + 0.25f * v) * (blk ? 0.8f : 1.0f));
                if (act) c = mixc(c, WHITE, 0.3f);
                const int S = 4;
                for (int k = 0; k < S; ++k) {
                    float a0 = ta + (tb - ta) * k / S, a1 = ta + (tb - ta) * (k + 1) / S;
                    float v0 = 1 - (float)k / S, v1 = 1 - (float)(k + 1) / S;
                    float fade = clamp01((lookahead - a1) / 0.4f + 0.2f);   // apparition douce à l'horizon
                    Color cn = withA(c, glow_pass ? (act ? 1.0f : 0.55f) : clamp01((lookahead - a0) / 0.4f + 0.2f));
                    Color cf = withA(c, glow_pass ? (act ? 1.0f : 0.55f) * fade : fade);
                    lane_quad(X0, X1, a0, a1, vpx, cn, cf, v0, v1);
                }
            }
        }
    }

    void draw_lane_lights(float t, float vpx, float strength) const {
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float rel = t - n.t1;
            if (t < n.t0 || rel > 0.2f) continue;
            float attack = std::exp(-(t - n.t0) / 0.2f);
            float a = (0.25f + 0.55f * attack) * (rel > 0 ? 1 - rel / 0.2f : 1) * strength;
            lane_quad(kb.x[n.pitch], kb.x[n.pitch] + kb.w[n.pitch], 0, 0.55f, vpx,
                      withA(col_hand[n.hand], a), withA(col_hand[n.hand], 0), 1, 0);
        }
    }

    void draw_flares(float t, float strength) const {
        const auto& notes = track.notes;
        for (size_t i = track.first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float age = t - n.t0;
            if (age < 0 || age > 0.3f) continue;
            float a = std::pow(1 - age / 0.3f, 2.0f) * (0.55f + 0.45f * n.vel / 127.0f);
            float fw = std::max(kb.whiteW * 5.0f, 130 * sc);
            g.glow_at(kb.center(n.pitch), hitY, fw, fw * 0.32f, withA(mixc(col_hand[n.hand], WHITE, 0.45f), a * strength));
            g.glow_at(kb.center(n.pitch), hitY, fw * 0.35f, fw * 0.35f, withA(WHITE, a * 0.8f * strength));
        }
    }

    float beat_pulse(float t, bool bars_only) const {
        float best = 0;
        for (auto it = beats.rbegin(); it != beats.rend(); ++it) {
            if (it->t > t) continue;
            if (bars_only && !it->bar) continue;
            best = std::exp(-(t - it->t) / (bars_only ? 0.3f : 0.16f));
            break;
        }
        return best;
    }

    // piste : sol, couloirs, rails, grille au tempo
    void draw_highway(float t, float vpx, bool glow_pass) const {
        Color amb = mixc(col_hand[1], col_hand[0], 0.5f);
        if (!glow_pass) {
            Vector2 fl = proj(0, lookahead, vpx), fr = proj(W, lookahead, vpx);
            quad(fl, fr, Vector2{W, hitY}, Vector2{0, hitY}, Color{4, 5, 12, 120}, Color{4, 5, 12, 215}, 0, 1);
            for (int p = kb.lo; p <= kb.hi + 1; ++p) {
                if (p <= kb.hi && is_black(p)) continue;
                float X = p <= kb.hi ? kb.x[p] : W;
                bool octave = p % 12 == 0;
                Vector2 a = proj(X, lookahead, vpx), b = proj(X, 0, vpx);
                DrawLineEx(a, b, (octave ? 2.0f : 1.2f) * sc, withA(octave ? amb : WHITE, octave ? 0.28f : 0.06f));
            }
        }
        // rails lumineux sur les bords
        for (int side = 0; side < 2; ++side) {
            float X = side ? W - 2 * sc : 2 * sc;
            DrawLineEx(proj(X, lookahead, vpx), proj(X, 0, vpx), (glow_pass ? 8.0f : 3.5f) * sc,
                       withA(col_hand[side], glow_pass ? 0.9f : 0.8f));
        }
        // grille qui défile au tempo : mesures plus marquées
        for (const auto& b : beats) {
            float tau = b.t - t;
            if (tau <= 0) continue;
            if (tau > lookahead) break;
            float s = depth(tau);
            Vector2 l = proj(0, tau, vpx), r = proj(W, tau, vpx);
            float fade = clamp01((lookahead - tau) / 0.5f);
            float a = (b.bar ? 0.55f : 0.16f) * fade * (glow_pass ? 1.2f : 1.0f);
            DrawLineEx(l, r, (b.bar ? 4.0f : 2.0f) * std::max(0.35f, s) * sc, withA(b.bar ? mixc(amb, WHITE, 0.4f) : WHITE, a));
        }
    }

    void draw_streaks(float t, float vpx, float e) const {
        float Rmax = H * 1.1f;
        for (const auto& s : streaks) {
            float r = std::fmod(s.r0 * Rmax + s.speed * Rmax * 0.9f * t, Rmax);
            float k = r / Rmax;
            float len = r * 0.16f;
            Vector2 d{std::cos(s.ang), std::sin(s.ang)};
            Vector2 a{vpx + d.x * r, horizonY + d.y * r}, b{vpx + d.x * (r + len), horizonY + d.y * (r + len)};
            DrawLineEx(a, b, s.width * (0.4f + k), withA(WHITE, std::pow(k, 1.3f) * (0.10f + 0.30f * e)));
        }
    }

    // ---------------------------------------------------------------- état
    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        float k = std::exp(-dt / 0.45f);
        energy[0] *= k;
        energy[1] *= k;
        const auto& notes = track.notes;
        while (next_note < notes.size() && notes[next_note].t0 < t1) {
            const NoteEv& n = notes[next_note++];
            if (n.t0 < t0 - 0.05f) continue;
            float v = n.vel / 127.0f;
            energy[n.hand] = std::min(1.6f, energy[n.hand] + 0.10f + 0.14f * v);
            sparks.burst(ctx, kb.center(n.pitch), hitY - 2, kb.w[n.pitch], col_hand[n.hand],
                         6 + (int)(v * 10), 1000 * sc * (0.6f + 0.6f * v), t1 - std::max(n.t0, t0));
        }
        if (!final_burst && reveal_t < t1) {
            final_burst = true;
            for (const auto& s : slots)
                if (s.letter)
                    sparks.burst(ctx, s.x, s.y + letter_size * 0.5f, letter_size * 0.6f, col_hand[ctx.randi(0, 1)],
                                 4, 900 * sc, t1 - reveal_t, -3.1f, -0.05f);
        }
        sparks.update(dt, H);
    }

    // ---------------------------------------------------------------- textes
    // texte centré ; outline > 0 : contour sombre (lisible même sur les notes lumineuses)
    void text_center(const char* txt, float x, float y, float size, float spacing, Color c, float outline = 0) const {
        if (!has_font) return;
        Vector2 m = MeasureTextEx(font, txt, size, spacing);
        Vector2 p{x - m.x / 2, y - m.y / 2};
        if (outline > 0) {
            Color o = withA(BLACK, 0.55f * c.a / 255.0f);
            for (int k = 0; k < 8; ++k) {
                float a = k * 0.7854f;
                DrawTextEx(font, txt, Vector2{p.x + std::cos(a) * outline, p.y + std::sin(a) * outline}, size, spacing, o);
            }
        }
        DrawTextEx(font, txt, p, size, spacing, c);
    }

    int combo_at(float t) const {
        const auto& notes = track.notes;
        auto it = std::upper_bound(notes.begin(), notes.end(), t, [](float v, const NoteEv& n) { return v < n.t0; });
        return (int)(it - notes.begin());
    }

    void draw_title_slots(float t, bool glow_pass) const {
        float L = letter_size;
        float slot_w = L * 0.74f;
        for (const auto& s : slots) {
            bool shown = !s.letter || t >= s.t_reveal;
            if (!shown) {
                if (!glow_pass) {
                    float pulse = 0.35f + 0.15f * std::sin(t * 4 + s.x * 0.01f);
                    g.rect(Rectangle{s.x - slot_w * 0.43f, s.y + L * 1.0f, slot_w * 0.86f, 5 * sc}, withA(WHITE, pulse + 0.15f));
                } else {
                    g.rect(Rectangle{s.x - slot_w * 0.43f, s.y + L * 1.0f, slot_w * 0.86f, 5 * sc}, withA(col_hand[0], 0.5f));
                }
                continue;
            }
            float age = s.letter ? t - s.t_reveal : 10;
            float k = ease_out_back(age / 0.32f);
            float size = L * std::max(0.01f, k);
            if (glow_pass) {
                if (s.letter) g.glow_at(s.x, s.y + L * 0.55f, L * 2.2f, L * 2.2f,
                                        withA(mixc(col_hand[0], WHITE, 0.3f), 0.25f + 0.75f * std::exp(-age / 0.35f)));
                continue;
            }
            if (!has_font) continue;
            float adv = glyph_adv(s.cp, size);
            Color c = s.letter ? WHITE : withA(WHITE, 0.8f);
            DrawTextCodepoint(font, s.cp, Vector2{s.x - adv / 2, s.y + (L - size) * 0.5f}, size, c);
            if (s.letter && age < 0.5f) {
                BeginBlendMode(BLEND_ADDITIVE);
                g.glow_at(s.x, s.y + L * 0.55f, L * 1.8f, L * 1.8f, withA(col_hand[0], 0.8f * (1 - age / 0.5f)));
                EndBlendMode();
            }
        }
    }

    void draw_hud(float t) {
        // assombrit le haut pour la lisibilité
        g.vgrad_down(Rectangle{0, 0, W, title_bottom + 120 * sc}, withA(BLACK, 0.7f));
        float hide = clamp01((t - reveal_t) / 0.3f);
        if (hide < 1) text_center("GUESS THE SONG", W / 2, 205 * sc, 40 * sc, 9 * sc, withA(WHITE, 0.8f * (1 - hide)));
        draw_title_slots(t, false);

        // barre de progression vers la réponse
        float by = title_bottom + 30 * sc, bw = W * 0.56f, bx = W / 2 - bw / 2, bh = 8 * sc;
        float p = clamp01(t / std::max(0.1f, reveal_t));
        if (hide < 1) {
            g.begin_shape(0, bh / 2);
            g.rect(Rectangle{bx, by, bw, bh}, withA(Color{70, 70, 90, 255}, 0.8f * (1 - hide)));
            if (p > 0.01f) g.rect(Rectangle{bx, by, bw * p, bh}, withA(mixc(col_hand[0], col_hand[1], p), 1 - hide));
            g.end_shape();
        }

        // compteur de combo
        int combo = combo_at(t);
        if (combo >= 2 && t < reveal_t) {
            const auto& notes = track.notes;
            float since = t - notes[combo - 1].t0;
            float pop = 1 + 0.16f * std::exp(-since / 0.07f);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", combo);
            BeginBlendMode(BLEND_ADDITIVE);
            g.glow_at(W / 2, combo_y, 360 * sc, 200 * sc, withA(mixc(col_hand[0], col_hand[1], 0.5f), 0.25f + 0.25f * std::exp(-since / 0.15f)));
            EndBlendMode();
            text_center(buf, W / 2, combo_y, 124 * sc * pop, 2 * sc, withA(WHITE, 0.95f), 4 * sc);
            text_center("COMBO", W / 2, combo_y + 80 * sc, 36 * sc, 8 * sc, withA(WHITE, 0.75f), 2.5f * sc);
        }

        // "PERFECT!" et paliers
        for (const auto& pu : popups) {
            if (pu.t > t) break;
            float dur = pu.kind ? 1.0f : 0.7f;
            float age = t - pu.t;
            if (age > dur) continue;
            float k = ease_out_back(age / 0.22f);
            float a = 1 - std::pow(age / dur, 2.0f);
            float size = (pu.kind ? 96 : 56) * sc * k;
            float y = pu.y - (pu.kind ? 30 : 110) * sc * (age / dur);
            BeginBlendMode(BLEND_ADDITIVE);
            g.glow_at(pu.x, y, size * 5, size * 1.6f, withA(pu.c, 0.55f * a));
            EndBlendMode();
            text_center(pu.text.c_str(), std::max(size * 2.6f, std::min(W - size * 2.6f, pu.x)), y, size, 3 * sc,
                        withA(mixc(pu.c, WHITE, 0.6f), a), 3.5f * sc);
        }

        // révélation finale
        if (t >= reveal_t) {
            float u = t - reveal_t;
            BeginBlendMode(BLEND_ADDITIVE);
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(WHITE, 0.25f * std::exp(-u / 0.16f)));
            g.glow_at(W / 2, combo_y, W * 1.1f, 320 * sc, withA(col_hand[0], 0.5f * clamp01(u / 0.3f)));
            EndBlendMode();
            float k = ease_out_back((u - 0.15f) / 0.35f);
            if (u > 0.15f) text_center("FULL COMBO!", W / 2, combo_y, 104 * sc * k, 4 * sc, WHITE, 4 * sc);
            float ua = clamp01((u - 0.5f) / 0.4f);
            if (!artist.empty() && ua > 0)
                text_center(artist.c_str(), W / 2, title_bottom + 40 * sc + 12 * sc * (1 - ua), 50 * sc, 2 * sc, withA(WHITE, ua));
        }
    }

    // ---------------------------------------------------------------- halo
    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        float vpx = vp_x(t);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        float pulse = beat_pulse(t, false);
        g.begin_glow();
        g.glow_at(vpx, horizonY, W * 1.2f, 340 * sc, withA(mixc(col_hand[0], col_hand[1], 0.5f), 0.45f + 0.45f * pulse));
        draw_highway(t, vpx, true);
        draw_lane_lights(t, vpx, 1.3f);
        draw_notes(t, vpx, true);
        draw_flares(t, 1.4f);
        sparks.draw(g, 1.0f);
        kb.draw_glow(g, hand, col_hand);
        draw_title_slots(t, true);
        g.end_glow();
    }

    // ---------------------------------------------------------------- image
    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        float vpx = vp_x(t);
        int hand[128]; float since[128], vel[128];
        track.key_states(t, hand, since, vel);
        float e = std::min(1.0f, (energy[0] + energy[1]) * 0.6f);
        float pulse = beat_pulse(t, false), bar_pulse = beat_pulse(t, true);

        if (has_bg) g.tex(bg, Rectangle{0, 0, W, H}, WHITE);
        else ClearBackground(Color{6, 6, 14, 255});

        BeginBlendMode(BLEND_ADDITIVE);
        draw_streaks(t, vpx, e);
        Color amb = mixc(col_hand[1], col_hand[0], energy[0] / std::max(0.01f, energy[0] + energy[1]));
        g.glow_at(vpx, horizonY, W * 1.6f, 520 * sc, withA(amb, 0.25f + 0.2f * e + 0.25f * bar_pulse));
        g.glow_at(vpx, horizonY, 260 * sc, 90 * sc, withA(WHITE, 0.35f + 0.4f * pulse));
        EndBlendMode();

        draw_highway(t, vpx, false);
        BeginBlendMode(BLEND_ADDITIVE);
        draw_lane_lights(t, vpx, 0.8f);
        EndBlendMode();

        g.begin_shape(4, 0);
        draw_notes(t, vpx, false);
        g.end_shape();

        g.composite_glow(1.3f);

        kb.draw(g, hand, since, col_hand, sc);
        BeginBlendMode(BLEND_ADDITIVE);
        draw_flares(t, 1.0f);
        sparks.draw(g, 1.0f);
        EndBlendMode();
        kb.draw_floor(g, H, hand, col_hand, sc);

        draw_hud(t);

        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out)
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    void shutdown(AnimContext&) override {
        g.unload();
        if (has_bg) UnloadTexture(bg);
        if (has_font) UnloadFont(font);
    }
};

Scene* make_highway_scene() { return new HighwayScene(); }
