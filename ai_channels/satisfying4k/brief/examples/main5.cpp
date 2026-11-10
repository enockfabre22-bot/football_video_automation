// THE GREAT GRAIN MACHINE - raylib + Box2D, export natif 4K, format YouTube long.
//
// Une seule boucle fermée, entièrement visible à l'écran :
//   trémie -> tamis d'engrenages -> goulot en sablier -> piston à vilebrequin
//   -> tube en S -> grand tambour à pales -> tube de sortie -> ascenseur à courroie
//   -> longue goulotte -> retour dans la trémie.
// Les grains sont déjà dans la trémie à la frame 0 (aucune apparition magique).
// Leur teinte avance à chaque passage dans le goulot : les couleurs évoluent en continu.

#include <raylib.h>
#include <rlgl.h>
#include <Box2D/Box2D.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static constexpr float PIXELS_PER_METER = 60.0f;          // 64 m x 36 m à l'écran
static constexpr float DRAW_SCALE = PIXELS_PER_METER;
static constexpr float WORLD_W = OUTPUT_WIDTH  / PIXELS_PER_METER;
static constexpr float WORLD_H = OUTPUT_HEIGHT / PIXELS_PER_METER;

static constexpr int   TOTAL_GRAINS = 2800;
static constexpr float GRAIN_R = 0.08f;
static constexpr int   MAX_FRAMES = 18000;                // 5 min à 60 fps (108000 = 30 min)

static constexpr bool EXPORT_VIDEO = true;              // true pour le rendu final
static constexpr bool USE_MSAA = true;

static constexpr float GRAVITY  = 9.81f;
static constexpr float FIXED_DT = 1.0f / 60.0f;
static constexpr int   SUBSTEPS = 2;                      // physique à 120 Hz

enum CollisionCategory { CAT_WALL = 0x0001, CAT_MACHINE = 0x0002, CAT_GRAIN = 0x0004 };

// Palette
static const Color COL_BG      = {24, 27, 34, 255};
static const Color COL_WALL    = {112, 120, 134, 255};
static const Color COL_MACHINE = {70, 77, 90, 255};
static const Color COL_MACHINE_LIGHT = {150, 156, 168, 255};
static const Color COL_TUBE_BG = {18, 20, 26, 255};

static Vector2 WS(float x, float y) { return {x * DRAW_SCALE, y * DRAW_SCALE}; }
static Vector2 WorldToScreen(const b2Vec2& p) { return {p.x * DRAW_SCALE, p.y * DRAW_SCALE}; }

// ---------------- GEOMETRIE ----------------

struct BoxWall {
    b2Body* body = nullptr;
    b2Vec2 p1, p2;
    float thickness;
    Color color;

    BoxWall(b2World& world, b2Vec2 start, b2Vec2 end, float thick, Color col) {
        p1 = start; p2 = end; thickness = thick; color = col;
        b2BodyDef bd;
        bd.position = 0.5f * (start + end);
        bd.angle = std::atan2(end.y - start.y, end.x - start.x);
        body = world.CreateBody(&bd);
        b2PolygonShape box;
        box.SetAsBox(b2Distance(start, end) * 0.5f, thick * 0.5f);
        b2FixtureDef fd;
        fd.shape = &box;
        fd.friction = 0.04f;
        fd.filter.categoryBits = CAT_WALL;
        body->CreateFixture(&fd);
    }
    void Draw() const {
        DrawLineEx(WS(p1.x, p1.y), WS(p2.x, p2.y), thickness * DRAW_SCALE, color);
        DrawCircleV(WS(p1.x, p1.y), thickness * DRAW_SCALE * 0.5f, color);
        DrawCircleV(WS(p2.x, p2.y), thickness * DRAW_SCALE * 0.5f, color);
    }
};

static std::vector<b2Vec2> Chaikin(std::vector<b2Vec2> p, int iters) {
    for (; iters > 0; --iters) {
        std::vector<b2Vec2> q;
        q.push_back(p.front());
        for (size_t i = 0; i + 1 < p.size(); ++i) {
            q.push_back(0.75f * p[i] + 0.25f * p[i + 1]);
            q.push_back(0.25f * p[i] + 0.75f * p[i + 1]);
        }
        q.push_back(p.back());
        p = q;
    }
    return p;
}

