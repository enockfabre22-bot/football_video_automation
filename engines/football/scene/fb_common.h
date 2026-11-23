// ============================================================================
//  fb_common.h — Moteur partagé des animations foot (raylib, runtime anim_runtime) :
//  formes anticrénelées (SDF), halo lumineux, polices, jetons d'équipe, particules,
//  physique 2D déterministe, habillage (titre, bandeaux, carte du vainqueur) et
//  scène de base (temps de simulation, ralenti, tremblement de caméra).
//
//  Toutes les scènes dessinent dans un espace de conception FIXE de 1080 x 1920 :
//  la résolution réelle (1080p, 1440p, 4K...) n'est qu'une mise à l'échelle.
// ============================================================================
#pragma once

#include "anim_runtime.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static const float DW = 1080.0f;   // largeur de conception
static const float DH = 1920.0f;   // hauteur de conception
static const float PI_F = 3.14159265358979f;
static const float SIM_DT = 1.0f / 240.0f;   // pas fixe de la physique (4 pas par image à 60 i/s)

// ---------------------------------------------------------------------------
// Outils mathématiques
// ---------------------------------------------------------------------------
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smooth01(float v) { v = clamp01(v); return v * v * (3 - 2 * v); }
static inline float ease_out_cubic(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
static inline float ease_in_out(float t) { t = clamp01(t); return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }
static inline float ease_out_back(float t, float k = 1.70158f) {
    t = clamp01(t);
    float c3 = k + 1;
    return 1 + c3 * std::pow(t - 1, 3) + k * std::pow(t - 1, 2);
}
static inline float ease_out_elastic(float t) {
    t = clamp01(t);
    if (t == 0 || t == 1) return t;
    return std::pow(2.0f, -10 * t) * std::sin((t * 10 - 0.75f) * (2 * PI_F / 3)) + 1;
}
// 0 -> 1 -> 0 sur [a, b] avec montée/descente douces de durée f
static inline float window01(float t, float a, float b, float f) {
    if (t < a || t > b) return 0;
    return smooth01((t - a) / f) * smooth01((b - t) / f);
}
static inline float wrap_angle(float a) {
    while (a > PI_F) a -= 2 * PI_F;
    while (a < -PI_F) a += 2 * PI_F;
    return a;
}
static inline float len2(float x, float y) { return std::sqrt(x * x + y * y); }

// Générateur pseudo-aléatoire maison (identique sous Windows et Linux, indépendant de la STL)
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    explicit Rng(uint64_t seed = 1) { reseed(seed); }
    void reseed(uint64_t seed) { s = seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull; next(); next(); }
    uint32_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (uint32_t)(s >> 16);
    }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    bool chance(float p) { return f() < p; }
};

// ---------------------------------------------------------------------------
// Couleurs
// ---------------------------------------------------------------------------
static inline Color rgba(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}
static inline Color withA(Color c, float a) { c.a = (unsigned char)(255 * clamp01(a)); return c; }
static inline Color mulA(Color c, float a) { c.a = (unsigned char)(c.a * clamp01(a)); return c; }
static inline Color mixc(Color a, Color b, float t) {
    t = clamp01(t);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
static inline Color scalec(Color c, float k) {
    auto ch = [&](unsigned char v) { return (unsigned char)std::min(255.0f, v * k); };
    return Color{ch(c.r), ch(c.g), ch(c.b), c.a};
}
static inline float luma(Color c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.0f; }
static inline Color parse_color(const std::string& s) {
    int r = 255, g = 255, b = 255;
    sscanf(s.c_str(), "%d,%d,%d", &r, &g, &b);
    return rgba(r, g, b);
}
static inline Color hsv(float h, float s, float v) {
    Vector3 c = {h, s, v};
    Color out = ColorFromHSV(c.x, c.y, c.z);
    return out;
}
static const Color FB_GOLD = {255, 200, 60, 255};
static const Color HP_GREEN = {70, 230, 110, 255};
static const Color HP_RED = {255, 60, 70, 255};

// ---------------------------------------------------------------------------
// Réglages (scene.txt, clé=valeur) et dossier de travail
// ---------------------------------------------------------------------------
static inline std::string fb_dir() {
    const char* env = getenv("FOOTBALL_DIR");
    return env ? env : "/work/football";
}

struct Settings {
    std::map<std::string, std::string> kv;
    void load(const std::string& path) {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '#') continue;
            std::string v = line.substr(eq + 1);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            kv[line.substr(0, eq)] = v;
        }
    }
    std::string str(const char* k, const std::string& d = "") const { auto it = kv.find(k); return it == kv.end() ? d : it->second; }
    float num(const char* k, float d) const { auto it = kv.find(k); return it == kv.end() ? d : std::stof(it->second); }
    int integer(const char* k, int d) const { auto it = kv.find(k); return it == kv.end() ? d : std::stoi(it->second); }
};

// ---------------------------------------------------------------------------
// Équipes (teams.txt écrit par prepare.py : nom|code|couleur1|couleur2|halo|logo)
// ---------------------------------------------------------------------------
struct Team {
    std::string name, short_name;
    Color c1{255, 255, 255, 255}, c2{30, 30, 30, 255}, glow{255, 255, 255, 255};
    Texture2D logo{};
    bool has_logo = false;
    float logo_aspect = 1.0f;
};

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) out.push_back(item);
    return out;
}

static std::vector<Team> load_teams(const std::string& dir) {
    std::vector<Team> teams;
    std::ifstream in(dir + "/teams.txt");
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        auto f = split(line, '|');
        if (f.size() < 6) continue;
        Team t;
        t.name = f[0];
        t.short_name = f[1];
        t.c1 = parse_color(f[2]);
        t.c2 = parse_color(f[3]);
        t.glow = parse_color(f[4]);
        std::string path = dir + "/" + f[5];
        if (FileExists(path.c_str())) {
            t.logo = LoadTexture(path.c_str());
            if (t.logo.id) {
                GenTextureMipmaps(&t.logo);
                SetTextureFilter(t.logo, TEXTURE_FILTER_TRILINEAR);
                t.has_logo = true;
                t.logo_aspect = (float)t.logo.width / std::max(1, t.logo.height);
            }
        }
        teams.push_back(t);
    }
    return teams;
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
// Formes anticrénelées par champ de distance, dessinées sur un quad :
// 0 disque, 1 anneau, 2 arc (bouts arrondis), 3 rectangle arrondi, 4 disque en dégradé radial,
// 5 rectangle arrondi en dégradé vertical, 6 contour de rectangle arrondi
static const char* SDF_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform int mode;
uniform vec2 size;
uniform vec4 prm;
uniform vec4 col2;
float sdRB(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    vec2 q = (fragTexCoord - 0.5) * size;
    vec4 c = fragColor;
    float d;
    if (mode == 0 || mode == 4) {
        d = length(q) - prm.x;
        if (mode == 4) {
            float t = clamp(length(q) / prm.x, 0.0, 1.0);
            c = mix(fragColor, col2, pow(t, prm.y));
        }
    } else if (mode == 1) {
        d = abs(length(q) - prm.x) - prm.y;
    } else if (mode == 2) {
        float a = 1.5707963 - prm.z;
        float cs = cos(a), sn = sin(a);
        vec2 r = vec2(cs * q.x - sn * q.y, sn * q.x + cs * q.y);
        r.x = abs(r.x);
        vec2 sc = vec2(sin(prm.w), cos(prm.w));
        d = ((sc.y * r.x > sc.x * r.y) ? length(r - sc * prm.x) : abs(length(r) - prm.x)) - prm.y;
    } else if (mode == 6) {
        d = abs(sdRB(q, 0.5 * size - vec2(prm.z), prm.x)) - prm.y;
    } else {
        d = sdRB(q, 0.5 * size - vec2(prm.z), prm.x);
        if (mode == 5) c = mix(fragColor, col2, fragTexCoord.y);
    }
    float aa = max(fwidth(d), 1e-4);
    float a = clamp(0.5 - d / aa, 0.0, 1.0);
    finalColor = vec4(c.rgb, c.a * a);
}
)";

