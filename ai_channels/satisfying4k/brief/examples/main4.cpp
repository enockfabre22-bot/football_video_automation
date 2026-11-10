// Animation 2D - raylib + Box2D, export natif 4K.
// LE COMPLEXE INDUSTRIEL (The Grand Cascade) - version 2.
//
// Boucle fermée à 100 % :
//   réservoir invisible (hors écran, en haut) -> valves à engrenages -> pyramide
//   -> moulins broyeurs -> champ de Plinko -> bassin en V inversé
//   -> 2 ascenseurs à godets géants (gauche / droite) qui montent HORS ÉCRAN
//   -> goulottes -> retour dans le sommet du réservoir.
//
// Tout le circuit est visible à l'écran (rien hors cadre).
// Les billes ne "poppent" jamais : les 1200 billes sont posées dans le réservoir
// dès la frame 0 et ne sont ensuite déplacées que par la physique.

#include <raylib.h>
#include <rlgl.h>
#include <Box2D/Box2D.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
    #define PIPE_OPEN  _popen
    #define PIPE_CLOSE _pclose
#else
    #define PIPE_OPEN  popen
    #define PIPE_CLOSE pclose
#endif

// ---------------- CONFIGURATION ----------------

static constexpr int OUTPUT_WIDTH  = 3840;
static constexpr int OUTPUT_HEIGHT = 2160;
static constexpr int FPS = 60;

// L'écran représente ~85,3 m de large x 48 m de haut (45 px/m en 4K).
static constexpr float PIXELS_PER_METER = 45.0f;
static constexpr float WORLD_W = OUTPUT_WIDTH  / PIXELS_PER_METER;
static constexpr float WORLD_H = OUTPUT_HEIGHT / PIXELS_PER_METER;
static constexpr float CX = WORLD_W * 0.5f;   // axe central de la machine
static constexpr float DRAW_SCALE = PIXELS_PER_METER;

static constexpr int TOTAL_BALLS = 000; // peut être modifié
static constexpr int MAX_FRAMES  = 18000;   // 5 min à 60 fps (36000 = 10 min)

static constexpr bool EXPORT_VIDEO = false; // true pour le rendu final
static constexpr bool USE_MSAA = true;

static constexpr float BALL_RADIUS = 0.25f;
static constexpr float GRAVITY = 9.81f;
static constexpr float FIXED_DT = 1.0f / 60.0f;

enum CollisionCategory {
    CAT_WALL    = 0x0001,
    CAT_MACHINE = 0x0002,
    CAT_BALL    = 0x0004
};

// Palette "Neon Industrial ASMR"
static const Color COL_BG      = {12, 16, 22, 255};
static const Color COL_WALL    = {35, 45, 60, 255};
static const Color COL_MACHINE = {20, 28, 38, 255};
static const Color COL_PEG     = {70, 85, 110, 255};
static const Color COL_PADDLE  = {58, 74, 98, 255};
static const Color COL_DECO    = {22, 30, 42, 255};

static const Color COLORS[6] = {
    {255, 30, 110, 255},
    {0, 255, 150, 255},
    {0, 210, 255, 255},
    {255, 210, 0, 255},
    {190, 0, 255, 255},
    {255, 90, 0, 255}
};

static Vector2 WorldToScreen(const b2Vec2& p) { return {p.x * DRAW_SCALE, p.y * DRAW_SCALE}; }
static Vector2 WS(float x, float y) { return {x * DRAW_SCALE, y * DRAW_SCALE}; }

// ---------------- GEOMETRIE DE BASE ----------------

struct BoxWall {
    b2Body* body = nullptr;
    b2Vec2 p1, p2;
    float thickness;
    Color color;

    BoxWall(b2World& world, b2Vec2 start, b2Vec2 end, float thick = 0.4f, Color col = COL_WALL) {
        p1 = start; p2 = end; thickness = thick; color = col;
        b2BodyDef bd;
        bd.position = 0.5f * (start + end);
        bd.angle = std::atan2(end.y - start.y, end.x - start.x);
        body = world.CreateBody(&bd);
        b2PolygonShape box;
        float length = b2Distance(start, end);
        box.SetAsBox(length * 0.5f, thick * 0.5f);
        b2FixtureDef fd;
        fd.shape = &box;
        fd.friction = 0.1f;
        fd.filter.categoryBits = CAT_WALL;
        fd.filter.maskBits = 0xFFFF;
        body->CreateFixture(&fd);
    }