static std::vector<b2Vec2> ArcPts(b2Vec2 c, float r, float deg0, float deg1, int n) {
    std::vector<b2Vec2> v;
    for (int i = 0; i <= n; ++i) {
        float a = (deg0 + (deg1 - deg0) * i / n) * DEG2RAD;
        v.push_back(b2Vec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r));
    }
    return v;
}

// ---------------- MÉCANISMES ----------------

// Ascenseur à courroie : plateaux cinématiques sur une boucle fermée continue.
struct Lift {
    static constexpr float XU = 4.1f;       // axe de la montée
    static constexpr float XR = 1.2f;       // axe de la descente
    static constexpr float YB = 33.0f;      // bas du trajet
    static constexpr float YT = 2.0f;       // haut du trajet
    static constexpr float HALF_W = 1.27f;
    static constexpr float HALF_T = 0.1f;
    static constexpr float TILT = 0.5f;     // inclinaison de déversement (rad)

    float speed;
    float sprocketAngle = 0.0f;
    std::vector<b2Body*> paddles;
    std::vector<float> s;
    struct Pose { float x, y, a; };

    static float R()   { return (XU - XR) * 0.5f; }
    static float XC()  { return (XU + XR) * 0.5f; }
    static float H()   { return YB - YT; }
    static float Len() { return 2.0f * H() + 2.0f * PI * R(); }

    static Pose PathPose(float t) {
        const float r = R(), xc = XC(), h = H(), arc = PI * r, L = Len();
        t = std::fmod(t, L); if (t < 0) t += L;
        float x, y, a = 0.0f;
        if (t < h) {
            x = XU; y = YB - t;
            float k = std::clamp((11.0f - y) / 4.0f, 0.0f, 1.0f);
            a = TILT * k * k * (3.0f - 2.0f * k);
        } else if (t < h + arc) {
            float ph = (t - h) / r;
            x = xc + r * std::cos(ph); y = YT - r * std::sin(ph);
            a = TILT * (1.0f - ph / PI);
        } else if (t < 2.0f * h + arc) {
            x = XR; y = YT + (t - h - arc);
        } else {
            float ph = (t - 2.0f * h - arc) / r;
            x = xc - r * std::cos(ph); y = YB + r * std::sin(ph);
        }
        return {x, y, a};
    }

    Lift(b2World& world, float spd = 2.0f) : speed(spd) {
        const float L = Len();
        const int n = (int)std::lround(L / 2.5f);
        for (int i = 0; i < n; ++i) {
            float t = L * i / n;
            Pose p = PathPose(t);
            b2BodyDef bd; bd.type = b2_kinematicBody; bd.position.Set(p.x, p.y); bd.angle = p.a;
            b2Body* b = world.CreateBody(&bd);
            b2PolygonShape box; box.SetAsBox(HALF_W, HALF_T);
            b2FixtureDef fd; fd.shape = &box; fd.friction = 0.4f; fd.filter.categoryBits = CAT_MACHINE;
            b->CreateFixture(&fd);
            paddles.push_back(b); s.push_back(t);
        }
    }