// Flou gaussien séparable (9 taps) pour le halo lumineux
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

// Désaturation (jeton gelé / éliminé)
static const char* GRAY_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform float amount;
void main() {
    vec4 t = texture(texture0, fragTexCoord) * fragColor;
    float g = dot(t.rgb, vec3(0.299, 0.587, 0.114));
    finalColor = vec4(mix(t.rgb, vec3(g), amount), t.a);
}
)";

// ---------------------------------------------------------------------------
// Ressources graphiques
// ---------------------------------------------------------------------------
struct Gfx {
    Texture2D white{}, glow{}, ring{}, vgrad{};
    Shader sdf{}, blur{}, gray{};
    int l_mode = -1, l_size = -1, l_prm = -1, l_col2 = -1, l_dir = -1, l_gray = -1;
    RenderTexture2D glowA{}, glowB{};
    int gw = 270, gh = 480;
    float S = 1.0f;   // échelle conception -> rendu

    static Texture2D make_radial(int n, float (*f)(float)) {
        Color* px = (Color*)MemAlloc(n * n * sizeof(Color));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
                float r = std::sqrt(dx * dx + dy * dy);
                px[y * n + x] = Color{255, 255, 255, (unsigned char)(255 * clamp01(f(r)))};
            }
        Image img = {px, n, n, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D t = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        return t;
    }

    void init(float scale) {
        S = scale;
        white = LoadTextureFromImage(GenImageColor(2, 2, WHITE));
        glow = make_radial(128, [](float r) { return r >= 1 ? 0.0f : std::exp(-r * r * 4.5f) * (1 - r * r); });
        ring = make_radial(256, [](float r) { return std::exp(-std::pow((r - 0.85f) / 0.07f, 2.0f)); });
        {
            Color* px = (Color*)MemAlloc(256 * sizeof(Color));
            for (int y = 0; y < 256; ++y) px[y] = Color{255, 255, 255, (unsigned char)(255 * (y / 255.0f))};
            Image img = {px, 1, 256, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            vgrad = LoadTextureFromImage(img);
            UnloadImage(img);
            SetTextureFilter(vgrad, TEXTURE_FILTER_BILINEAR);
        }
        sdf = LoadShaderFromMemory(nullptr, SDF_FS);
        l_mode = GetShaderLocation(sdf, "mode");
        l_size = GetShaderLocation(sdf, "size");
        l_prm = GetShaderLocation(sdf, "prm");
        l_col2 = GetShaderLocation(sdf, "col2");
        blur = LoadShaderFromMemory(nullptr, BLUR_FS);
        l_dir = GetShaderLocation(blur, "dir");
        gray = LoadShaderFromMemory(nullptr, GRAY_FS);
        l_gray = GetShaderLocation(gray, "amount");
        gw = std::max(64, (int)(DW * S / 4));
        gh = std::max(64, (int)(DH * S / 4));
        glowA = LoadRenderTexture(gw, gh);
        glowB = LoadRenderTexture(gw, gh);
        SetTextureFilter(glowA.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(glowB.texture, TEXTURE_FILTER_BILINEAR);
    }

    void unload() {
        UnloadRenderTexture(glowA);
        UnloadRenderTexture(glowB);
        UnloadShader(sdf);
        UnloadShader(blur);
        UnloadShader(gray);
        UnloadTexture(white);
        UnloadTexture(glow);
        UnloadTexture(ring);
        UnloadTexture(vgrad);
    }

    // --- primitives ---
    void tex(const Texture2D& t, Rectangle dst, Color c, float rot = 0) const {
        if (rot == 0) {
            DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height}, dst, Vector2{0, 0}, 0, c);
        } else {
            Rectangle d2 = {dst.x + dst.width / 2, dst.y + dst.height / 2, dst.width, dst.height};
            DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height}, d2,
                           Vector2{dst.width / 2, dst.height / 2}, rot, c);
        }
    }
    void tex_center(const Texture2D& t, float x, float y, float w, float h, Color c, float rot_deg = 0) const {
        DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height}, Rectangle{x, y, w, h},
                       Vector2{w / 2, h / 2}, rot_deg, c);
    }
    void rect(Rectangle r, Color c) const {
        DrawTexturePro(white, Rectangle{0, 0, (float)white.width, (float)white.height}, r, Vector2{0, 0}, 0, c);
    }
    void glow_at(float x, float y, float rad, Color c) const {
        tex(glow, Rectangle{x - rad, y - rad, 2 * rad, 2 * rad}, c);
    }
    void vgrad_rect(Rectangle r, Color c, bool down = false) const {
        Rectangle src = down ? Rectangle{0, 256, 1, -256} : Rectangle{0, 0, 1, 256};
        DrawTexturePro(vgrad, src, r, Vector2{0, 0}, 0, c);
    }

    void shape(int mode, Rectangle dst, float p0, float p1, float p2, float p3, Color c, Color c2 = BLANK) {
        float sz[2] = {dst.width, dst.height};
        float prm[4] = {p0, p1, p2, p3};
        float col2[4] = {c2.r / 255.0f, c2.g / 255.0f, c2.b / 255.0f, c2.a / 255.0f};
        BeginShaderMode(sdf);
        SetShaderValue(sdf, l_mode, &mode, SHADER_UNIFORM_INT);
        SetShaderValue(sdf, l_size, sz, SHADER_UNIFORM_VEC2);
        SetShaderValue(sdf, l_prm, prm, SHADER_UNIFORM_VEC4);
        SetShaderValue(sdf, l_col2, col2, SHADER_UNIFORM_VEC4);
        DrawTexturePro(white, Rectangle{0, 0, (float)white.width, (float)white.height}, dst, Vector2{0, 0}, 0, c);
        EndShaderMode();
    }
    void disc(float x, float y, float r, Color c) {
        float m = r + 2;
        shape(0, Rectangle{x - m, y - m, 2 * m, 2 * m}, r, 0, 0, 0, c);
    }
    void disc_grad(float x, float y, float r, Color center, Color edge, float power = 1.6f) {
        float m = r + 2;
        shape(4, Rectangle{x - m, y - m, 2 * m, 2 * m}, r, power, 0, 0, center, edge);
    }
    void ring_(float x, float y, float r, float half_thick, Color c) {
        float m = r + half_thick + 2;
        shape(1, Rectangle{x - m, y - m, 2 * m, 2 * m}, r, half_thick, 0, 0, c);
    }
    // arc centré sur l'angle a_center (radians, repère écran), demi-ouverture half_ap
    void arc(float x, float y, float r, float half_thick, float a_center, float half_ap, Color c) {
        if (half_ap <= 0.001f) return;
        if (half_ap >= PI_F - 0.001f) { ring_(x, y, r, half_thick, c); return; }
        float m = r + half_thick + 2;
        shape(2, Rectangle{x - m, y - m, 2 * m, 2 * m}, r, half_thick, a_center, half_ap, c);
    }
    // arc de jauge : part du haut, sens horaire, fraction 0..1
    void gauge(float x, float y, float r, float half_thick, float frac, Color c) {
        frac = clamp01(frac);
        arc(x, y, r, half_thick, -PI_F / 2 + PI_F * frac, PI_F * frac, c);
    }
    void rrect(Rectangle r, float radius, Color c) { shape(3, r, radius, 0, 0, 0, c); }
    void rrect_grad(Rectangle r, float radius, Color top, Color bottom) { shape(5, r, radius, 0, 0, 0, top, bottom); }
    void rrect_line(Rectangle r, float radius, float half_thick, Color c) {
        Rectangle g = {r.x - half_thick - 1, r.y - half_thick - 1, r.width + 2 * half_thick + 2, r.height + 2 * half_thick + 2};
        shape(6, g, radius, half_thick, half_thick + 1, 0, c);
    }

    // --- halo lumineux : dessiner entre begin_glow() et end_glow() (dans render_offscreen) ---
    void begin_glow() {
        Camera2D cam{};
        cam.zoom = (float)gw / DW;
        BeginTextureMode(glowA);
        ClearBackground(BLACK);
        BeginMode2D(cam);
        BeginBlendMode(BLEND_ADDITIVE);
    }
    void end_glow(const std::vector<float>& spreads = {1.0f, 2.4f}) {
        EndBlendMode();
        EndMode2D();
        EndTextureMode();
        for (float sp : spreads) {
            blur_pass(glowA, glowB, sp / gw, 0);
            blur_pass(glowB, glowA, 0, sp / gh);
        }
    }
    void blur_pass(RenderTexture2D& src, RenderTexture2D& dst, float dx, float dy) {
        float d[2] = {dx, dy};
        BeginTextureMode(dst);
        ClearBackground(BLACK);
        BeginShaderMode(blur);
        SetShaderValue(blur, l_dir, d, SHADER_UNIFORM_VEC2);
        DrawTextureRec(src.texture, Rectangle{0, 0, (float)gw, -(float)gh}, Vector2{0, 0}, WHITE);
        EndShaderMode();
        EndTextureMode();
    }
    void composite_glow(float strength) const {
        BeginBlendMode(BLEND_ADDITIVE);
        while (strength > 0.001f) {
            float s = std::min(1.0f, strength);
            DrawTexturePro(glowA.texture, Rectangle{0, 0, (float)gw, -(float)gh}, Rectangle{0, 0, DW, DH},
                           Vector2{0, 0}, 0, withA(WHITE, s));
            strength -= s;
        }
        EndBlendMode();
    }
};