    void Draw() const {
        float length = b2Distance(p1, p2);
        Rectangle rec = {
            body->GetPosition().x * DRAW_SCALE, body->GetPosition().y * DRAW_SCALE,
            length * DRAW_SCALE, thickness * DRAW_SCALE
        };
        Vector2 origin = {rec.width * 0.5f, rec.height * 0.5f};
        DrawRectanglePro(rec, origin, body->GetAngle() * RAD2DEG, color);
        DrawCircleV(WorldToScreen(p1), thickness * DRAW_SCALE * 0.5f, color);
        DrawCircleV(WorldToScreen(p2), thickness * DRAW_SCALE * 0.5f, color);
    }
};

struct Peg {
    b2Body* body = nullptr;
    float radius;

    Peg(b2World& world, float x, float y, float r = 0.2f) : radius(r) {
        b2BodyDef bd; bd.position.Set(x, y);
        body = world.CreateBody(&bd);
        b2CircleShape shape; shape.m_radius = radius;
        b2FixtureDef fd;
        fd.shape = &shape;
        fd.friction = 0.3f;
        fd.restitution = 0.4f;
        fd.filter.categoryBits = CAT_WALL;
        body->CreateFixture(&fd);
    }

    void Draw() const {
        const Vector2 p = WorldToScreen(body->GetPosition());
        DrawCircleV(p, radius * DRAW_SCALE, COL_PEG);
    }
};

// ---------------- MÉCANISMES ----------------

// Ascenseur à godets : une courroie fermée de plateaux cinématiques.
// Montée dans un canal vertical, jusqu'à bien au-dessus de l'écran, déversement
// par basculement des plateaux, retour par une colonne extérieure.
// Aucun téléport : le trajet est une boucle fermée continue.
struct Lift {
    static constexpr float XU = 5.7f;     // axe de la montée
    static constexpr float XR = 1.6f;     // axe de la descente (retour)
    static constexpr float YB = 45.0f;    // bas du trajet
    static constexpr float YT = 2.05f;    // haut du trajet (arc du haut visible)
    static constexpr float HALF_W = 1.4f; // demi-largeur d'un plateau
    static constexpr float HALF_T = 0.15f;
    static constexpr float TILT = 0.5f;  // inclinaison de déversement (rad)

    bool mirror;
    float speed;
    float loopLen;
    float sprocketAngle = 0.0f;
    std::vector<b2Body*> paddles;
    std::vector<float> s;

    struct Pose { float x, y, a; };

    static float R()   { return (XU - XR) * 0.5f; }
    static float XC()  { return (XU + XR) * 0.5f; }
    static float H()   { return YB - YT; }
    static float Len() { return 2.0f * H() + 2.0f * PI * R(); }

    Pose PathPose(float t) const {
        const float r = R(), xc = XC(), h = H(), arc = PI * r, L = Len();
        t = std::fmod(t, L); if (t < 0) t += L;
        float x, y, a = 0.0f;
        if (t < h) {                                   // montée
            x = XU; y = YB - t;
            float k = std::clamp((16.0f - y) / 6.0f, 0.0f, 1.0f);
            a = TILT * k * k * (3.0f - 2.0f * k);
        } else if (t < h + arc) {                      // arc du haut
            float ph = (t - h) / r;
            x = xc + r * std::cos(ph); y = YT - r * std::sin(ph);
            a = TILT * (1.0f - ph / PI);
        } else if (t < 2.0f * h + arc) {               // descente
            x = XR; y = YT + (t - h - arc);
        } else {                                       // arc du bas
            float ph = (t - 2.0f * h - arc) / r;
            x = xc - r * std::cos(ph); y = YB + r * std::sin(ph);
        }
        if (mirror) { x = WORLD_W - x; a = -a; }
        return {x, y, a};
    }