    void Update(float dt) {
        sprocketAngle += speed * dt / (R() + 0.2f);
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

    void DrawBack() const {
        DrawRectangleRec({2.8f * DRAW_SCALE, 0.0f, 2.6f * DRAW_SCALE, 33.5f * DRAW_SCALE}, COL_TUBE_BG);
        DrawRectangleRec({-0.2f * DRAW_SCALE, 0.0f, 2.7f * DRAW_SCALE, WORLD_H * DRAW_SCALE}, {20, 23, 29, 255});
        Color rail = {52, 58, 70, 255};
        DrawLineEx(WS(XU, YT), WS(XU, YB), 0.1f * DRAW_SCALE, rail);
        DrawLineEx(WS(XR, YT), WS(XR, YB), 0.1f * DRAW_SCALE, rail);
        DrawRing(WS(XC(), YB), (R() - 0.05f) * DRAW_SCALE, (R() + 0.05f) * DRAW_SCALE, 0.0f, 180.0f, 32, rail);
        DrawRing(WS(XC(), YT), (R() - 0.05f) * DRAW_SCALE, (R() + 0.05f) * DRAW_SCALE, 180.0f, 360.0f, 32, rail);
        for (int k = 0; k < 2; ++k) {
            Vector2 c = WS(XC(), k == 0 ? YB : YT);
            DrawCircleV(c, (R() - 0.1f) * DRAW_SCALE, COL_MACHINE);
            for (int i = 0; i < 8; ++i) {
                float ang = sprocketAngle + i * (2.0f * PI / 8.0f);
                DrawCircleV({c.x + std::cos(ang) * (R() - 0.1f) * DRAW_SCALE, c.y + std::sin(ang) * (R() - 0.1f) * DRAW_SCALE},
                            0.12f * DRAW_SCALE, COL_MACHINE_LIGHT);
            }
            DrawCircleV(c, 0.3f * DRAW_SCALE, COL_BG);
        }
    }
    void DrawFront() const {
        for (const b2Body* b : paddles) {
            b2Vec2 p = b->GetPosition();
            Rectangle rec = {p.x * DRAW_SCALE, p.y * DRAW_SCALE, 2.0f * HALF_W * DRAW_SCALE, 2.0f * HALF_T * DRAW_SCALE};
            DrawRectanglePro(rec, {rec.width * 0.5f, rec.height * 0.5f}, b->GetAngle() * RAD2DEG, COL_MACHINE_LIGHT);
        }
    }
};

// Petit engrenage du tamis (3 pales).
struct SieveGear {
    b2Body* body = nullptr;
    float radius, speed;
    SieveGear(b2World& world, b2Vec2 c, float r, float spd, float a0) : radius(r), speed(spd) {
        b2BodyDef bd; bd.type = b2_kinematicBody; bd.position = c; bd.angle = a0;
        body = world.CreateBody(&bd);
        b2CircleShape hub; hub.m_radius = 0.22f;
        b2FixtureDef fh; fh.shape = &hub; fh.friction = 0.3f; fh.filter.categoryBits = CAT_MACHINE;
        body->CreateFixture(&fh);
        for (int i = 0; i < 3; ++i) {
            float ang = i * 2.0f * PI / 3.0f;
            float hx = (radius - 0.2f) * 0.5f;
            b2PolygonShape blade;
            blade.SetAsBox(hx, 0.07f, b2Vec2(std::cos(ang) * (0.2f + hx), std::sin(ang) * (0.2f + hx)), ang);
            b2FixtureDef fb; fb.shape = &blade; fb.friction = 0.3f; fb.filter.categoryBits = CAT_MACHINE;
            body->CreateFixture(&fb);
        }
        body->SetAngularVelocity(speed);
    }
    void Draw() const {
        b2Vec2 p = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(p.x * DRAW_SCALE, p.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) {
            rlPushMatrix();
            rlRotatef(i * 120.0f, 0.0f, 0.0f, 1.0f);
            DrawRectangleRec({0.0f, -0.07f * DRAW_SCALE, radius * DRAW_SCALE, 0.14f * DRAW_SCALE}, COL_MACHINE_LIGHT);
            rlPopMatrix();
        }
        DrawCircle(0, 0, 0.26f * DRAW_SCALE, COL_MACHINE_LIGHT);
        DrawCircle(0, 0, 0.09f * DRAW_SCALE, COL_BG);
        rlPopMatrix();
    }
};

// Grand tambour à 3 pales.
struct Drum {
    b2Body* body = nullptr;
    float radius, hub, speed;
    Drum(b2World& world, b2Vec2 c, float r, float hubR, float spd) : radius(r), hub(hubR), speed(spd) {
        b2BodyDef bd; bd.type = b2_kinematicBody; bd.position = c;
        body = world.CreateBody(&bd);
        b2CircleShape h; h.m_radius = hub;
        b2FixtureDef fh; fh.shape = &h; fh.friction = 0.3f; fh.filter.categoryBits = CAT_MACHINE;
        body->CreateFixture(&fh);
        for (int i = 0; i < 3; ++i) {
            float ang = i * 2.0f * PI / 3.0f + 0.5f;
            float hx = (radius - hub) * 0.5f;
            b2PolygonShape blade;
            blade.SetAsBox(hx, 0.15f, b2Vec2(std::cos(ang) * (hub + hx), std::sin(ang) * (hub + hx)), ang);
            b2FixtureDef fb; fb.shape = &blade; fb.friction = 0.3f; fb.filter.categoryBits = CAT_MACHINE;
            body->CreateFixture(&fb);
        }
        body->SetAngularVelocity(speed);
    }
    void Draw() const {
        b2Vec2 p = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(p.x * DRAW_SCALE, p.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) {
            rlPushMatrix();
            rlRotatef(i * 120.0f + 0.5f * RAD2DEG, 0.0f, 0.0f, 1.0f);
            DrawRectangleRec({hub * DRAW_SCALE, -0.15f * DRAW_SCALE, (radius - hub) * DRAW_SCALE, 0.3f * DRAW_SCALE}, COL_MACHINE_LIGHT);
            rlPopMatrix();
        }
        DrawCircle(0, 0, hub * DRAW_SCALE, COL_MACHINE);
        DrawCircle(0, 0, hub * 0.55f * DRAW_SCALE, COL_MACHINE_LIGHT);
        DrawCircle(0, 0, hub * 0.2f * DRAW_SCALE, COL_BG);
        rlPopMatrix();
    }
};

// Piston entraîné par un vilebrequin (cinématique réelle bielle-manivelle).
struct Piston {
    static constexpr float CRANK_X = 61.4f, CRANK_Y = 25.55f;
    static constexpr float CRANK_R = 1.7f, ROD_L = 6.2f;
    static constexpr float HALF_LEN = 2.2f, HALF_H = 1.44f;
    b2Body* body = nullptr;
    float theta = 3.14159f;
    float omega = 2.0f;