// ---------------------------------------------------------------------------
// Polices (Anton = titres, Poppins = textes) chargées à la taille réelle de rendu
// ---------------------------------------------------------------------------
enum FontKind { F_DISPLAY = 0, F_BOLD = 1, F_SEMI = 2 };

struct Fonts {
    struct Entry { Font f; int design; FontKind kind; };
    std::vector<Entry> list;

    void load(const std::string& dir, float S) {
        std::vector<int> cps;
        for (int c = 32; c < 256; ++c) cps.push_back(c);
        auto add = [&](const char* file, FontKind kind, int design) {
            std::string p = dir + "/" + file;
            if (!FileExists(p.c_str())) return;
            Font f = LoadFontEx(p.c_str(), (int)std::ceil(design * S), cps.data(), (int)cps.size());
            if (f.texture.id == 0) return;
            GenTextureMipmaps(&f.texture);
            SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
            list.push_back({f, design, kind});
        };
        add("Anton-Regular.ttf", F_DISPLAY, 190);
        add("Anton-Regular.ttf", F_DISPLAY, 104);
        add("Anton-Regular.ttf", F_DISPLAY, 60);
        add("Poppins-Bold.ttf", F_BOLD, 64);
        add("Poppins-Bold.ttf", F_BOLD, 34);
        add("Poppins-SemiBold.ttf", F_SEMI, 40);
        add("Poppins-SemiBold.ttf", F_SEMI, 26);
    }
    void unload() { for (auto& e : list) UnloadFont(e.f); }

    const Font& get(FontKind kind, float size) const {
        const Entry* best = nullptr;
        for (const auto& e : list) {
            if (e.kind != kind) continue;
            // plus petite police >= taille demandée, sinon la plus grande
            if (e.design >= size * 0.92f) { if (!best || e.design < best->design || best->design < size * 0.92f) best = &e; }
            else if (!best || (best->design < size * 0.92f && e.design > best->design)) best = &e;
        }
        static Font fallback = GetFontDefault();
        return best ? best->f : fallback;
    }
};

struct TextStyle {
    FontKind kind = F_DISPLAY;
    float size = 60;
    Color color = WHITE;
    float spacing = 0;
    float shadow = 0.55f;     // opacité de l'ombre portée
    float outline = 0;        // épaisseur du contour sombre (unités de conception)
    Color outline_color = {8, 10, 18, 255};
};

static inline float text_width(const Fonts& fonts, const std::string& s, const TextStyle& st) {
    return MeasureTextEx(fonts.get(st.kind, st.size), s.c_str(), st.size, st.spacing).x;
}

// Texte centré horizontalement sur x, verticalement sur y. Réduit si plus large que max_w.
static inline void draw_text(const Fonts& fonts, const std::string& s, float x, float y, TextStyle st,
                             float max_w = 0, float align = 0.5f, float alpha = 1.0f) {
    if (s.empty() || alpha <= 0.002f) return;
    float w = text_width(fonts, s, st);
    if (max_w > 0 && w > max_w) { st.size *= max_w / w; st.spacing *= max_w / w; w = max_w; }
    const Font& f = fonts.get(st.kind, st.size);
    float h = st.size;
    float ox = x - w * align, oy = y - h * 0.5f;
    // Anton : la boîte inclut beaucoup d'espace sous la ligne -> léger recentrage
    if (st.kind == F_DISPLAY) oy += st.size * 0.03f;
    if (st.shadow > 0)
        DrawTextEx(f, s.c_str(), Vector2{ox + st.size * 0.03f, oy + st.size * 0.06f}, st.size, st.spacing,
                   withA(BLACK, st.shadow * alpha));
    if (st.outline > 0) {
        Color oc = mulA(st.outline_color, alpha);
        for (int k = 0; k < 12; ++k) {
            float a = k * PI_F / 6;
            DrawTextEx(f, s.c_str(), Vector2{ox + std::cos(a) * st.outline, oy + std::sin(a) * st.outline}, st.size,
                       st.spacing, oc);
        }
    }
    DrawTextEx(f, s.c_str(), Vector2{ox, oy}, st.size, st.spacing, mulA(st.color, alpha));
}

static inline std::string upper(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    // lettres accentuées en UTF-8 (é -> É, ø -> Ø...) : 0xC3 0xA0..0xBE -> 0xC3 0x80..0x9E
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if ((unsigned char)s[i] == 0xC3 && (unsigned char)s[i + 1] >= 0xA0 && (unsigned char)s[i + 1] <= 0xBE &&
            (unsigned char)s[i + 1] != 0xB7)
            s[i + 1] = (char)((unsigned char)s[i + 1] - 0x20);
    return s;
}