    Lift(b2World& world, bool mir, float spd = 2.8f) : mirror(mir), speed(spd) {
        loopLen = Len();
        const int n = (int)std::lround(loopLen / 3.5f);
        for (int i = 0; i < n; ++i) {
            float t = loopLen * i / n;
            Pose p = PathPose(t);
            b2BodyDef bd;
            bd.type = b2_kinematicBody;
            bd.position.Set(p.x, p.y);
            bd.angle = p.a;
            b2Body* b = world.CreateBody(&bd);
            b2PolygonShape box;
            box.SetAsBox(HALF_W, HALF_T);
            b2FixtureDef fd;
            fd.shape = &box;
            fd.friction = 0.35f;
            fd.filter.categoryBits = CAT_MACHINE;
            b->CreateFixture(&fd);
            paddles.push_back(b);
            s.push_back(t);
        }
    }

    void Update(float dt) {
        sprocketAngle += speed * dt / (R() + 0.35f);
        for (size_t i = 0; i < paddles.size(); ++i) {
            s[i] += speed * dt;
            Pose p = PathPose(s[i]);
            b2Body* b = paddles[i];
            b2Vec2 cur = b->GetPosition();
            b->SetLinearVelocity(b2Vec2((p.x - cur.x) / dt, (p.y - cur.y) / dt));
            float da = p.a - b->GetAngle();
            while (da >  PI) da -= 2.0f * PI;
            while (da < -PI) da += 2.0f * PI;
            b->SetAngularVelocity(da / dt);
        }
    }

    float X(float x) const { return mirror ? WORLD_W - x : x; }

    void DrawBack() const {
        // canal de montée et colonne de retour
        float cx0 = mirror ? WORLD_W - 7.2f : 4.2f;
        DrawRectangleRec({cx0 * DRAW_SCALE, 0.0f, 3.0f * DRAW_SCALE, 45.8f * DRAW_SCALE}, {16, 22, 31, 255});
        float rx0 = mirror ? WORLD_W - (XR + HALF_W + 0.2f) : (XR - HALF_W - 0.2f);
        DrawRectangleRec({rx0 * DRAW_SCALE, 0.0f, (2.0f * HALF_W + 0.4f) * DRAW_SCALE, WORLD_H * DRAW_SCALE}, {14, 19, 27, 255});
        // rails de la courroie
        Color rail = {46, 58, 80, 255};
        DrawLineEx(WS(X(XU), 0), WS(X(XU), 46.0f), 0.12f * DRAW_SCALE, rail);
        DrawLineEx(WS(X(XR), 0), WS(X(XR), 46.0f), 0.12f * DRAW_SCALE, rail);
        DrawRing(WS(X(XC()), YB), (R() - 0.06f) * DRAW_SCALE, (R() + 0.06f) * DRAW_SCALE, 0.0f, 180.0f, 48, rail);
        // pignon du bas
        Vector2 c = WS(X(XC()), YB);
        DrawCircleV(c, (R() + 0.6f) * DRAW_SCALE, {24, 32, 45, 255});
        for (int i = 0; i < 12; ++i) {
            float ang = sprocketAngle * (mirror ? -1.0f : 1.0f) + i * (2.0f * PI / 12.0f);
            Vector2 tp = {c.x + std::cos(ang) * (R() + 0.7f) * DRAW_SCALE, c.y + std::sin(ang) * (R() + 0.7f) * DRAW_SCALE};
            DrawCircleV(tp, 0.28f * DRAW_SCALE, {24, 32, 45, 255});
        }
        DrawCircleV(c, 0.7f * DRAW_SCALE, COL_WALL);
    }

    void DrawFront() const {
        for (const b2Body* b : paddles) {
            b2Vec2 p = b->GetPosition();
            Rectangle rec = {p.x * DRAW_SCALE, p.y * DRAW_SCALE, 2.0f * HALF_W * DRAW_SCALE, 2.0f * HALF_T * DRAW_SCALE};
            Vector2 origin = {rec.width * 0.5f, rec.height * 0.5f};
            DrawRectanglePro(rec, origin, b->GetAngle() * RAD2DEG, COL_PADDLE);
        }
    }
};

// Valves de régulation : engrenages engrenés (déphasage de 30 deg entre les deux).
struct PacingGear {
    b2Body* body = nullptr;
    float radius, hubRadius;
    int blades;
    float speed;