    static float RearX(float th) {
        float py = CRANK_R * std::sin(th);
        return CRANK_X + CRANK_R * std::cos(th) - std::sqrt(ROD_L * ROD_L - py * py);
    }

    Piston(b2World& world) {
        b2BodyDef bd; bd.type = b2_kinematicBody;
        bd.position.Set(RearX(theta) - HALF_LEN, CRANK_Y);
        body = world.CreateBody(&bd);
        b2PolygonShape box; box.SetAsBox(HALF_LEN, HALF_H);
        b2FixtureDef fd; fd.shape = &box; fd.friction = 0.3f; fd.filter.categoryBits = CAT_MACHINE;
        body->CreateFixture(&fd);
    }
    void Update(float dt) {
        theta += omega * dt;
        float tx = RearX(theta) - HALF_LEN;
        body->SetLinearVelocity(b2Vec2((tx - body->GetPosition().x) / dt, 0.0f));
    }
    void Draw() const {
        b2Vec2 p = body->GetPosition();
        Rectangle rec = {(p.x - HALF_LEN) * DRAW_SCALE, (p.y - HALF_H) * DRAW_SCALE, 2.0f * HALF_LEN * DRAW_SCALE, 2.0f * HALF_H * DRAW_SCALE};
        DrawRectangleRec(rec, COL_MACHINE);
        DrawRectangleLines((int)rec.x, (int)rec.y, (int)rec.width, (int)rec.height, COL_MACHINE_LIGHT);
        // bielle + volant + manivelle
        Vector2 pin = WS(CRANK_X + CRANK_R * std::cos(theta), CRANK_Y + CRANK_R * std::sin(theta));
        Vector2 rear = WS(p.x + HALF_LEN, CRANK_Y);
        DrawLineEx(rear, pin, 0.22f * DRAW_SCALE, COL_MACHINE_LIGHT);
        Vector2 c = WS(CRANK_X, CRANK_Y);
        DrawCircleV(c, 2.4f * DRAW_SCALE, COL_MACHINE);
        DrawCircleV(c, 2.1f * DRAW_SCALE, {40, 45, 55, 255});
        for (int i = 0; i < 4; ++i) {
            float a = theta + i * PI * 0.5f;
            DrawLineEx(c, {c.x + std::cos(a) * 2.1f * DRAW_SCALE, c.y + std::sin(a) * 2.1f * DRAW_SCALE}, 0.18f * DRAW_SCALE, COL_MACHINE);
        }
        DrawCircleV(c, 0.5f * DRAW_SCALE, COL_MACHINE_LIGHT);
        DrawCircleV(pin, 0.3f * DRAW_SCALE, COL_MACHINE_LIGHT);
        DrawCircleV(pin, 0.12f * DRAW_SCALE, COL_BG);
        DrawCircleV(rear, 0.16f * DRAW_SCALE, COL_BG);
    }
};

// ---------------- GRAINS ----------------

struct Grain {
    b2Body* body = nullptr;
    float hue = 0.0f;
    Color color{255, 255, 255, 255};