// ---------------------------------------------------------------------------
// Jeton d'équipe : anneau aux couleurs du club, disque clair, écusson, reflet
// ---------------------------------------------------------------------------
struct TokenFx {
    float scale = 1.0f;
    float alpha = 1.0f;
    float flash = 0.0f;      // éclair blanc (coup reçu)
    float red = 0.0f;        // voile rouge (dégâts)
    float gray = 0.0f;       // désaturation (gelé / éliminé)
    float tilt = 0.0f;       // rotation de l'écusson (degrés)
    float shadow = 1.0f;
};

static void draw_token(Gfx& g, const Team& t, float x, float y, float r, const TokenFx& fx = TokenFx()) {
    r *= fx.scale;
    if (r < 1 || fx.alpha <= 0.003f) return;
    float a = fx.alpha;
    Color c1 = mixc(t.c1, rgba(150, 150, 160), fx.gray);
    Color c2 = mixc(t.c2, rgba(90, 90, 100), fx.gray);
    if (fx.shadow > 0) g.glow_at(x + r * 0.08f, y + r * 0.30f, r * 1.45f, withA(BLACK, 0.62f * a * fx.shadow));
    // anneau extérieur en dégradé (relief)
    g.disc_grad(x, y, r, mulA(scalec(c1, 1.12f), a), mulA(scalec(c1, 0.70f), a), 2.2f);
    g.ring_(x, y, r * 0.855f, r * 0.032f, mulA(c2, a));
    // disque intérieur clair
    g.disc_grad(x, y, r * 0.80f, mulA(rgba(255, 255, 255), a), mulA(rgba(206, 212, 224), a), 2.0f);
    if (t.has_logo) {
        float box = r * 1.16f;
        float w = t.logo_aspect >= 1 ? box : box * t.logo_aspect;
        float h = t.logo_aspect >= 1 ? box / t.logo_aspect : box;
        Color lc = withA(WHITE, a);
        if (fx.gray > 0.01f) {
            BeginShaderMode(g.gray);
            SetShaderValue(g.gray, g.l_gray, &fx.gray, SHADER_UNIFORM_FLOAT);
            g.tex_center(t.logo, x, y, w, h, lc, fx.tilt);
            EndShaderMode();
        } else {
            g.tex_center(t.logo, x, y, w, h, lc, fx.tilt);
        }
    }
    // reflet spéculaire + liseré
    g.arc(x, y, r * 0.70f, r * 0.035f, -PI_F / 2 - 0.55f, 0.42f, withA(WHITE, 0.55f * a));
    g.ring_(x, y, r - 0.8f, 1.1f, withA(WHITE, 0.30f * a));
    if (fx.red > 0.003f) g.disc(x, y, r, withA(rgba(255, 40, 50), 0.55f * fx.red * a));
    if (fx.flash > 0.003f) g.disc(x, y, r, withA(WHITE, 0.85f * fx.flash * a));
}

// halo du jeton (à dessiner dans la passe de halo)
static void draw_token_glow(Gfx& g, const Team& t, float x, float y, float r, float strength) {
    g.glow_at(x, y, r * 1.9f, withA(t.glow, 0.55f * strength));
}

// ---------------------------------------------------------------------------
// Particules et effets (avancés dans update(), dessinés sans modifier l'état)
// ---------------------------------------------------------------------------
struct Particle {
    int kind;          // 0 confetti, 1 étincelle, 2 éclat de verre, 3 éclaboussure, 4 poussière lumineuse
    float x, y, vx, vy, rot, vrot, w, h, life, max_life, drag, grav;
    Color c;
};
struct Shock { float x, y, r0, r1, life, max_life, thick; Color c; };
struct FloatText { std::string s; float x, y, vy, life, max_life, size; Color c; };

struct Fx {
    std::vector<Particle> parts;
    std::vector<Shock> shocks;
    std::vector<FloatText> texts;
    Rng rng{7};
    float cull_max_y = DH + 100;   // au-delà (coordonnées du monde), les particules disparaissent

