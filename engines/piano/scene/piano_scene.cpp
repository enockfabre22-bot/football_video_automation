// ============================================================================
//  piano_scene.cpp — Notes qui tombent sur un clavier (style « Synthesia » néon).
//  Scène pour le runtime raylib du sandbox (voir sandbox/profiles/raylib_box2d).
//
//  Données lues dans $PIANO_DIR (par défaut /work/piano), écrites par engines/piano/prepare.py :
//    scene.txt  : réglages (layout, durée, clavier, couleurs)
//    notes.txt  : "début fin pitch vélocité main" en secondes VIDÉO
//    background.png, title.png
//  Le son n'est pas géré ici : finalize.py ajoute audio.wav (vrai piano) à la vidéo muette.
// ============================================================================
#include "anim_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
// Formes arrondies anticrénelées (champ de distance) : notes, touches blanches, touches noires.
static const char* SHAPE_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform int style;
uniform float radius;
float sdRB(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    vec2 uv = fragTexCoord;
    vec2 size = 1.0 / max(fwidth(uv), vec2(1e-5));
    vec2 p = (uv - 0.5) * size;
    float r = min(radius, 0.5 * min(size.x, size.y));
    float d = sdRB(p, 0.5 * size, r);
    float aa = clamp(0.5 - d, 0.0, 1.0);
    vec3 base = fragColor.rgb;
    vec3 col;
    if (style == 0) {                       // note lumineuse
        col = base * mix(0.58, 1.10, smoothstep(0.0, 1.0, uv.y));
        float rim = smoothstep(-3.2, -1.0, d);
        col = mix(col, min(base * 1.25 + 0.30, vec3(1.0)), rim * 0.80);
        float hl = smoothstep(0.05, 0.15, uv.x) * (1.0 - smoothstep(0.15, 0.32, uv.x));
        col += hl * 0.12;
    } else if (style == 1) {                // touche blanche
        col = base * mix(0.80, 1.0, smoothstep(0.0, 0.22, uv.y));
        col *= mix(1.0, 0.86, smoothstep(0.93, 1.0, uv.y));
        float side = smoothstep(-2.2, -0.4, d);
        col = mix(col, base * 0.62, side * 0.7);
    } else {                                // touche noire laquée
        float bevel = smoothstep(0.80, 0.86, uv.y) * (1.0 - smoothstep(0.93, 0.99, uv.y));
        col = base * (0.85 + 0.9 * bevel);
        float hl = smoothstep(0.12, 0.25, uv.x) * (1.0 - smoothstep(0.25, 0.45, uv.x));
        col += vec3(0.11) * hl * (1.0 - uv.y * 0.7);
        float rim = smoothstep(-2.0, -0.3, d);
        col = mix(col, vec3(0.0), rim * 0.55);
    }
    finalColor = vec4(col, fragColor.a * aa);
}
)";