    Grain(b2World& world, float x, float y, float h) : hue(h) {
        b2BodyDef bd; bd.type = b2_dynamicBody; bd.position.Set(x, y);
        bd.linearDamping = 0.02f; bd.angularDamping = 0.1f;
        body = world.CreateBody(&bd);
        b2CircleShape shape; shape.m_radius = GRAIN_R;
        b2FixtureDef fd; fd.shape = &shape; fd.density = 1.0f;
        fd.friction = 0.08f; fd.restitution = 0.03f;
        fd.filter.categoryBits = CAT_GRAIN;
        body->CreateFixture(&fd);
        UpdateColor();
    }
    void UpdateColor() { color = ColorFromHSV(std::fmod(hue, 360.0f), 0.82f, 1.0f); }
    void Draw() const {
        const Vector2 p = WorldToScreen(body->GetPosition());
        DrawCircleV(p, GRAIN_R * 1.15f * DRAW_SCALE, color);
    }
};

// ---------------- LA MACHINE ----------------

struct Machine {
    b2World world;
    std::vector<BoxWall> walls;
    std::vector<Grain> grains;
    std::vector<SieveGear> sieve;
    Lift lift;
    Piston piston;
    Drum drum;
    std::vector<float> prevY;
    int frame = 0;
    int leaks = 0;
    long neckCount = 0;

    static constexpr float DRUM_CX = 21.0f, DRUM_CY = 26.6f, DRUM_RW = 8.8f;

    Machine()
        : world(b2Vec2(0.0f, GRAVITY)), lift(world), piston(world),
          drum(world, b2Vec2(DRUM_CX, DRUM_CY), 8.4f, 1.2f, 0.45f)
    {
        world.SetContinuousPhysics(true);
        Build();
    }