    void confetti(float x, float y, int n, const std::vector<Color>& cols, float speed = 900, float spread = PI_F,
                  float dir = -PI_F / 2) {
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.kind = 0;
            float a = dir + rng.r(-spread, spread);
            float s = speed * rng.r(0.35f, 1.0f);
            p.x = x; p.y = y; p.vx = std::cos(a) * s; p.vy = std::sin(a) * s;
            p.rot = rng.r(0, 360); p.vrot = rng.r(-720, 720);
            p.w = rng.r(10, 20); p.h = rng.r(5, 10);
            p.max_life = p.life = rng.r(1.6f, 3.0f);
            p.drag = 1.8f; p.grav = 900;
            p.c = cols[rng.i(0, (int)cols.size() - 1)];
            parts.push_back(p);
        }
    }
    void sparks(float x, float y, int n, Color c, float speed = 900, float size = 1.0f) {
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.kind = 1;
            float a = rng.r(0, 2 * PI_F), s = speed * rng.r(0.3f, 1.0f);
            p.x = x; p.y = y; p.vx = std::cos(a) * s; p.vy = std::sin(a) * s;
            p.w = rng.r(2.5f, 5.0f) * size;
            p.max_life = p.life = rng.r(0.25f, 0.6f);
            p.drag = 4.0f; p.grav = 300;
            p.c = c;
            parts.push_back(p);
        }
    }
    void shards(float x, float y, float vx, float vy, int n, Color c, float size = 1.0f) {
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.kind = 2;
            float a = rng.r(0, 2 * PI_F), s = rng.r(150, 650);
            p.x = x + rng.r(-6, 6); p.y = y + rng.r(-6, 6);
            p.vx = vx + std::cos(a) * s; p.vy = vy + std::sin(a) * s;
            p.rot = rng.r(0, 360); p.vrot = rng.r(-900, 900);
            p.w = rng.r(8, 22) * size; p.h = rng.r(4, 12) * size;
            p.max_life = p.life = rng.r(0.8f, 1.6f);
            p.drag = 1.2f; p.grav = 1400;
            p.c = c;
            parts.push_back(p);
        }
    }
    void splash(float x, float y, int n, Color c, float speed = 500) {
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.kind = 3;
            float a = rng.r(0, 2 * PI_F), s = speed * rng.r(0.2f, 1.0f);
            p.x = x; p.y = y; p.vx = std::cos(a) * s; p.vy = std::sin(a) * s;
            p.w = rng.r(5, 13);
            p.max_life = p.life = rng.r(0.35f, 0.8f);
            p.drag = 5.0f; p.grav = 0;
            p.c = c;
            parts.push_back(p);
        }
    }
    void shock(float x, float y, float r0, float r1, float life, Color c, float thick = 1.0f) {
        shocks.push_back({x, y, r0, r1, life, life, thick, c});
    }
    void text(const std::string& s, float x, float y, Color c, float size = 46, float life = 0.9f) {
        texts.push_back({s, x, y, -120, life, life, size, c});
    }

    void update(float dt) {
        for (auto& p : parts) {
            float d = std::exp(-p.drag * dt);
            p.vx *= d; p.vy = p.vy * d + p.grav * dt;
            if (p.kind == 0) p.vx += std::sin(p.life * 7 + p.rot * 0.02f) * 220 * dt;   // flottement
            p.x += p.vx * dt; p.y += p.vy * dt;
            p.rot += p.vrot * dt;
            p.life -= dt;
        }
        float cull = cull_max_y;
        parts.erase(std::remove_if(parts.begin(), parts.end(), [cull](const Particle& p) {
            return p.life <= 0 || p.y > cull;
        }), parts.end());
        for (auto& s : shocks) s.life -= dt;
        shocks.erase(std::remove_if(shocks.begin(), shocks.end(), [](const Shock& s) { return s.life <= 0; }), shocks.end());
        for (auto& t : texts) { t.y += t.vy * dt; t.vy *= std::exp(-3 * dt); t.life -= dt; }
        texts.erase(std::remove_if(texts.begin(), texts.end(), [](const FloatText& t) { return t.life <= 0; }), texts.end());
    }

    void draw(Gfx& g, const Fonts& fonts) const {
        for (const auto& s : shocks) {
            float k = 1 - s.life / s.max_life;
            float r = lerpf(s.r0, s.r1, ease_out_cubic(k));
            g.tex(g.ring, Rectangle{s.x - r, s.y - r, 2 * r, 2 * r}, withA(s.c, (1 - k) * 0.9f));
        }
        for (const auto& p : parts) {
            float k = clamp01(p.life / p.max_life);
            if (p.kind == 0) {
                float flip = std::fabs(std::cos(p.rot * 0.05f));
                Color c = withA(scalec(p.c, 0.7f + 0.3f * flip), std::min(1.0f, k * 2.5f));
                DrawRectanglePro(Rectangle{p.x, p.y, p.w, p.h * (0.25f + 0.75f * flip)}, Vector2{p.w / 2, p.h / 2},
                                 p.rot, c);
            } else if (p.kind == 1) {
                Color c = withA(mixc(p.c, WHITE, 0.6f * k), k);
                DrawLineEx(Vector2{p.x, p.y}, Vector2{p.x - p.vx * 0.03f, p.y - p.vy * 0.03f}, p.w * (0.4f + 0.6f * k), c);
            } else if (p.kind == 2) {
                Color c = withA(mixc(p.c, WHITE, 0.35f), k * 0.9f);
                float cs = std::cos(p.rot * DEG2RAD), sn = std::sin(p.rot * DEG2RAD);
                Vector2 a = {p.x + cs * p.w, p.y + sn * p.w};
                Vector2 b = {p.x - sn * p.h, p.y + cs * p.h};
                Vector2 c3 = {p.x - cs * p.w * 0.6f, p.y - sn * p.w * 0.6f};
                DrawTriangle(a, c3, b, c);
                DrawTriangle(a, b, c3, c);
            } else if (p.kind == 3) {
                g.disc(p.x, p.y, p.w * (0.5f + 0.5f * k), withA(p.c, k));
            }
        }
        for (const auto& t : texts) {
            float k = t.life / t.max_life;
            float pop = ease_out_back(clamp01((1 - k) * 6));
            TextStyle st;
            st.size = t.size * (0.6f + 0.4f * pop);
            st.color = t.c;
            st.outline = 3.5f;
            draw_text(fonts, t.s, t.x, t.y, st, 0, 0.5f, std::min(1.0f, k * 3));
        }
    }
    void draw_glow(Gfx& g) const {
        for (const auto& p : parts) {
            float k = clamp01(p.life / p.max_life);
            if (p.kind == 1) g.glow_at(p.x, p.y, 14 * p.w, withA(p.c, 0.8f * k));
            if (p.kind == 2) g.glow_at(p.x, p.y, 3 * p.w, withA(p.c, 0.4f * k));
        }
        for (const auto& s : shocks) {
            float k = 1 - s.life / s.max_life;
            float r = lerpf(s.r0, s.r1, ease_out_cubic(k));
            g.tex(g.ring, Rectangle{s.x - r, s.y - r, 2 * r, 2 * r}, withA(s.c, (1 - k)));
        }
    }
};

// ---------------------------------------------------------------------------
// Physique 2D déterministe : billes, murs (segments épais, éventuellement en rotation),
// arcs de cercle percés d'une ouverture (anneaux), plots, cercle ou rectangle d'arène.
// ---------------------------------------------------------------------------
struct Ball {
    float x = 0, y = 0, vx = 0, vy = 0, r = 40, m = 1;
    float ang = 0, angv = 0;
    bool on = true;
    int id = 0;
};

struct Seg {            // capsule (a-b, demi-épaisseur t). pivot + angvel = barre tournante
    float ax, ay, bx, by, t = 6;
    float rest = 0.6f, friction = 0.0f;
    bool on = true;
    bool rotating = false;
    float px = 0, py = 0, len_a = 0, len_b = 0, ang = 0, angv = 0;
    int tag = 0;
    void update_rot() {
        if (!rotating) return;
        float c = std::cos(ang), s = std::sin(ang);
        ax = px - c * len_a; ay = py - s * len_a;
        bx = px + c * len_b; by = py + s * len_b;
    }
};

struct ArcWall {        // anneau de rayon R, demi-épaisseur t, ouverture de demi-angle gap centrée sur gap_ang
    float cx, cy, R, t = 7;
    float gap_ang = 0, gap = 0.4f, angv = 0;
    float rest = 1.0f;
    bool on = true;
    int tag = 0;
};

struct Peg { float x, y, r, rest = 0.7f; int tag = 0; bool on = true; };

struct BallHit { int a, b; float speed, x, y, nx, ny, att_a, att_b; };   // att_* : vitesse vers l'autre avant le choc
struct WallHit { int ball; float speed, x, y; int kind, tag; };   // kind : 0 arène, 1 segment, 2 arc, 3 plot

struct World {
    std::vector<Ball> balls;
    std::vector<Seg> segs;
    std::vector<ArcWall> arcs;
    std::vector<Peg> pegs;
    float gx = 0, gy = 0;
    float ball_rest = 1.0f;
    bool ball_ball = true;
    // arène circulaire (contenu à l'intérieur) ; R <= 0 : désactivée
    float arena_x = 540, arena_y = 960, arena_R = -1, arena_rest = 1.0f;
    // arène rectangulaire ; w <= 0 : désactivée
    Rectangle box{0, 0, 0, 0};
    float box_rest = 1.0f;
    std::vector<BallHit> ball_hits;
    std::vector<WallHit> wall_hits;
    // grille verticale optionnelle des obstacles fixes (parcours très longs) : build_buckets()
    float bucket_h = 0;
    std::vector<std::vector<int>> seg_b, peg_b;
    std::vector<int> seg_stamp, peg_stamp;
    int stamp = 0;

    void build_buckets(float h, float max_y) {
        bucket_h = h;
        int nb = (int)(max_y / h) + 2;
        seg_b.assign(nb, {});
        peg_b.assign(nb, {});
        auto add = [&](std::vector<std::vector<int>>& B, int idx, float y0, float y1) {
            int a = std::max(0, (int)std::floor(y0 / h)), b = std::min(nb - 1, (int)std::floor(y1 / h));
            for (int k = a; k <= b; ++k) B[k].push_back(idx);
        };
        for (int i = 0; i < (int)segs.size(); ++i) {
            const Seg& s = segs[i];
            float y0 = std::min(s.ay, s.by) - s.t, y1 = std::max(s.ay, s.by) + s.t;
            if (s.rotating) { float L = std::max(s.len_a, s.len_b) + s.t; y0 = s.py - L; y1 = s.py + L; }
            add(seg_b, i, y0, y1);
        }
        for (int i = 0; i < (int)pegs.size(); ++i) add(peg_b, i, pegs[i].y - pegs[i].r, pegs[i].y + pegs[i].r);
        seg_stamp.assign(segs.size(), -1);
        peg_stamp.assign(pegs.size(), -1);
    }