    PacingGear(b2World& world, b2Vec2 center, float r, float hubR, int n, float spd, float angle0)
        : radius(r), hubRadius(hubR), blades(n), speed(spd) {
        b2BodyDef bd;
        bd.type = b2_kinematicBody;
        bd.position = center;
        bd.angle = angle0;
        body = world.CreateBody(&bd);

        b2CircleShape hub;
        hub.m_radius = hubRadius;
        b2FixtureDef fdHub;
        fdHub.shape = &hub;
        fdHub.friction = 0.4f;
        fdHub.filter.categoryBits = CAT_MACHINE;
        body->CreateFixture(&fdHub);

        for (int i = 0; i < blades; ++i) {
            b2PolygonShape blade;
            float angle = (i * 360.0f / blades) * DEG2RAD;
            float hx = (radius - hubRadius) * 0.5f;
            float hy = 0.3f;
            float dist = hubRadius + hx;
            blade.SetAsBox(hx, hy, b2Vec2(std::cos(angle) * dist, std::sin(angle) * dist), angle);
            b2FixtureDef fdBlade;
            fdBlade.shape = &blade;
            fdBlade.friction = 0.4f;
            fdBlade.filter.categoryBits = CAT_MACHINE;
            body->CreateFixture(&fdBlade);
        }
        body->SetAngularVelocity(speed);
    }
    void Update() { body->SetAngularVelocity(speed); }

    void Draw() const {
        b2Vec2 pos = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(pos.x * DRAW_SCALE, pos.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);
        DrawCircle(0, 0, hubRadius * DRAW_SCALE, COL_MACHINE);
        for (int i = 0; i < blades; ++i) {
            rlPushMatrix();
            rlRotatef(i * (360.0f / blades), 0.0f, 0.0f, 1.0f);
            Rectangle rec = {hubRadius * DRAW_SCALE, -0.3f * DRAW_SCALE, (radius - hubRadius) * DRAW_SCALE, 0.6f * DRAW_SCALE};
            DrawRectangleRec(rec, COL_MACHINE);
            rlPopMatrix();
        }
        DrawCircle(0, 0, 0.8f * DRAW_SCALE, COL_WALL);
        DrawCircle(0, 0, 0.3f * DRAW_SCALE, COL_BG);
        rlPopMatrix();
    }
};

// Moulins broyeurs
struct CrossSpinner {
    b2Body* body = nullptr;
    float size, speed;

    CrossSpinner(b2World& world, b2Vec2 center, float s, float spd) : size(s), speed(spd) {
        b2BodyDef bd; bd.type = b2_kinematicBody; bd.position = center;
        body = world.CreateBody(&bd);
        b2PolygonShape box1, box2;
        box1.SetAsBox(size * 0.5f, 0.3f);
        box2.SetAsBox(0.3f, size * 0.5f);
        b2FixtureDef fd; fd.friction = 0.5f; fd.filter.categoryBits = CAT_MACHINE;
        fd.shape = &box1; body->CreateFixture(&fd);
        fd.shape = &box2; body->CreateFixture(&fd);
        body->SetAngularVelocity(speed);
    }
    void Update() { body->SetAngularVelocity(speed); }
    void Draw() const {
        b2Vec2 pos = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(pos.x * DRAW_SCALE, pos.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);
        Rectangle r1 = {-size * 0.5f * DRAW_SCALE, -0.3f * DRAW_SCALE, size * DRAW_SCALE, 0.6f * DRAW_SCALE};
        Rectangle r2 = {-0.3f * DRAW_SCALE, -size * 0.5f * DRAW_SCALE, 0.6f * DRAW_SCALE, size * DRAW_SCALE};
        DrawRectangleRec(r1, COL_MACHINE);
        DrawRectangleRec(r2, COL_MACHINE);
        DrawCircle(0, 0, 0.6f * DRAW_SCALE, COL_WALL);
        rlPopMatrix();
    }
};

// Engrenages décoratifs (sans collision) pour habiller le fond.
struct DecoGear {
    Vector2 c; float r; int teeth; float speed; float angle;
    void Update(float dt) { angle += speed * dt; }
    void Draw() const {
        Vector2 p = WS(c.x, c.y);
        DrawCircleV(p, r * DRAW_SCALE, COL_DECO);
        for (int i = 0; i < teeth; ++i) {
            float a = angle + i * (2.0f * PI / teeth);
            Rectangle rec = {p.x + std::cos(a) * r * DRAW_SCALE, p.y + std::sin(a) * r * DRAW_SCALE,
                             0.55f * DRAW_SCALE, 0.7f * DRAW_SCALE};
            DrawRectanglePro(rec, {rec.width * 0.5f, rec.height * 0.5f}, a * RAD2DEG, COL_DECO);
        }
        DrawCircleV(p, r * 0.55f * DRAW_SCALE, COL_BG);
        DrawCircleV(p, r * 0.18f * DRAW_SCALE, COL_DECO);
    }
};