    void Polyline(const std::vector<b2Vec2>& pts, float th) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            if (b2Distance(pts[i], pts[i + 1]) < 0.02f) continue;
            walls.emplace_back(world, pts[i], pts[i + 1], th, COL_WALL);
        }
    }

    // Tube de demi-largeur hw autour d'une ligne centrale (plancher + plafond optionnel).
    void Tube(const std::vector<b2Vec2>& center, float hw, bool leftward, float th, bool ceiling,
              const b2Vec2* ceilStart = nullptr, const b2Vec2* floorStart = nullptr,
              const b2Vec2* ceilEnd = nullptr) {
        auto c = Chaikin(center, 2);
        std::vector<b2Vec2> fl, ce;
        for (size_t i = 0; i < c.size(); ++i) {
            b2Vec2 a = c[i == 0 ? 0 : i - 1], b = c[i + 1 < c.size() ? i + 1 : i];
            b2Vec2 t = b - a; t.Normalize();
            b2Vec2 n = leftward ? b2Vec2(t.y, -t.x) : b2Vec2(-t.y, t.x);
            fl.push_back(c[i] + hw * n);
            ce.push_back(c[i] - hw * n);
        }
        if (floorStart) fl.front() = *floorStart;
        if (ceilStart) ce.front() = *ceilStart;
        if (ceilEnd) ce.back() = *ceilEnd;
        Polyline(fl, th);
        if (ceiling) Polyline(ce, th);
    }

    void Build() {
        // --- Ascenseur : canal, sol, mur intérieur
        Polyline({{2.6f, 0.3f}, {2.6f, 33.7f}, {6.4f, 33.7f}}, 0.4f);
        Polyline({{5.6f, 4.0f}, {5.6f, 30.4f}}, 0.4f);

        // --- Longue goulotte de retour (plancher ouvert) jusqu'à la trémie
        Polyline({{5.6f, 4.2f}, {41.5f, 7.7f}}, 0.3f);

        // --- Trémie + tamis + entonnoir + goulot en sablier
        {
            std::vector<b2Vec2> L = {{40.0f, 8.4f}, {40.0f, 17.0f}, {48.95f, 20.6f}, {48.95f, 21.0f}};
            std::vector<b2Vec2> R = {{59.0f, 2.0f}, {59.0f, 17.0f}, {50.05f, 20.6f}, {50.05f, 21.0f}};
            for (int k = 1; k <= 10; ++k) {
                float t = k / 10.0f, bulge = 0.9f * std::sin(PI * t);
                L.push_back(b2Vec2(48.95f - bulge, 21.0f + 2.2f * t));
                R.push_back(b2Vec2(50.05f + bulge, 21.0f + 2.2f * t));
            }
            L.push_back(b2Vec2(48.95f, 23.9f));
            R.push_back(b2Vec2(50.05f, 23.9f));
            Polyline(L, 0.4f);
            Polyline(R, 0.4f);
        }
        // tamis : engrenages en quinconce
        for (int row = 0; row < 2; ++row) {
            int cols = row == 0 ? 9 : 8;
            for (int c = 0; c < cols; ++c) {
                float x = 40.9f + c * 2.15f + (row ? 1.075f : 0.0f);
                float y = 14.0f + row * 1.8f;
                float dir = ((c + row) % 2 == 0) ? 1.0f : -1.0f;
                sieve.emplace_back(world, b2Vec2(x, y), 0.9f, 1.3f * dir, 0.4f * c);
            }
        }

        // --- Chambre du piston : plafond (avec ouverture sous le goulot), plancher, fond
        Polyline({{50.05f, 23.9f}, {58.6f, 23.9f}}, 0.3f);
        Polyline({{58.6f, 23.9f}, {58.6f, 27.2f}}, 0.3f);
        Polyline({{58.6f, 27.2f}, {48.95f, 27.2f}}, 0.3f);

        // --- Tube en S : de la chambre jusqu'au tambour
        const float gapHalf = std::asin(1.5f / DRUM_RW) * RAD2DEG;
        {
            float aIn = 15.0f, a0 = aIn - gapHalf, a1 = aIn + gapHalf;   // entrée du tambour (bas-droite)
            b2Vec2 up = {DRUM_CX + DRUM_RW * std::cos(a0 * DEG2RAD), DRUM_CY + DRUM_RW * std::sin(a0 * DEG2RAD)};
            b2Vec2 lo = {DRUM_CX + DRUM_RW * std::cos(a1 * DEG2RAD), DRUM_CY + DRUM_RW * std::sin(a1 * DEG2RAD)};
            std::vector<b2Vec2> c = {{48.95f, 25.55f}, {45.0f, 26.2f}, {40.0f, 27.3f}, {35.0f, 28.2f}, {29.9f, 28.9f}};
            // plafond et plancher se terminent exactement sur le bord du tambour
            auto cs = Chaikin(c, 2);
            std::vector<b2Vec2> fl, ce;
            for (size_t i = 0; i < cs.size(); ++i) {
                b2Vec2 a = cs[i == 0 ? 0 : i - 1], b = cs[i + 1 < cs.size() ? i + 1 : i];
                b2Vec2 t = b - a; t.Normalize();
                b2Vec2 n(t.y, -t.x);
                fl.push_back(cs[i] + 1.65f * n);
                ce.push_back(cs[i] - 1.65f * n);
            }
            fl.back() = lo; ce.back() = up;
            Polyline(fl, 0.3f);
            Polyline(ce, 0.3f);

            // --- Tambour : deux arcs (entrée et sortie laissées ouvertes)
            float bIn = 165.0f, b0 = bIn - gapHalf, b1 = bIn + gapHalf;  // sortie (bas-gauche)
            Polyline(ArcPts(b2Vec2(DRUM_CX, DRUM_CY), DRUM_RW, a1, b0, 34), 0.4f);
            Polyline(ArcPts(b2Vec2(DRUM_CX, DRUM_CY), DRUM_RW, b1, a0 + 360.0f, 48), 0.4f);

            // --- Tube de sortie du tambour vers l'ascenseur
            b2Vec2 exUp = {DRUM_CX + DRUM_RW * std::cos(b1 * DEG2RAD), DRUM_CY + DRUM_RW * std::sin(b1 * DEG2RAD)};
            b2Vec2 exLo = {DRUM_CX + DRUM_RW * std::cos(b0 * DEG2RAD), DRUM_CY + DRUM_RW * std::sin(b0 * DEG2RAD)};
            b2Vec2 exEnd(5.6f, 30.4f);
            Tube({{12.5f, 28.9f}, {10.8f, 30.0f}, {8.6f, 31.3f}, {5.6f, 32.05f}}, 1.65f, true, 0.3f, true, &exUp, &exLo, &exEnd);
        }
    }

    // Les grains naissent dans la trémie, en contact, avec un dégradé de couleurs.
    void SpawnGrains() {
        grains.reserve(TOTAL_GRAINS);
        int placed = 0;
        for (int row = 0; placed < TOTAL_GRAINS && row < 100; ++row) {
            float y = 12.4f - row * 0.17f;
            for (float x = 40.5f; x <= 58.5f && placed < TOTAL_GRAINS; x += 0.17f, ++placed) {
                float hue = std::fmod(300.0f * (x - 40.5f) / 18.0f + 40.0f * (12.4f - y) / 3.8f, 360.0f);
                grains.emplace_back(world, x + 0.005f * std::sin(placed * 12.9898f), y, hue);
            }
        }
        prevY.assign(grains.size(), 0.0f);
        for (size_t i = 0; i < grains.size(); ++i) prevY[i] = grains[i].body->GetPosition().y;
    }

    void Rescue() {
        for (auto& g : grains) {
            b2Vec2 p = g.body->GetPosition();
            if (p.y > WORLD_H + 1.0f || p.x < -1.0f || p.x > WORLD_W + 1.0f || p.y < -5.0f) {
                g.body->SetTransform(b2Vec2(49.5f, 6.0f), 0.0f);
                g.body->SetLinearVelocity(b2Vec2(0.0f, 0.0f));
                ++leaks;
            }
        }
    }

    void Step() {
        const float dt = FIXED_DT / SUBSTEPS;
        for (int k = 0; k < SUBSTEPS; ++k) {
            lift.Update(dt);
            piston.Update(dt);
            world.Step(dt, 8, 3);
        }
        // teinte : +22 degrés à chaque passage dans le goulot
        for (size_t i = 0; i < grains.size(); ++i) {
            b2Vec2 p = grains[i].body->GetPosition();
            if (prevY[i] < 22.0f && p.y >= 22.0f && p.x > 47.5f && p.x < 51.5f) {
                grains[i].hue += 22.0f; grains[i].UpdateColor(); ++neckCount;
            }
            prevY[i] = p.y;
        }
        if (frame % 30 == 0) Rescue();
        ++frame;
    }
};