    void collide_seg(Ball& b, const Seg& s) {
        if (!s.on) return;
        float ex = s.bx - s.ax, ey = s.by - s.ay;
        float L2 = ex * ex + ey * ey;
        float t = L2 > 0 ? clamp01(((b.x - s.ax) * ex + (b.y - s.ay) * ey) / L2) : 0;
        float cx = s.ax + ex * t, cy = s.ay + ey * t;
        float wvx = 0, wvy = 0;
        if (s.rotating) { wvx = -s.angv * (cy - s.py); wvy = s.angv * (cx - s.px); }
        resolve_point(b, cx, cy, s.t, wvx, wvy, s.rest, s.friction, 1, s.tag);
    }
    // filtre optionnel : les billes a et b se percutent-elles ?
    std::function<bool(int, int)> collide_filter;

    void resolve_point(Ball& b, float cx, float cy, float rad, float wvx, float wvy, float rest, float friction,
                       int kind, int tag) {
        float dx = b.x - cx, dy = b.y - cy;
        float d = std::sqrt(dx * dx + dy * dy);
        float minD = b.r + rad;
        if (d >= minD || d < 1e-6f) return;
        float nx = dx / d, ny = dy / d;
        b.x = cx + nx * minD;
        b.y = cy + ny * minD;
        float rvx = b.vx - wvx, rvy = b.vy - wvy;
        float vn = rvx * nx + rvy * ny;
        if (vn < 0) {
            float tx = -ny, ty = nx;
            float vt = rvx * tx + rvy * ty;
            vt *= (1 - friction);
            float nvn = -vn * rest;
            rvx = nx * nvn + tx * vt;
            rvy = ny * nvn + ty * vt;
            b.vx = rvx + wvx;
            b.vy = rvy + wvy;
            b.angv = vt / std::max(1.0f, b.r);
            wall_hits.push_back({b.id, -vn, cx + nx * rad, cy + ny * rad, kind, tag});
        }
    }

    void step(float dt) {
        for (auto& s : segs) if (s.rotating) { s.ang += s.angv * dt; s.update_rot(); }
        for (auto& a : arcs) a.gap_ang = wrap_angle(a.gap_ang + a.angv * dt);
        for (auto& b : balls) {
            if (!b.on) continue;
            b.vx += gx * dt; b.vy += gy * dt;
            b.x += b.vx * dt; b.y += b.vy * dt;
            b.ang += b.angv * dt;
        }
        // bille / bille
        if (ball_ball) {
            for (size_t i = 0; i < balls.size(); ++i) {
                Ball& A = balls[i];
                if (!A.on) continue;
                for (size_t j = i + 1; j < balls.size(); ++j) {
                    Ball& B = balls[j];
                    if (!B.on) continue;
                    float dx = B.x - A.x, dy = B.y - A.y;
                    float minD = A.r + B.r;
                    if (std::fabs(dx) >= minD || std::fabs(dy) >= minD) continue;
                    float d2 = dx * dx + dy * dy;
                    if (d2 >= minD * minD) continue;
                    if (collide_filter && !collide_filter((int)i, (int)j)) continue;
                    float d = std::sqrt(d2);
                    float nx = d > 1e-6f ? dx / d : 1, ny = d > 1e-6f ? dy / d : 0;
                    float overlap = minD - d;
                    float ia = 1 / A.m, ib = 1 / B.m;
                    A.x -= nx * overlap * ia / (ia + ib); A.y -= ny * overlap * ia / (ia + ib);
                    B.x += nx * overlap * ib / (ia + ib); B.y += ny * overlap * ib / (ia + ib);
                    float vn = (B.vx - A.vx) * nx + (B.vy - A.vy) * ny;
                    if (vn < 0) {
                        float att_a = A.vx * nx + A.vy * ny, att_b = -(B.vx * nx + B.vy * ny);
                        float imp = -(1 + ball_rest) * vn / (ia + ib);
                        A.vx -= imp * ia * nx; A.vy -= imp * ia * ny;
                        B.vx += imp * ib * nx; B.vy += imp * ib * ny;
                        ball_hits.push_back({(int)i, (int)j, -vn, A.x + nx * A.r, A.y + ny * A.r, nx, ny, att_a, att_b});
                    }
                }
            }
        }
        for (auto& b : balls) {
            if (!b.on) continue;
            // segments (capsules) et plots
            if (bucket_h > 0) {
                stamp++;
                int k0 = std::max(0, (int)std::floor((b.y - b.r) / bucket_h));
                int k1 = std::min((int)seg_b.size() - 1, (int)std::floor((b.y + b.r) / bucket_h));
                for (int k = k0; k <= k1; ++k) {
                    for (int i : seg_b[k]) if (seg_stamp[i] != stamp) { seg_stamp[i] = stamp; collide_seg(b, segs[i]); }
                    for (int i : peg_b[k]) if (peg_stamp[i] != stamp) {
                        peg_stamp[i] = stamp;
                        const Peg& p = pegs[i];
                        if (p.on) resolve_point(b, p.x, p.y, p.r, 0, 0, p.rest, 0.05f, 3, p.tag);
                    }
                }
            } else {
                for (const auto& s : segs) collide_seg(b, s);
                for (const auto& p : pegs)
                    if (p.on) resolve_point(b, p.x, p.y, p.r, 0, 0, p.rest, 0.05f, 3, p.tag);
            }
            // anneaux percés
            for (const auto& a : arcs) {
                if (!a.on) continue;
                float dx = b.x - a.cx, dy = b.y - a.cy;
                float d = std::sqrt(dx * dx + dy * dy);
                if (std::fabs(d - a.R) > b.r + a.t + 2) continue;
                float th = std::atan2(dy, dx);
                float phi = wrap_angle(th - a.gap_ang);
                float px, py;
                if (std::fabs(phi) >= a.gap) { px = a.cx + std::cos(th) * a.R; py = a.cy + std::sin(th) * a.R; }
                else {
                    float e = a.gap_ang + (phi >= 0 ? a.gap : -a.gap);
                    px = a.cx + std::cos(e) * a.R; py = a.cy + std::sin(e) * a.R;
                }
                float wvx = -a.angv * (py - a.cy), wvy = a.angv * (px - a.cx);
                resolve_point(b, px, py, a.t, wvx, wvy, a.rest, 0, 2, a.tag);
            }
            // arène circulaire
            if (arena_R > 0) {
                float dx = b.x - arena_x, dy = b.y - arena_y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d + b.r > arena_R && d > 1e-6f) {
                    float nx = dx / d, ny = dy / d;
                    b.x = arena_x + nx * (arena_R - b.r);
                    b.y = arena_y + ny * (arena_R - b.r);
                    float vn = b.vx * nx + b.vy * ny;
                    if (vn > 0) {
                        b.vx -= (1 + arena_rest) * vn * nx;
                        b.vy -= (1 + arena_rest) * vn * ny;
                        wall_hits.push_back({b.id, vn, arena_x + nx * arena_R, arena_y + ny * arena_R, 0, 0});
                    }
                }
            }
            if (box.width > 0) {
                auto wall = [&](float pen, float nx, float ny, float hx, float hy) {
                    if (pen <= 0) return;
                    b.x += nx * pen; b.y += ny * pen;
                    float vn = b.vx * nx + b.vy * ny;
                    if (vn < 0) {
                        b.vx -= (1 + box_rest) * vn * nx;
                        b.vy -= (1 + box_rest) * vn * ny;
                        wall_hits.push_back({b.id, -vn, hx, hy, 0, 0});
                    }
                };
                wall(box.x - (b.x - b.r), 1, 0, box.x, b.y);
                wall((b.x + b.r) - (box.x + box.width), -1, 0, box.x + box.width, b.y);
                wall(box.y - (b.y - b.r), 0, 1, b.x, box.y);
                wall((b.y + b.r) - (box.y + box.height), 0, -1, b.x, box.y + box.height);
            }
        }
    }

    // garde chaque bille à vitesse constante (jeux de rebonds : aucune perte ni gain d'énergie)
    void normalize_speed(float target, float rate, float dt) {
        for (auto& b : balls) {
            if (!b.on) continue;
            float v = len2(b.vx, b.vy);
            if (v < 1e-3f) { b.vx = target; continue; }
            float nv = lerpf(v, target, clamp01(rate * dt));
            b.vx *= nv / v; b.vy *= nv / v;
        }
    }
};