// Flou gaussien séparable (5 échantillons linéaires = 9 taps) pour le halo lumineux (bloom)
static const char* BLUR_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 dir;
void main() {
    vec3 c = texture(texture0, fragTexCoord).rgb * 0.2270270270;
    c += texture(texture0, fragTexCoord + dir * 1.3846153846).rgb * 0.3162162162;
    c += texture(texture0, fragTexCoord - dir * 1.3846153846).rgb * 0.3162162162;
    c += texture(texture0, fragTexCoord + dir * 3.2307692308).rgb * 0.0702702703;
    c += texture(texture0, fragTexCoord - dir * 3.2307692308).rgb * 0.0702702703;
    finalColor = vec4(c, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Outils
// ---------------------------------------------------------------------------
static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float smooth01(float v) { v = clamp01(v); return v * v * (3 - 2 * v); }

static Color mixc(Color a, Color b, float t) {
    t = clamp01(t);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
static Color scalec(Color c, float k, float alpha = 1.0f) {
    auto ch = [&](unsigned char v) { return (unsigned char)std::min(255.0f, v * k); };
    return Color{ch(c.r), ch(c.g), ch(c.b), (unsigned char)(255 * clamp01(alpha))};
}
static Color withA(Color c, float a) { c.a = (unsigned char)(255 * clamp01(a)); return c; }

static bool is_black(int p) { int m = p % 12; return m == 1 || m == 3 || m == 6 || m == 8 || m == 10; }

static Color parse_color(const std::string& s) {
    int r = 255, g = 255, b = 255;
    sscanf(s.c_str(), "%d,%d,%d", &r, &g, &b);
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
}

// Texture de halo : blanc, alpha en gaussienne (bords invisibles)
static Texture2D make_glow_texture(int n) {
    Color* px = (Color*)MemAlloc(n * n * sizeof(Color));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
            float a = std::exp(-(dx * dx + dy * dy) * 4.0f);
            if (dx * dx + dy * dy > 1) a = 0;
            px[y * n + x] = Color{255, 255, 255, (unsigned char)(255 * a)};
        }
    Image img = {px, n, n, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

// Dégradé vertical : transparent en haut -> opaque en bas
static Texture2D make_vgrad_texture(int n) {
    Color* px = (Color*)MemAlloc(n * sizeof(Color));
    for (int y = 0; y < n; ++y) {
        float a = (float)y / (n - 1);
        px[y] = Color{255, 255, 255, (unsigned char)(255 * a * a)};
    }
    Image img = {px, 1, n, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

struct NoteEv { float t0, t1; int pitch, vel, hand; };
struct Spark { float x, y, vx, vy, life, max_life, size; Color col; };
struct Dust { float x, y0, r, speed, phase, alpha; };

struct DrawNote { Rectangle r; Color c; bool active; float vel; };

// ---------------------------------------------------------------------------
// Scène
// ---------------------------------------------------------------------------
class PianoScene : public Scene {
    // réglages
    std::string dir;
    bool portrait = true;
    float W = 1080, H = 1920;
    float duration = 30, lookahead = 2.6f, fade_out = 0.35f;
    float title_hold = 0;          // 0 = titre permanent ; sinon disparaît après N secondes
    int key_lo = 48, key_hi = 84;
    Color col_hand[2] = {{0, 225, 255, 255}, {255, 70, 170, 255}};

    // géométrie
    float hitY = 1370, felt = 12, keyH = 250, whiteW = 40, blackW = 24, blackH = 160, pps = 500;
    float keyX[128] = {0}, keyW[128] = {0};
    float beamH = 380;

    // données
    std::vector<NoteEv> notes;
    float max_dur = 1;
    size_t next_hit = 0;
    std::vector<Spark> sparks;
    std::vector<Dust> dust;
    float energy[2] = {0, 0};

    // ressources
    Texture2D bg{}, title{}, white{}, glow{}, vgrad{};
    bool has_bg = false, has_title = false;
    Shader shape{}, blur{};
    int loc_style = -1, loc_radius = -1, loc_dir = -1;
    RenderTexture2D glowA{}, glowB{};
    int gw = 270, gh = 480;

public:
    void init(AnimContext& ctx) override {
        const char* env = getenv("PIANO_DIR");
        dir = env ? env : "/work/piano";
        W = (float)ctx.width;
        H = (float)ctx.height;
        load_settings();
        load_notes();
        layout();

        white = LoadTextureFromImage(GenImageColor(1, 1, WHITE));
        glow = make_glow_texture(128);
        vgrad = make_vgrad_texture(256);
        std::string p = dir + "/background.png";
        if (FileExists(p.c_str())) { bg = LoadTexture(p.c_str()); has_bg = bg.id != 0; SetTextureFilter(bg, TEXTURE_FILTER_BILINEAR); }
        p = dir + "/title.png";
        if (FileExists(p.c_str())) { title = LoadTexture(p.c_str()); has_title = title.id != 0; SetTextureFilter(title, TEXTURE_FILTER_BILINEAR); }

        shape = LoadShaderFromMemory(nullptr, SHAPE_FS);
        loc_style = GetShaderLocation(shape, "style");
        loc_radius = GetShaderLocation(shape, "radius");
        blur = LoadShaderFromMemory(nullptr, BLUR_FS);
        loc_dir = GetShaderLocation(blur, "dir");

        gw = std::max(64, (int)(W / 4));
        gh = std::max(64, (int)(H / 4));
        glowA = LoadRenderTexture(gw, gh);
        glowB = LoadRenderTexture(gw, gh);
        SetTextureFilter(glowA.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(glowB.texture, TEXTURE_FILTER_BILINEAR);

        for (int i = 0; i < 46; ++i) {
            Dust d;
            d.x = ctx.randf(0, W);
            d.y0 = ctx.randf(0, hitY);
            d.r = ctx.randf(3, 13) * W / 1080;
            d.speed = ctx.randf(10, 34);
            d.phase = ctx.randf(0, 6.28f);
            d.alpha = ctx.randf(0.05f, 0.16f);
            dust.push_back(d);
        }
        sparks.reserve(3000);
        ctx.metric("notes", (double)notes.size());
        ctx.metric("key_lo", key_lo);
        ctx.metric("key_hi", key_hi);
    }

    void load_settings() {
        std::ifstream in(dir + "/scene.txt");
        std::string line;
        std::map<std::string, std::string> kv;
        while (std::getline(in, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string v = line.substr(eq + 1);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            kv[line.substr(0, eq)] = v;
        }
        if (kv.count("layout")) portrait = kv["layout"] != "landscape";
        if (kv.count("duration")) duration = std::stof(kv["duration"]);
        if (kv.count("lookahead")) lookahead = std::stof(kv["lookahead"]);
        if (kv.count("fade_out")) fade_out = std::stof(kv["fade_out"]);
        if (kv.count("title_hold")) title_hold = std::stof(kv["title_hold"]);
        if (kv.count("key_lo")) key_lo = std::stoi(kv["key_lo"]);
        if (kv.count("key_hi")) key_hi = std::stoi(kv["key_hi"]);
        if (kv.count("color_right")) col_hand[0] = parse_color(kv["color_right"]);
        if (kv.count("color_left")) col_hand[1] = parse_color(kv["color_left"]);
    }

    void load_notes() {
        FILE* f = fopen((dir + "/notes.txt").c_str(), "r");
        if (!f) { printf("[RUNTIME] notes.txt introuvable dans %s\n", dir.c_str()); return; }
        NoteEv n;
        while (fscanf(f, "%f %f %d %d %d", &n.t0, &n.t1, &n.pitch, &n.vel, &n.hand) == 5) {
            if (n.pitch < key_lo || n.pitch > key_hi) continue;
            n.hand = n.hand ? 1 : 0;
            notes.push_back(n);
            max_dur = std::max(max_dur, n.t1 - n.t0);
        }
        fclose(f);
        std::sort(notes.begin(), notes.end(), [](const NoteEv& a, const NoteEv& b) { return a.t0 < b.t0; });
    }

    void layout() {
        float s = W / 1080.0f;
        if (portrait) {
            hitY = H * 0.715f;
            keyH = 245 * s;
            beamH = 520 * s;
        } else {
            hitY = H * 0.79f;
            keyH = H - hitY - 36;
            beamH = 260;
        }
        felt = std::max(10.0f, 12 * s);
        pps = hitY / lookahead;

        int whites = 0;
        for (int p = key_lo; p <= key_hi; ++p) if (!is_black(p)) whites++;
        whiteW = W / std::max(1, whites);
        blackW = whiteW * 0.60f;
        blackH = keyH * 0.63f;
        // décalage réaliste des touches noires dans leur groupe (2 et 3)
        static const float shift[12] = {0, -0.13f, 0, 0.13f, 0, 0, -0.16f, 0, 0, 0, 0.16f, 0};
        int wi = 0;
        for (int p = key_lo; p <= key_hi; ++p) {
            if (!is_black(p)) {
                keyX[p] = wi * whiteW;
                keyW[p] = whiteW;
                wi++;
            } else {
                float boundary = wi * whiteW;
                keyX[p] = boundary - blackW / 2 + shift[p % 12] * blackW;
                keyW[p] = blackW;
            }
        }
    }

    // ---------------------------------------------------------------- état
    void update(AnimContext& ctx, float dt) override {
        float t0 = (float)ctx.time, t1 = t0 + dt;
        float k = std::exp(-dt / 0.55f);
        energy[0] *= k;
        energy[1] *= k;
        while (next_hit < notes.size() && notes[next_hit].t0 < t1) {
            const NoteEv& n = notes[next_hit++];
            if (n.t0 < t0 - 0.05f) continue;
            float v = n.vel / 127.0f;
            energy[n.hand] += 0.10f + 0.16f * v;
            spawn_sparks(ctx, n, t1 - std::max(n.t0, t0));
        }
        energy[0] = std::min(energy[0], 1.6f);
        energy[1] = std::min(energy[1], 1.6f);

        float g = 1500.0f * (W / 1080.0f);
        float drag = std::exp(-dt * 2.2f);
        for (auto& s : sparks) {
            s.vx *= drag;
            s.vy = s.vy * drag + g * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.life -= dt;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(),
                                    [&](const Spark& s) { return s.life <= 0 || s.y > H; }),
                     sparks.end());
    }

    void spawn_sparks(AnimContext& ctx, const NoteEv& n, float advance) {
        float v = n.vel / 127.0f;
        int count = 6 + (int)(v * 12);
        if (sparks.size() > 2400) count = 2;
        float cx = keyX[n.pitch] + keyW[n.pitch] / 2;
        float sc = W / 1080.0f;
        for (int i = 0; i < count; ++i) {
            Spark s;
            s.x = cx + ctx.randf(-0.3f, 0.3f) * keyW[n.pitch];
            s.y = hitY - 2;
            float ang = ctx.randf(-2.6f, -0.55f);              // vers le haut, en éventail
            float spd = ctx.randf(300, 900) * sc * (0.6f + 0.6f * v);
            s.vx = std::cos(ang) * spd;
            s.vy = std::sin(ang) * spd;
            s.max_life = s.life = ctx.randf(0.30f, 0.85f);
            s.size = ctx.randf(2.0f, 4.0f) * sc;
            s.col = col_hand[n.hand];
            s.x += s.vx * advance;
            s.y += s.vy * advance;
            s.life -= advance;
            sparks.push_back(s);
        }
    }

    // ---------------------------------------------------------------- calculs sans état
    bool active_at(const NoteEv& n, float t) const { return n.t0 <= t && t < n.t1; }

    size_t first_visible(float t) const {
        float tmin = t - max_dur - 0.5f;
        auto it = std::lower_bound(notes.begin(), notes.end(), tmin,
                                   [](const NoteEv& a, float v) { return a.t0 < v; });
        return (size_t)(it - notes.begin());
    }

    void collect_notes(float t, std::vector<DrawNote>& whites_v, std::vector<DrawNote>& blacks_v) const {
        float gap = std::max(1.5f, whiteW * 0.07f);
        for (size_t i = first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t + lookahead + 0.1f) break;
            float yb = hitY - (n.t0 - t) * pps;
            float yt = hitY - (n.t1 - t) * pps;
            if (yb - yt < 14) yt = yb - 14;
            if (yt >= hitY - 1 || yb < -20) continue;
            yb = std::min(yb, hitY + felt * 0.8f);
            bool blk = is_black(n.pitch);
            float x = keyX[n.pitch] + (blk ? 1.0f : gap);
            float w = keyW[n.pitch] - (blk ? 2.0f : 2 * gap);
            bool act = active_at(n, t);
            float v = n.vel / 127.0f;
            float bright = (0.80f + 0.25f * v) * (blk ? 0.80f : 1.0f);
            Color c = scalec(col_hand[n.hand], act ? bright * 1.25f : bright);
            if (act) c = mixc(c, WHITE, 0.18f);
            DrawNote d{{x, yt, w, yb - yt}, c, act, v};
            (blk ? blacks_v : whites_v).push_back(d);
        }
    }

    // touche enfoncée -> (main, vélocité, temps depuis l'attaque) ; -1 si relâchée
    void key_states(float t, int* hand, float* since, float* vel) const {
        for (int p = 0; p < 128; ++p) { hand[p] = -1; since[p] = 99; vel[p] = 0; }
        for (size_t i = first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            if (active_at(n, t) && t - n.t0 <= since[n.pitch]) {
                hand[n.pitch] = n.hand;
                since[n.pitch] = t - n.t0;
                vel[n.pitch] = n.vel / 127.0f;
            }
        }
    }

    void draw_tex(const Texture2D& tex, Rectangle dst, Color c) const {
        DrawTexturePro(tex, Rectangle{0, 0, (float)tex.width, (float)tex.height}, dst, Vector2{0, 0}, 0, c);
    }

    void draw_shape(Rectangle r, Color c) const {
        DrawTexturePro(white, Rectangle{0, 0, 1, 1}, r, Vector2{0, 0}, 0, c);
    }

    void begin_shape(int style, float radius) {
        SetShaderValue(shape, loc_style, &style, SHADER_UNIFORM_INT);
        SetShaderValue(shape, loc_radius, &radius, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(shape);
    }

    // éléments lumineux : dessinés dans le tampon de halo (render_offscreen) et/ou à l'écran
    void draw_beams(float t, float strength) const {
        for (size_t i = first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float rel = t - n.t1;
            if (t < n.t0 || rel > 0.25f) continue;
            float attack = std::exp(-(t - n.t0) / 0.22f);
            float a = (0.30f + 0.45f * attack) * (rel > 0 ? 1 - rel / 0.25f : 1) * (0.6f + 0.4f * n.vel / 127.0f);
            float w = keyW[n.pitch];
            float h = beamH * (0.75f + 0.25f * attack);
            draw_tex(vgrad, Rectangle{keyX[n.pitch] + w * 0.08f, hitY - h, w * 0.84f, h},
                     withA(col_hand[n.hand], a * strength));
        }
    }

    void draw_flares(float t, float strength) const {
        for (size_t i = first_visible(t); i < notes.size(); ++i) {
            const NoteEv& n = notes[i];
            if (n.t0 > t) break;
            float age = t - n.t0;
            if (age < 0 || age > 0.35f) continue;
            float a = std::pow(1 - age / 0.35f, 2.0f) * (0.5f + 0.5f * n.vel / 127.0f);
            float cx = keyX[n.pitch] + keyW[n.pitch] / 2;
            float sc = W / 1080.0f;
            float fw = std::max(whiteW * 5.5f, 130.0f * sc), fh = std::max(whiteW * 1.6f, 44.0f * sc);
            draw_tex(glow, Rectangle{cx - fw / 2, hitY - fh / 2, fw, fh}, withA(mixc(col_hand[n.hand], WHITE, 0.45f), a * strength));
            float cw = fw * 0.42f;
            draw_tex(glow, Rectangle{cx - cw / 2, hitY - cw / 2, cw, cw}, withA(WHITE, a * 0.9f * strength));
        }
    }

    void draw_sparks(float strength) const {
        for (const auto& s : sparks) {
            float k = clamp01(s.life / s.max_life);
            Color c = withA(mixc(s.col, WHITE, 0.55f * k), k * strength);
            Vector2 a{s.x, s.y}, b{s.x - s.vx * 0.022f, s.y - s.vy * 0.022f};
            DrawLineEx(a, b, s.size * (0.5f + 0.5f * k), c);
            float r = s.size * 3.2f;
            draw_tex(glow, Rectangle{s.x - r, s.y - r, 2 * r, 2 * r}, withA(c, 0.5f * k * strength));
        }
    }

    void draw_hit_line(float t, const int* hand, float strength) const {
        draw_shape(Rectangle{0, hitY - 1.5f, W, 3}, withA(WHITE, 0.35f * strength));
        for (int p = key_lo; p <= key_hi; ++p) {
            if (hand[p] < 0) continue;
            float x = keyX[p], w = keyW[p];
            draw_tex(glow, Rectangle{x - w * 0.6f, hitY - 7, w * 2.2f, 14}, withA(col_hand[hand[p]], 0.9f * strength));
        }
        (void)t;
    }

    // ---------------------------------------------------------------- halo (bloom)
    void render_offscreen(AnimContext& ctx) override {
        float t = (float)ctx.time;
        std::vector<DrawNote> wn, bn;
        collect_notes(t, wn, bn);
        int hand[128]; float since[128], vel[128];
        key_states(t, hand, since, vel);

        Camera2D cam{};
        cam.zoom = (float)gw / W;
        BeginTextureMode(glowA);
        ClearBackground(BLACK);
        BeginMode2D(cam);
        BeginBlendMode(BLEND_ADDITIVE);
        for (auto* v : {&wn, &bn})
            for (const auto& d : *v)
                DrawRectangleRec(d.r, withA(d.c, d.active ? 1.0f : 0.5f));
        draw_beams(t, 1.3f);
        draw_flares(t, 1.4f);
        draw_sparks(1.0f);
        draw_hit_line(t, hand, 1.2f);
        for (int p = key_lo; p <= key_hi; ++p)
            if (hand[p] >= 0)
                DrawRectangleRec(Rectangle{keyX[p], hitY + felt, keyW[p], (is_black(p) ? blackH : keyH) * 0.5f},
                                 withA(col_hand[hand[p]], 0.55f));
        EndBlendMode();
        EndMode2D();
        EndTextureMode();

        float spread[2] = {1.0f, 2.2f};
        for (float sp : spread) {
            blur_pass(glowA, glowB, sp / gw, 0);
            blur_pass(glowB, glowA, 0, sp / gh);
        }
    }

    void blur_pass(RenderTexture2D& src, RenderTexture2D& dst, float dx, float dy) {
        float d[2] = {dx, dy};
        SetShaderValue(blur, loc_dir, d, SHADER_UNIFORM_VEC2);
        BeginTextureMode(dst);
        ClearBackground(BLACK);
        BeginShaderMode(blur);
        DrawTextureRec(src.texture, Rectangle{0, 0, (float)gw, -(float)gh}, Vector2{0, 0}, WHITE);
        EndShaderMode();
        EndTextureMode();
    }

    // ---------------------------------------------------------------- image
    void draw(AnimContext& ctx) override {
        float t = (float)ctx.time;
        float sc = W / 1080.0f;
        std::vector<DrawNote> wn, bn;
        collect_notes(t, wn, bn);
        int hand[128]; float since[128], vel[128];
        key_states(t, hand, since, vel);

        // 1. fond
        if (has_bg) draw_tex(bg, Rectangle{0, 0, W, H}, WHITE);
        else ClearBackground(Color{8, 8, 16, 255});

        BeginBlendMode(BLEND_ADDITIVE);
        // 2. lueur d'ambiance qui respire avec la musique
        float e = std::min(1.0f, (energy[0] + energy[1]) * 0.6f);
        Color amb = mixc(col_hand[1], col_hand[0], energy[0] / std::max(0.01f, energy[0] + energy[1]));
        float aw = W * 1.7f, ah = H * 0.55f;
        draw_tex(glow, Rectangle{W / 2 - aw / 2, hitY - ah * 0.62f, aw, ah}, withA(amb, 0.10f + 0.22f * e));
        // 3. poussières lumineuses (bokeh)
        for (const auto& d : dust) {
            float y = std::fmod(d.y0 - d.speed * t + 10 * hitY, hitY);
            float a = d.alpha * (0.6f + 0.4f * std::sin(d.phase + t * 1.3f)) * (0.3f + 0.7f * y / hitY);
            float r = d.r * 2.2f;
            draw_tex(glow, Rectangle{d.x - r, y - r, 2 * r, 2 * r}, withA(mixc(WHITE, amb, 0.5f), a));
        }
        // 4. repères d'octave
        EndBlendMode();
        for (int p = key_lo; p <= key_hi; ++p)
            if (p % 12 == 0) draw_shape(Rectangle{keyX[p] - 0.75f, 0, 1.5f, hitY}, withA(WHITE, 0.06f));
        // 5. faisceaux
        BeginBlendMode(BLEND_ADDITIVE);
        draw_beams(t, 0.8f);
        EndBlendMode();

        // 6. notes (formes anticrénelées)
        float radius = std::min(10.0f * sc, whiteW * 0.28f);
        begin_shape(0, radius);
        for (const auto& d : wn) draw_shape(d.r, d.c);
        for (const auto& d : bn) draw_shape(d.r, d.c);
        EndShaderMode();

        // 7. halo lumineux
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(glowA.texture, Rectangle{0, 0, (float)gw, -(float)gh}, Rectangle{0, 0, W, H},
                       Vector2{0, 0}, 0, withA(WHITE, 1.0f));
        DrawTexturePro(glowA.texture, Rectangle{0, 0, (float)gw, -(float)gh}, Rectangle{0, 0, W, H},
                       Vector2{0, 0}, 0, withA(WHITE, 0.3f));
        EndBlendMode();

        // 8. clavier
        float kTop = hitY;
        float kBottom = hitY + felt + keyH;
        draw_shape(Rectangle{0, kTop, W, kBottom - kTop + 4}, Color{14, 14, 20, 255});
        begin_shape(1, 7.0f * sc);
        for (int p = key_lo; p <= key_hi; ++p) {
            if (is_black(p)) continue;
            Color base = Color{244, 244, 248, 255};
            float press = 0;
            if (hand[p] >= 0) {
                press = 1;
                base = mixc(base, scalec(col_hand[hand[p]], 1.05f), 0.78f);
            }
            draw_shape(Rectangle{keyX[p] + 1.0f, kTop + press * 3 * sc, whiteW - 2.0f, felt + keyH - press * 3 * sc}, base);
        }
        EndShaderMode();
        // ombre portée des touches noires
        for (int p = key_lo; p <= key_hi; ++p)
            if (is_black(p))
                draw_tex(glow, Rectangle{keyX[p] - blackW * 0.15f, kTop + felt + blackH - blackW * 0.5f,
                                         blackW * 1.4f, blackW * 1.2f}, withA(BLACK, 0.45f));
        begin_shape(2, 4.0f * sc);
        for (int p = key_lo; p <= key_hi; ++p) {
            if (!is_black(p)) continue;
            Color base = Color{22, 22, 28, 255};
            float press = 0;
            if (hand[p] >= 0) {
                press = 1;
                base = scalec(col_hand[hand[p]], 0.85f);
            }
            draw_shape(Rectangle{keyX[p], kTop + press * 3 * sc, blackW, felt + blackH - press * 2 * sc}, base);
        }
        EndShaderMode();
        // feutrine (cache le haut des touches) + ligne de frappe
        draw_shape(Rectangle{0, kTop, W, felt}, Color{48, 10, 22, 255});
        draw_shape(Rectangle{0, kTop + felt, W, 5 * sc}, withA(BLACK, 0.35f));

        BeginBlendMode(BLEND_ADDITIVE);
        draw_hit_line(t, hand, 1.0f);
        // lumière sur les touches enfoncées
        for (int p = key_lo; p <= key_hi; ++p) {
            if (hand[p] < 0) continue;
            float a = 0.35f + 0.5f * std::exp(-since[p] / 0.18f);
            float h = (is_black(p) ? blackH : keyH) * 0.7f;
            Rectangle r{keyX[p], kTop + felt, keyW[p], h};
            DrawTexturePro(vgrad, Rectangle{0, 0, 1, -(float)vgrad.height}, r, Vector2{0, 0}, 0,
                           withA(mixc(col_hand[hand[p]], WHITE, 0.3f), a * 0.6f));
        }
        // 9. éclats + étincelles
        draw_flares(t, 1.0f);
        draw_sparks(1.0f);
        EndBlendMode();

        // 10. sol sous le clavier + reflet des touches jouées
        if (kBottom < H) {
            draw_shape(Rectangle{0, kBottom, W, H - kBottom}, Color{6, 6, 10, 255});
            BeginBlendMode(BLEND_ADDITIVE);
            for (int p = key_lo; p <= key_hi; ++p) {
                if (hand[p] < 0) continue;
                float rh = std::min(H - kBottom, 150 * sc);
                DrawTexturePro(vgrad, Rectangle{0, 0, 1, -(float)vgrad.height},
                               Rectangle{keyX[p], kBottom, keyW[p], rh}, Vector2{0, 0}, 0,
                               withA(col_hand[hand[p]], 0.22f));
            }
            EndBlendMode();
            draw_tex(vgrad, Rectangle{0, kBottom - 30 * sc, W, 30 * sc}, withA(BLACK, 0.5f));
        }

        // 11. titre
        float title_a = title_hold > 0 ? clamp01(title_hold + 1.0f - t) : 1.0f;
        if (has_title && title_a > 0) {
            float tw = (float)title.width, th = (float)title.height;
            Vector2 pos = portrait ? Vector2{W / 2 - tw / 2, 150 * sc} : Vector2{24, 18};
            draw_tex(title, Rectangle{pos.x, pos.y, tw, th}, withA(WHITE, title_a));
        }

        // 12. fondu final
        float remain = duration - t;
        if (fade_out > 0 && remain < fade_out)
            DrawRectangleRec(Rectangle{0, 0, W, H}, withA(BLACK, 1 - remain / fade_out));
    }

    void shutdown(AnimContext&) override {
        UnloadRenderTexture(glowA);
        UnloadRenderTexture(glowB);
        UnloadShader(shape);
        UnloadShader(blur);
        UnloadTexture(white);
        UnloadTexture(glow);
        UnloadTexture(vgrad);
        if (has_bg) UnloadTexture(bg);
        if (has_title) UnloadTexture(title);
    }
};

Scene* create_scene() { return new PianoScene(); }