// ---------------- BILLES ----------------

struct Ball {
    b2Body* body = nullptr;
    int typeIndex = 0;
    float radius;

    Ball(b2World& world, float x, float y, int type) : typeIndex(type) {
        radius = BALL_RADIUS;
        b2BodyDef bd; bd.type = b2_dynamicBody; bd.position.Set(x, y);
        bd.linearDamping = 0.01f; bd.angularDamping = 0.05f;
        body = world.CreateBody(&bd);
        b2CircleShape shape; shape.m_radius = radius;
        b2FixtureDef fd; fd.shape = &shape; fd.density = 1.0f;
        fd.friction = 0.1f; fd.restitution = 0.15f;
        fd.filter.categoryBits = CAT_BALL;
        body->CreateFixture(&fd);
    }
    void Draw() const {
        const Vector2 p = WorldToScreen(body->GetPosition());
        if (p.y < -2.0f * DRAW_SCALE || p.y > (WORLD_H + 2.0f) * DRAW_SCALE) return;
        const float r = radius * DRAW_SCALE;
        Color c = COLORS[typeIndex % 6];
        DrawCircleV(p, r * 1.6f, ColorAlpha(c, 0.15f));
        DrawCircleV(p, r * 1.2f, ColorAlpha(c, 0.4f));
        DrawCircleV(p, r, c);
        DrawCircleV({p.x - r * 0.3f, p.y - r * 0.3f}, r * 0.3f, {255, 255, 255, 200});
    }
};

// ---------------- LA MACHINE ----------------

// Géométrie clé (en mètres, côté gauche ; le côté droit est le miroir x -> 64 - x)
static constexpr float GEAR_DX = 3.2f, GEAR_CY = 18.8f, GEAR_R = 3.6f, GEAR_HUB = 2.0f;
static constexpr float GEAR_W  = 0.8f;

struct Machine {
    b2World world;
    std::vector<BoxWall> walls;
    std::vector<Peg> pegs;
    std::vector<Ball> balls;
    std::vector<DecoGear> deco;
    Lift liftL, liftR;
    PacingGear gearL, gearR;
    CrossSpinner spinL, spinR;
    int frame = 0;
    int leaks = 0;

    Machine()
        : world(b2Vec2(0.0f, GRAVITY)),
          liftL(world, false), liftR(world, true),
          gearL(world, b2Vec2(CX - GEAR_DX, GEAR_CY), GEAR_R, GEAR_HUB, 6,  GEAR_W, 0.0f),
          gearR(world, b2Vec2(CX + GEAR_DX, GEAR_CY), GEAR_R, GEAR_HUB, 6, -GEAR_W, PI / 6.0f),
          spinL(world, b2Vec2(CX - 8.8f, 31.8f), 5.2f, -2.4f),
          spinR(world, b2Vec2(CX + 8.8f, 31.8f), 5.2f, 2.4f)
    {
        world.SetContinuousPhysics(true);
        Build();
    }