// ---------------------------------------------------------------------------
// Fond (prépare.py : bg.png) et icônes
// ---------------------------------------------------------------------------
static inline Texture2D load_tex(const std::string& path, bool mip = true) {
    Texture2D t{};
    if (!FileExists(path.c_str())) return t;
    t = LoadTexture(path.c_str());
    if (t.id && mip) { GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); }
    else if (t.id) SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

// ---------------------------------------------------------------------------
// Scène de base : réglages, ressources, horloge de simulation (ralenti), caméra, habillage
// ---------------------------------------------------------------------------
struct Toast { std::string text; int team; float t0; Color accent; std::string tag; };

class FootballScene : public Scene {
public:
    Settings cfg;
    std::vector<Team> teams;
    Gfx g;
    Fonts fonts;
    Fx fx;
    Texture2D bg{};
    std::map<std::string, Texture2D> icons;
    std::string dir;
    Rng frng{1};             // aléatoire de présentation (sons, particules) : n'influence pas la simulation
    Color accent = {47, 107, 255, 255};

    // horloge : la simulation démarre à start_time et avance au rythme time_scale
    float start_time = 1.0f;
    double sim_time = 0;
    float sim_acc = 0;
    float time_scale = 1.0f;
    float hitstop = 0;       // gel bref (impact)
    bool sim_done = false;
    float done_time = -1;    // temps vidéo de la fin de la simulation
    float outro_len = 3.4f;

    // caméra
    float shake = 0, shake_x = 0, shake_y = 0;
    float zoom = 1.0f, zoom_x = DW / 2, zoom_y = DH / 2;

    std::vector<Toast> toasts;

    // --- à fournir par chaque format ---
    virtual void setup(AnimContext& ctx) = 0;
    virtual void sim_step() = 0;            // un pas de SIM_DT de la simulation + traitement des événements
    virtual void after_frame(AnimContext&, float) {}
    virtual float desired_time_scale() { return 1.0f; }
    virtual void draw_world(AnimContext& ctx) = 0;
    virtual void draw_world_glow(AnimContext&) {}
    virtual void draw_hud(AnimContext& ctx) = 0;

    std::string icon_path(const std::string& n) const { return dir + "/icons/" + n + ".png"; }
    const Texture2D* icon(const std::string& n) const {
        auto it = icons.find(n);
        return it == icons.end() || it->second.id == 0 ? nullptr : &it->second;
    }

    void init(AnimContext& ctx) override {
        dir = fb_dir();
        cfg.load(dir + "/scene.txt");
        float S = (float)ctx.width / DW;
        g.init(S);
        fonts.load(dir + "/fonts", S);
        teams = load_teams(dir);
        bg = load_tex(dir + "/bg.png", false);
        for (const char* n : {"heart", "bolt", "shield", "card", "crown", "trophy", "star", "skull", "ball", "bomb", "arrow"})
            icons[n] = load_tex(icon_path(n));
        accent = parse_color(cfg.str("accent", "47,107,255"));
        frng.reseed(ctx.seed * 2654435761u + 17);
        fx.rng.reseed(ctx.seed * 40503u + 3);
        start_time = cfg.num("start_time", 1.0f);
        setup(ctx);
    }

    void shutdown(AnimContext&) override {
        for (auto& t : teams) if (t.has_logo) UnloadTexture(t.logo);
        for (auto& kv : icons) if (kv.second.id) UnloadTexture(kv.second);
        if (bg.id) UnloadTexture(bg);
        fonts.unload();
        g.unload();
    }

    void kick_shake(float amount) { shake = std::max(shake, amount); }

    void sound(AnimContext& ctx, const std::string& base, int variants, float vol) {
        std::string n = variants > 1 ? base + "_" + std::to_string(frng.i(1, variants)) : base;
        ctx.sound(n, vol);
    }

    void toast(const std::string& text, int team, float t, Color c, const std::string& tag = "") {
        toasts.push_back({text, team, t, c, tag});
        if (toasts.size() > 4) toasts.erase(toasts.begin());
    }

    void update(AnimContext& ctx, float dt) override {
        float t = (float)ctx.time;
        if (t >= start_time && !sim_done) {
            float ts = desired_time_scale();
            time_scale = lerpf(time_scale, ts, clamp01(dt * 10));
            if (hitstop > 0) { hitstop -= dt; }
            else {
                sim_acc += dt * time_scale;
                while (sim_acc >= SIM_DT && !sim_done) {
                    sim_acc -= SIM_DT;
                    sim_time += SIM_DT;
                    sim_step();
                }
            }
            if (sim_done && done_time < 0) done_time = t;
        }
        fx.update(dt);
        shake *= std::exp(-dt * 7.0f);
        if (shake < 0.3f) shake = 0;
        shake_x = shake > 0 ? frng.r(-shake, shake) : 0;
        shake_y = shake > 0 ? frng.r(-shake, shake) : 0;
        after_frame(ctx, dt);
        if (done_time >= 0 && t >= done_time + outro_len) ctx.finish();
    }

    void render_offscreen(AnimContext& ctx) override {
        g.begin_glow();
        rlPushMatrix();
        apply_world_transform();
        draw_world_glow(ctx);
        fx.draw_glow(g);
        rlPopMatrix();
        g.end_glow();
    }

    float world_dy = 0;      // décalage vertical du monde (caméra qui suit une course)

    void apply_world_transform() const {
        rlTranslatef(zoom_x + shake_x, zoom_y + shake_y, 0);
        rlScalef(zoom, zoom, 1);
        rlTranslatef(-zoom_x, -zoom_y + world_dy, 0);
    }

    void draw(AnimContext& ctx) override {
        rlPushMatrix();
        rlScalef(g.S, g.S, 1);
        if (bg.id) g.tex(bg, Rectangle{0, 0, DW, DH}, WHITE);
        else g.rect(Rectangle{0, 0, DW, DH}, rgba(8, 10, 20));
        rlPushMatrix();
        apply_world_transform();
        draw_world(ctx);
        fx.draw(g, fonts);
        rlPopMatrix();
        g.composite_glow(cfg.num("glow", 1.0f));
        draw_hud(ctx);
        rlPopMatrix();
    }