// ---------------- DESSIN ----------------

static void DrawScene(const Machine& m) {
    ClearBackground(COL_BG);
    for (int i = 0; i < (int)WORLD_W; i += 4) DrawLine((int)(i * DRAW_SCALE), 0, (int)(i * DRAW_SCALE), OUTPUT_HEIGHT, {30, 34, 42, 255});
    for (int i = 0; i < (int)WORLD_H; i += 4) DrawLine(0, (int)(i * DRAW_SCALE), OUTPUT_WIDTH, (int)(i * DRAW_SCALE), {30, 34, 42, 255});

    // fonds sombres des conduits
    DrawCircleV(WS(Machine::DRUM_CX, Machine::DRUM_CY), Machine::DRUM_RW * DRAW_SCALE, COL_TUBE_BG);
    DrawRectangleRec({40.0f * DRAW_SCALE, 2.0f * DRAW_SCALE, 19.0f * DRAW_SCALE, 15.0f * DRAW_SCALE}, COL_TUBE_BG);
    DrawRectangleRec({48.95f * DRAW_SCALE, 17.0f * DRAW_SCALE, 1.1f * DRAW_SCALE, 7.0f * DRAW_SCALE}, COL_TUBE_BG);
    DrawRectangleRec({48.5f * DRAW_SCALE, 23.9f * DRAW_SCALE, 10.1f * DRAW_SCALE, 3.3f * DRAW_SCALE}, COL_TUBE_BG);

    m.lift.DrawBack();
    for (const auto& g : m.grains) g.Draw();

    m.lift.DrawFront();
    m.drum.Draw();
    for (const auto& s : m.sieve) s.Draw();
    m.piston.Draw();
    for (const auto& w : m.walls) w.Draw();

    DrawText("THE GREAT GRAIN MACHINE", 200, 60, 56, {200, 205, 214, 255});
    DrawText("ONE LOOP. NO CUTS. NO END.", 200, 130, 28, {120, 126, 138, 255});
}

// ---------------- PROGRAMME PRINCIPAL ----------------

int main() {
    SetConfigFlags(USE_MSAA ? FLAG_MSAA_4X_HINT : 0);
    InitWindow(1280, 720, "Great Grain Machine - 4K");
    SetTargetFPS(60);

    Machine machine;
    machine.SpawnGrains();

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
                if (fwrite(image.data, 1, bytes, ffmpeg) != bytes) exportFailed = true;
                UnloadImage(image);
            } else {
                exportFailed = true;
            }
        }
        ++frame;
        if (exportFailed) break;
    }

    if (machine.leaks > 0) std::printf("ATTENTION : %d grains remis en jeu par le filet de securite\n", machine.leaks);

    if (ffmpeg) PIPE_CLOSE(ffmpeg);
    UnloadRenderTexture(target);
    CloseWindow();
    return exportFailed ? 2 : 0;
}