    static std::vector<b2Vec2> ArcPts(b2Vec2 c, float r, float deg0, float deg1, int n, bool skipFirst) {
        std::vector<b2Vec2> v;
        for (int i = skipFirst ? 1 : 0; i <= n; ++i) {
            float a = (deg0 + (deg1 - deg0) * i / n) * DEG2RAD;
            v.push_back(b2Vec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r));
        }
        return v;
    }

    void Polyline(const std::vector<b2Vec2>& pts, float th) {
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            walls.emplace_back(world, pts[i], pts[i + 1], th, COL_WALL);
    }
    // Construit la polyligne côté gauche ET son miroir côté droit.
    void Both(std::vector<b2Vec2> pts, float th) {
        Polyline(pts, th);
        for (auto& p : pts) p.x = WORLD_W - p.x;
        Polyline(pts, th);
    }

    void Build() {
        // --- Ascenseurs : canal (mur extérieur + sol), mur intérieur, goulotte de déversement
        Both({{4.0f, -1.0f}, {4.0f, 46.0f}, {7.4f, 46.0f}}, 0.4f);
        Both({{7.4f, 4.0f}, {7.4f, 41.4f}}, 0.4f);
        Both({{7.3f, 4.0f}, {CX - 23.0f, 7.5f}}, 0.5f);

        // --- Trémie du réservoir (visible) + entonnoir qui épouse les valves
        {
            std::vector<b2Vec2> f = {{CX - 23.0f, 7.5f}, {CX - 23.0f, 9.0f}, {CX - 8.0f, 14.6f},
                                     {CX - 7.147f, 17.364f}};
            auto arc = ArcPts(b2Vec2(CX - GEAR_DX, GEAR_CY), 4.2f, 200.0f, 105.0f, 16, true);
            f.insert(f.end(), arc.begin(), arc.end());
            Both(f, 0.4f);
        }

        // --- Séparateur central (pyramide)
        Both({{CX, 24.6f}, {CX - 7.2f, 28.6f}}, 0.4f);

        // --- Champ de Plinko étendu
        for (int row = 0; row < 5; ++row) {
            float off = (row % 2) * 0.8f;
            for (float x = 10.0f + off; x < WORLD_W - 9.5f; x += 1.6f)
                pegs.emplace_back(world, x, 35.6f + row * 1.2f);
        }

        // --- Bassin de retour en V inversé : pente vers les deux ascenseurs
        Both({{CX, 42.6f}, {7.4f, 46.0f}}, 0.5f);

        // --- Déco de fond (engrenages engrenés, sans collision)
        deco.push_back({{16.0f, 18.0f}, 6.0f, 20,  0.18f, 0.0f});
        deco.push_back({{22.66f, 12.41f}, 3.0f, 10, -0.36f, 0.0f});
        deco.push_back({{16.0f, 26.7f}, 3.0f, 10, -0.36f, 0.0f});
        deco.push_back({{WORLD_W - 16.0f, 18.0f}, 6.0f, 20, -0.18f, 0.0f});
        deco.push_back({{WORLD_W - 22.66f, 12.41f}, 3.0f, 10, 0.36f, 0.0f});
        deco.push_back({{WORLD_W - 16.0f, 26.7f}, 3.0f, 10, 0.36f, 0.0f});

        // --- Les 1200 billes sont déjà dans la trémie dès la frame 0 : elles s'écoulent
        //     par les valves dès la première seconde. Aucune apparition magique.
        balls.reserve(TOTAL_BALLS);
        std::mt19937 rng(12345);
        std::uniform_int_distribution<int> colorDist(0, 5);
        std::uniform_real_distribution<float> jitter(-0.01f, 0.01f);
        int placed = 0;
        const int HOPPER_BALLS =1500;
        for (int row = 0; placed < HOPPER_BALLS && row < 200; ++row) {
            float y = 13.0f - row * 0.5f;
            float hw = (y >= 9.0f) ? 20.0f - 12.0f * (y - 9.0f) / 5.6f : 22.6f;
            hw -= 0.45f;
            for (float x = CX - hw; x <= CX + hw && placed < HOPPER_BALLS; x += 0.5f, ++placed)
                balls.emplace_back(world, x + jitter(rng), y, colorDist(rng));
        }
        // Le reste des billes est déjà posé dans le bassin en V (aussi présentes dès la frame 0).
        for (int layer = 0; placed < TOTAL_BALLS && layer < 12; ++layer) {
            for (float x = 9.5f; x < CX - 1.0f && placed < TOTAL_BALLS; x += 0.5f) {
                float yv = 46.0f - 3.4f * (x - 7.4f) / (CX - 7.4f);
                float y = yv - 0.75f - layer * 0.5f;
                balls.emplace_back(world, x + jitter(rng), y, colorDist(rng)); ++placed;
                if (placed < TOTAL_BALLS) { balls.emplace_back(world, WORLD_W - x + jitter(rng), y, colorDist(rng)); ++placed; }
            }
        }
    }

    // Filet de sécurité : si une bille s'échappait (ce qui ne doit pas arriver),
    // elle est remise au sommet du réservoir. Le compteur `leaks` doit rester à 0.
    void Rescue() {
        for (auto& b : balls) {
            b2Vec2 p = b.body->GetPosition();
            if (p.y > WORLD_H + 1.0f || p.x < -1.0f || p.x > WORLD_W + 1.0f || p.y < -5.0f) {
                b.body->SetTransform(b2Vec2(CX, 1.0f), 0.0f);
                b.body->SetLinearVelocity(b2Vec2(0.0f, 0.0f));
                ++leaks;
            }
        }
    }

    void Step() {
        liftL.Update(FIXED_DT);
        liftR.Update(FIXED_DT);
        gearL.Update(); gearR.Update();
        spinL.Update(); spinR.Update();
        for (auto& d : deco) d.Update(FIXED_DT);
        world.Step(FIXED_DT, 12, 8);
        if (frame % 30 == 0) Rescue();
        ++frame;
    }
};