    // ------------------------------------------------------------------
    // Habillage commun
    // ------------------------------------------------------------------
    // Pastille (compétition, date...) centrée en x
    void chip(const std::string& text, float x, float y, Color bgc, Color fg, float alpha = 1, float size = 30) {
        TextStyle st; st.kind = F_BOLD; st.size = size; st.color = fg; st.shadow = 0; st.spacing = 2.5f;
        float w = text_width(fonts, text, st) + size * 1.4f;
        float h = size * 1.55f;
        g.rrect(Rectangle{x - w / 2, y - h / 2, w, h}, h / 2, mulA(bgc, alpha));
        draw_text(fonts, text, x, y + size * 0.04f, st, 0, 0.5f, alpha);
    }

    // En-tête : pastille de compétition + titre sur deux lignes. intro = 0..1 (animation d'entrée)
    void header(float t, float y = 245) {
        std::string l1 = cfg.str("title1"), l2 = cfg.str("title2"), comp = cfg.str("chip");
        float in = ease_out_back(clamp01(t / 0.35f), 1.2f);
        float in2 = ease_out_back(clamp01((t - 0.08f) / 0.35f), 1.2f);
        if (!comp.empty()) chip(upper(comp), DW / 2, y - 118, withA(accent, 0.95f), WHITE, clamp01(t / 0.2f), 28);
        TextStyle a; a.size = 128; a.outline = 5; a.shadow = 0.6f; a.spacing = 2;
        draw_text(fonts, upper(l1), DW / 2, y - 18 + (1 - in) * -40, a, 980, 0.5f, clamp01(in));
        TextStyle b; b.size = 70; b.color = FB_GOLD; b.outline = 4; b.shadow = 0.6f; b.spacing = 2;
        draw_text(fonts, upper(l2), DW / 2, y + 84 + (1 - in2) * 30, b, 960, 0.5f, clamp01(in2));
    }

    // Bandeaux d'annonce (éliminations, records...) empilés sous l'arène
    void draw_toasts(float t, float y0, float max_age = 2.2f) {
        int shown = 0;
        for (int i = (int)toasts.size() - 1; i >= 0 && shown < 2; --i) {
            const Toast& to = toasts[i];
            float age = t - to.t0;
            if (age < 0 || age > max_age) continue;
            float in = ease_out_back(clamp01(age / 0.28f));
            float out = 1 - smooth01((age - (max_age - 0.3f)) / 0.3f);
            float y = y0 + shown * 96;
            TextStyle st; st.size = 50; st.shadow = 0; st.spacing = 1.5f;
            float tw = std::min(text_width(fonts, to.text, st), 760.0f);
            float w = tw + (to.team >= 0 ? 130 : 60);
            float x = DW / 2 - w / 2 + (1 - in) * 60;
            float a = clamp01(in) * out;
            g.rrect(Rectangle{x, y - 40, w, 80}, 40, withA(rgba(10, 12, 22), 0.88f * a));
            g.rrect_line(Rectangle{x, y - 40, w, 80}, 40, 2.0f, withA(to.accent, 0.95f * a));
            float tx = x + 30;
            if (to.team >= 0 && to.team < (int)teams.size()) {
                TokenFx f; f.alpha = a; f.shadow = 0;
                draw_token(g, teams[to.team], x + 52, y, 30, f);
                tx = x + 100;
            }
            st.color = to.accent;
            draw_text(fonts, to.text, tx, y, st, 760, 0.0f, a);
            shown++;
        }
    }

    // Carte finale : jeton géant du vainqueur, nom, mention, appel à commenter
    void winner_card(float age, int team, const std::string& line, float cy = 1000) {
        if (team < 0 || team >= (int)teams.size()) return;
        const Team& w = teams[team];
        float k = ease_out_back(clamp01(age / 0.55f), 1.4f);
        float fade = clamp01(age / 0.25f);
        g.rect(Rectangle{0, 0, DW, DH}, withA(rgba(4, 6, 14), 0.62f * fade));
        // rayons tournants
        for (int i = 0; i < 14; ++i) {
            float a = i * 2 * PI_F / 14 + age * 0.35f;
            Vector2 c = {DW / 2, cy - 60};
            Vector2 p1 = {c.x + std::cos(a - 0.09f) * 1300, c.y + std::sin(a - 0.09f) * 1300};
            Vector2 p2 = {c.x + std::cos(a + 0.09f) * 1300, c.y + std::sin(a + 0.09f) * 1300};
            DrawTriangle(c, p2, p1, withA(w.glow, 0.10f * fade));
        }
        g.glow_at(DW / 2, cy - 60, 560 * k, withA(w.glow, 0.55f * fade));
        if (const Texture2D* crown = icon("crown"))
            g.tex_center(*crown, DW / 2, cy - 60 - 250 * k - 20 * std::sin(age * 3), 190 * k, 150 * k, withA(WHITE, fade));
        TokenFx f; f.scale = k;
        draw_token(g, w, DW / 2, cy - 60, 205, f);
        TextStyle n; n.size = 150; n.outline = 6; n.spacing = 2;
        float ka = ease_out_back(clamp01((age - 0.25f) / 0.4f));
        draw_text(fonts, upper(w.name), DW / 2, cy + 260, n, 980, 0.5f, clamp01(ka));
        TextStyle l; l.size = 86; l.color = FB_GOLD; l.outline = 5; l.spacing = 3;
        float kb = ease_out_back(clamp01((age - 0.45f) / 0.4f));
        draw_text(fonts, upper(line), DW / 2, cy + 390, l, 960, 0.5f, clamp01(kb));
        std::string cta = cfg.str("cta", "COMMENT YOUR TEAM");
        float kc = clamp01((age - 1.1f) / 0.35f);
        if (kc > 0) {
            float pulse = 1 + 0.04f * std::sin(age * 7);
            TextStyle c; c.kind = F_BOLD; c.size = 46 * pulse; c.shadow = 0; c.spacing = 1;
            float tw = text_width(fonts, upper(cta), c);
            float bw = tw + 120, bh = 92;
            float by = cy + 520;
            g.rrect(Rectangle{DW / 2 - bw / 2, by - bh / 2, bw, bh}, bh / 2, withA(WHITE, 0.96f * kc));
            c.color = rgba(12, 14, 24);
            draw_text(fonts, upper(cta), DW / 2 - 24, by + 2, c, 0, 0.5f, kc);
            // flèche vers le bas (vers les commentaires)
            float ax = DW / 2 + tw / 2 + 10, ay = by + 2 + 6 * std::sin(age * 8);
            DrawTriangle(Vector2{ax - 16, ay - 10}, Vector2{ax, ay + 12}, Vector2{ax + 16, ay - 10}, withA(rgba(12, 14, 24), kc));
        }
    }

    // Lignes de lumière de l'arène (halo) — utilitaire
    void glow_ring(float x, float y, float r, Color c, float strength) {
        g.ring_(x, y, r, 10, withA(c, strength));
    }
};

Scene* make_royale_scene();
Scene* make_rings_scene();
Scene* make_colorwar_scene();
Scene* make_marble_scene();
Scene* make_knockout_scene();
Scene* make_duel_scene();