// ---------------- DESSIN SCÈNE ----------------

static void DrawScene(const Machine& m) {
    ClearBackground(COL_BG);

    for (int i = 0; i < (int)WORLD_W; i += 4) DrawLine((int)(i * DRAW_SCALE), 0, (int)(i * DRAW_SCALE), OUTPUT_HEIGHT, {25, 30, 40, 60});
    for (int i = 0; i < (int)WORLD_H; i += 4) DrawLine(0, (int)(i * DRAW_SCALE), OUTPUT_WIDTH, (int)(i * DRAW_SCALE), {25, 30, 40, 60});

    for (const auto& d : m.deco) d.Draw();
    m.liftL.DrawBack();
    m.liftR.DrawBack();

    for (const auto& ball : m.balls) ball.Draw();

    m.liftL.DrawFront();
    m.liftR.DrawFront();
    m.gearL.Draw(); m.gearR.Draw();
    m.spinL.Draw(); m.spinR.Draw();

    for (const auto& peg : m.pegs) peg.Draw();
    for (const auto& wall : m.walls) wall.Draw();

    //DrawText("THE COLOSSAL COMPLEX", 340, 60, 52, {180, 185, 190, 255});
   // DrawText("PHASE 4 : PERPETUAL CASCADES", 340, 120, 28, {100, 105, 115, 255});
}

// ---------------- PROGRAMME PRINCIPAL ----------------

int main() {
    SetConfigFlags(USE_MSAA ? FLAG_MSAA_4X_HINT : 0);
    InitWindow(1280, 720, "Colossal Complex - 4K");
    SetTargetFPS(60);

    Machine machine;

    RenderTexture2D target = LoadRenderTexture(OUTPUT_WIDTH, OUTPUT_HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    FILE* ffmpeg = nullptr;
    if (EXPORT_VIDEO) {
        const std::string cmd =
            "ffmpeg -y -hide_banner -loglevel warning "
            "-f rawvideo -pixel_format rgba "
            "-video_size 3840x2160 -framerate 60 -i - "
            "-an -c:v libx264 -preset slow -crf 16 "
            "-pix_fmt yuv420p -movflags +faststart video_4k.mp4";
        ffmpeg = PIPE_OPEN(cmd.c_str(), "wb");
    }

    int frame = 0;
    bool exportFailed = EXPORT_VIDEO && ffmpeg == nullptr;

    while (!WindowShouldClose() && frame < MAX_FRAMES) {
        machine.Step();

        BeginTextureMode(target);
        DrawScene(machine);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture,
            {0.0f, 0.0f, (float)OUTPUT_WIDTH, -(float)OUTPUT_HEIGHT},
            {0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight()},
            {0.0f, 0.0f}, 0.0f, WHITE);
        DrawFPS(10, 10);
        EndDrawing();

        if (ffmpeg) {
            Image image = LoadImageFromTexture(target.texture);
            if (image.data) {
                ImageFlipVertical(&image);
                const size_t bytes = (size_t)OUTPUT_WIDTH * OUTPUT_HEIGHT * 4;
                const size_t written = fwrite(image.data, 1, bytes, ffmpeg);
                if (written != bytes) exportFailed = true;
                UnloadImage(image);
            } else {
                exportFailed = true;
            }
        }

        ++frame;
        if (exportFailed) break;
    }

    if (machine.leaks > 0) std::printf("ATTENTION : %d billes remises dans le reservoir par le filet de securite\n", machine.leaks);

    if (ffmpeg) PIPE_CLOSE(ffmpeg);
    UnloadRenderTexture(target);
    CloseWindow();

    return exportFailed ? 2 : 0;
}