// THE RAINBOW CIRCUIT - raylib + Box2D, export natif 4K, format YouTube long.
//
// Boucle fermée, 100 % visible, style "simulation satisfaisante" :
//   grande cuve ronde (arc-en-ciel) -> 2 chambres à étoiles rotatives (sas doseurs)
//   -> tube sinueux -> grand anneau à bras rotatifs -> tube en cascade
//   -> ascenseur à godets -> longue goulotte -> retour en haut de la cuve.
//
// Les 1800 particules hexagonales sont déjà dans la cuve à la frame 0 (aucun "pop").
// Au sommet de l'ascenseur, une "porte de peinture" donne à chaque particule la couleur
// du moment : la cuve se remplit de strates colorées qui se vident ensuite par le bas.

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

static constexpr int   TARGET_PARTICLES = 3500;
static constexpr float P_R = 0.11f;                        // rayon d'une particule
static constexpr int   MAX_FRAMES = 7200;                 // 5 min à 60 fps (108000 = 30 min)

static constexpr bool EXPORT_VIDEO = true;                // true pour le rendu final
static constexpr bool USE_MSAA = true;

static constexpr float GRAVITY  = 9.81f;
static constexpr float FIXED_DT = 1.0f / 60.0f;
static constexpr int   SUBSTEPS = 2;                       // physique à 120 Hz
static constexpr float HUE_SPEED = 20.0f;                  // degrés/s de la porte de peinture

enum CollisionCategory { CAT_WALL = 0x0001, CAT_MACHINE = 0x0002, CAT_PARTICLE = 0x0004 };

// Palette : fond gris, formes sombres, pièces mobiles claires
static const Color COL_BG    = {104, 106, 110, 255};
static const Color COL_DARK  = {42, 42, 45, 255};
static const Color COL_LIGHT = {236, 236, 238, 255};
static const Color COL_ORANGE = {240, 160, 50, 255};

static Vector2 WS(float x, float y) { return {x * DRAW_SCALE, y * DRAW_SCALE}; }

// ---------------- GEOMETRIE ----------------

struct BoxWall {
    b2Body* body = nullptr;
    b2Vec2 p1, p2;
    float thickness;

    BoxWall(b2World& world, b2Vec2 start, b2Vec2 end, float thick) {
        p1 = start; p2 = end; thickness = thick;
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
        DrawLineEx(WS(p1.x, p1.y), WS(p2.x, p2.y), thickness * DRAW_SCALE, COL_DARK);
        DrawCircleV(WS(p1.x, p1.y), thickness * DRAW_SCALE * 0.5f, COL_DARK);
        DrawCircleV(WS(p2.x, p2.y), thickness * DRAW_SCALE * 0.5f, COL_DARK);
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

static b2Vec2 OnCircle(b2Vec2 c, float r, float deg) {
    return b2Vec2(c.x + r * std::cos(deg * DEG2RAD), c.y + r * std::sin(deg * DEG2RAD));
}

// ---------------- MÉCANISMES ----------------

// Ascenseur à godets (miroir : monte sur le bord droit de l'écran).
struct Lift {
    static constexpr float XU = 4.1f, XR = 1.2f, YB = 33.0f, YT = 2.0f;
    static constexpr float HALF_W = 1.27f, HALF_T = 0.1f, TILT = 0.5f;

    float speed;
    float sprocketAngle = 0.0f;
    std::vector<b2Body*> paddles;
    std::vector<float> s;
    struct Pose { float x, y, a; };

    static float R()   { return (XU - XR) * 0.5f; }
    static float XC()  { return (XU + XR) * 0.5f; }
    static float H()   { return YB - YT; }
    static float Len() { return 2.0f * H() + 2.0f * PI * R(); }
    static float MX(float x) { return WORLD_W - x; }

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
        return {MX(x), y, -a};      // miroir horizontal
    }

    Lift(b2World& world, float spd = 2.8f) : speed(spd) {
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
        sprocketAngle -= speed * dt / (R() + 0.2f);
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
        DrawRectangleRec({MX(5.4f) * DRAW_SCALE, 0.0f, 2.6f * DRAW_SCALE, 33.5f * DRAW_SCALE}, {30, 30, 33, 255});
        DrawRectangleRec({MX(2.5f) * DRAW_SCALE, 0.0f, 2.8f * DRAW_SCALE, WORLD_H * DRAW_SCALE}, {52, 53, 57, 255});
        Color rail = {70, 71, 76, 255};
        DrawLineEx(WS(MX(XU), YT), WS(MX(XU), YB), 0.1f * DRAW_SCALE, rail);
        DrawLineEx(WS(MX(XR), YT), WS(MX(XR), YB), 0.1f * DRAW_SCALE, rail);
        for (int k = 0; k < 2; ++k) {
            Vector2 c = WS(MX(XC()), k == 0 ? YB : YT);
            DrawCircleV(c, (R() - 0.1f) * DRAW_SCALE, COL_ORANGE);
            for (int i = 0; i < 8; ++i) {
                float ang = sprocketAngle + i * (2.0f * PI / 8.0f);
                DrawCircleV({c.x + std::cos(ang) * (R() - 0.1f) * DRAW_SCALE, c.y + std::sin(ang) * (R() - 0.1f) * DRAW_SCALE},
                            0.1f * DRAW_SCALE, COL_DARK);
            }
            DrawCircleV(c, 0.25f * DRAW_SCALE, COL_DARK);
        }
    }
    void DrawFront() const {
        for (const b2Body* b : paddles) {
            b2Vec2 p = b->GetPosition();
            Rectangle rec = {p.x * DRAW_SCALE, p.y * DRAW_SCALE, 2.0f * HALF_W * DRAW_SCALE, 2.0f * HALF_T * DRAW_SCALE};
            DrawRectanglePro(rec, {rec.width * 0.5f, rec.height * 0.5f}, b->GetAngle() * RAD2DEG, COL_LIGHT);
        }
    }
};

// Chambre à étoile : sas doseur rotatif.
struct StarGear {
    b2Body* body = nullptr;
    float radius, speed;
    int blades;
    StarGear(b2World& world, b2Vec2 c, float r, int n, float spd, float a0) : radius(r), speed(spd), blades(n) {
        b2BodyDef bd; bd.type = b2_kinematicBody; bd.position = c; bd.angle = a0;
        body = world.CreateBody(&bd);
        b2CircleShape hub; hub.m_radius = 0.35f;
        b2FixtureDef fh; fh.shape = &hub; fh.friction = 0.2f; fh.filter.categoryBits = CAT_MACHINE;
        body->CreateFixture(&fh);
        for (int i = 0; i < blades; ++i) {
            float ang = i * 2.0f * PI / blades;
            float hx = (radius - 0.3f) * 0.5f;
            b2PolygonShape blade;
            blade.SetAsBox(hx, 0.06f, b2Vec2(std::cos(ang) * (0.3f + hx), std::sin(ang) * (0.3f + hx)), ang);
            b2FixtureDef fb; fb.shape = &blade; fb.friction = 0.2f; fb.filter.categoryBits = CAT_MACHINE;
            body->CreateFixture(&fb);
        }
        body->SetAngularVelocity(speed);
    }
    void Draw() const {
        b2Vec2 p = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(p.x * DRAW_SCALE, p.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);
        for (int i = 0; i < blades; ++i) {
            rlPushMatrix();
            rlRotatef(i * 360.0f / blades, 0.0f, 0.0f, 1.0f);
            DrawRectangleRec({0.0f, -0.06f * DRAW_SCALE, radius * DRAW_SCALE, 0.12f * DRAW_SCALE}, COL_LIGHT);
            rlPopMatrix();
        }
        DrawCircle(0, 0, 0.38f * DRAW_SCALE, COL_LIGHT);
        DrawCircle(0, 0, 0.14f * DRAW_SCALE, COL_DARK);
        rlPopMatrix();
    }
};

// Grand anneau : îlot central fixe + bras rotatifs qui poussent la matière.
struct RingArms {
    b2Body* body = nullptr;
    float inner, outer, speed;
    int arms;
    RingArms(b2World& world, b2Vec2 c, float in, float out, int n, float spd) : inner(in), outer(out), speed(spd), arms(n) {
        b2BodyDef bd; bd.type = b2_kinematicBody; bd.position = c;
        body = world.CreateBody(&bd);
        for (int i = 0; i < arms; ++i) {
            float ang = i * 2.0f * PI / arms + 0.6f;
            float hx = (outer - inner) * 0.5f;
            b2PolygonShape arm;
            arm.SetAsBox(hx, 0.15f, b2Vec2(std::cos(ang) * (inner + hx), std::sin(ang) * (inner + hx)), ang);
            b2FixtureDef fa; fa.shape = &arm; fa.friction = 0.2f; fa.filter.categoryBits = CAT_MACHINE;
            body->CreateFixture(&fa);
        }
        body->SetAngularVelocity(speed);
    }
    void Draw() const {
        b2Vec2 p = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(p.x * DRAW_SCALE, p.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);
        for (int i = 0; i < arms; ++i) {
            rlPushMatrix();
            rlRotatef(i * 360.0f / arms + 0.6f * RAD2DEG, 0.0f, 0.0f, 1.0f);
            DrawRectangleRec({inner * DRAW_SCALE, -0.15f * DRAW_SCALE, (outer - inner) * DRAW_SCALE, 0.3f * DRAW_SCALE}, COL_LIGHT);
            rlPopMatrix();
        }
        rlPopMatrix();
    }
};

// ---------------- PARTICULES ----------------

struct Particle {
    b2Body* body = nullptr;
    float hue = 0.0f;
    Color color{255, 255, 255, 255};

    Particle(b2World& world, float x, float y, float h) : hue(h) {
        b2BodyDef bd; bd.type = b2_dynamicBody; bd.position.Set(x, y);
        bd.linearDamping = 0.02f; bd.angularDamping = 0.1f;
        body = world.CreateBody(&bd);
        b2CircleShape shape; shape.m_radius = P_R;
        b2FixtureDef fd; fd.shape = &shape; fd.density = 1.0f;
        fd.friction = 0.08f; fd.restitution = 0.03f;
        fd.filter.categoryBits = CAT_PARTICLE;
        body->CreateFixture(&fd);
        UpdateColor();
    }
    void UpdateColor() { color = ColorFromHSV(std::fmod(hue + 3600.0f, 360.0f), 0.9f, 1.0f); }
    void Draw() const {
        const b2Vec2 p = body->GetPosition();
        DrawPoly(WS(p.x, p.y), 6, P_R * 1.18f * DRAW_SCALE, body->GetAngle() * RAD2DEG, color);
    }
};

// ---------------- LA MACHINE ----------------

struct TubeVis { std::vector<b2Vec2> pts; float hw; };

struct Machine {
    // Cuve
    static constexpr float TANK_CX = 9.0f, TANK_CY = 8.6f, TANK_R = 6.8f;
    // Chambres à étoiles
    static constexpr float CH_R = 2.2f, CH1_Y = 17.6f, CH2_Y = 21.8f;
    // Anneau
    static constexpr float RING_CX = 35.0f, RING_CY = 22.0f, RING_R = 8.0f, RING_ISLAND = 2.6f;

    b2World world;
    std::vector<BoxWall> walls;
    std::vector<Particle> particles;
    std::vector<StarGear> stars;
    std::vector<TubeVis> tubes;
    Lift lift;
    RingArms ring;
    std::vector<float> prevX;
    int frame = 0;
    int leaks = 0;
    long paintCount = 0;
    float simTime = 0.0f;

    Machine()
        : world(b2Vec2(0.0f, GRAVITY)), lift(world),
          ring(world, b2Vec2(RING_CX, RING_CY), RING_ISLAND + 0.2f, RING_R - 0.45f, 3, -0.55f)
    {
        world.SetContinuousPhysics(true);
        Build();
    }

    void Polyline(const std::vector<b2Vec2>& pts, float th) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            if (b2Distance(pts[i], pts[i + 1]) < 0.02f) continue;
            walls.emplace_back(world, pts[i], pts[i + 1], th);
        }
    }

    // Tube de demi-largeur hw autour d'une ligne centrale ; les extrémités peuvent être
    // forcées pour se raccorder exactement aux ouvertures des chambres.
    void Tube(const std::vector<b2Vec2>& center, float hw, bool leftward, bool ceiling,
              const b2Vec2* ceilStart, const b2Vec2* floorStart,
              const b2Vec2* ceilEnd, const b2Vec2* floorEnd) {
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
        if (ceilStart)  ce.front() = *ceilStart;
        if (floorEnd)   fl.back()  = *floorEnd;
        if (ceilEnd)    ce.back()  = *ceilEnd;
        Polyline(fl, 0.3f);
        if (ceiling) Polyline(ce, 0.3f);
        tubes.push_back({c, hw});
    }

    void Build() {
        const b2Vec2 tc(TANK_CX, TANK_CY);

        // ===== Ascenseur (côté droit) : canal, sol, mur intérieur
        Polyline({{WORLD_W - 2.6f, 0.3f}, {WORLD_W - 2.6f, 33.7f}, {WORLD_W - 6.4f, 33.7f}}, 0.4f);
        Polyline({{WORLD_W - 5.6f, 3.4f}, {WORLD_W - 5.6f, 30.2f}}, 0.4f);

        // ===== Cuve : deux arcs, entrée à -10 deg (goulotte), sortie à 90 deg (bas)
        {
            float gIn = std::asin(1.3f / TANK_R) * RAD2DEG, gOut = std::asin(0.8f / TANK_R) * RAD2DEG;
            Polyline(ArcPts(tc, TANK_R, -10.0f + gIn, 90.0f - gOut, 40), 0.3f);
            Polyline(ArcPts(tc, TANK_R, 90.0f + gOut, 350.0f - gIn, 90), 0.3f);
        }

        // ===== Chambres à étoiles (2), sas doseurs empilés sous la cuve
        const float chG = std::asin(0.8f / CH_R) * RAD2DEG;
        const float chY[2] = {CH1_Y, CH2_Y};
        for (int k = 0; k < 2; ++k) {
            b2Vec2 c(TANK_CX, chY[k]);
            Polyline(ArcPts(c, CH_R, -90.0f + chG, 90.0f - chG, 16), 0.3f);
            Polyline(ArcPts(c, CH_R, 90.0f + chG, 270.0f - chG, 16), 0.3f);
            stars.emplace_back(world, c, CH_R - 0.35f, 8, (k == 0 ? 1.4f : -1.4f), 0.2f * k);
        }
        {   // raccords verticaux : cuve -> chambre 1 -> chambre 2
            float yT = TANK_CY + TANK_R * std::sin((90.0f - std::asin(0.8f / TANK_R) * RAD2DEG) * DEG2RAD);
            float y1t = CH1_Y - CH_R * std::cos(std::asin(0.8f / CH_R)), y1b = CH1_Y + CH_R * std::cos(std::asin(0.8f / CH_R));
            float y2t = CH2_Y - CH_R * std::cos(std::asin(0.8f / CH_R));
            for (float sx : {-0.8f, 0.8f}) {
                Polyline({{TANK_CX + sx, yT}, {TANK_CX + sx, y1t}}, 0.3f);
                Polyline({{TANK_CX + sx, y1b}, {TANK_CX + sx, y2t}}, 0.3f);
            }
        }

        // ===== Tube sinueux : chambre 2 -> anneau (entrée à 130 deg)
        {
            const b2Vec2 rc(RING_CX, RING_CY);
            float gh = std::asin(1.0f / RING_R) * RAD2DEG;
            b2Vec2 inUp = OnCircle(rc, RING_R, 130.0f + gh), inLo = OnCircle(rc, RING_R, 130.0f - gh);
            float y2b = CH2_Y + CH_R * std::cos(std::asin(0.8f / CH_R));
            b2Vec2 cs(TANK_CX + 0.8f, y2b), fs(TANK_CX - 0.8f, y2b);
            std::vector<b2Vec2> c = {{TANK_CX, y2b}, {TANK_CX, y2b + 0.75f}, {10.0f, 25.4f}, {12.5f, 25.8f},
                                     {15.5f, 26.3f}, {18.5f, 26.5f}, {21.5f, 27.2f}, {24.5f, 27.5f},
                                     {27.2f, 28.0f}, {29.4f, 28.2f}};
            Tube(c, 0.8f, false, true, &cs, &fs, &inUp, &inLo);

            // ===== Anneau : deux arcs (entrée 130 deg, sortie 40 deg), îlot central
            float g1 = std::asin(1.0f / RING_R) * RAD2DEG;
            Polyline(ArcPts(rc, RING_R, 40.0f + g1, 130.0f - g1, 36), 0.3f);
            Polyline(ArcPts(rc, RING_R, 130.0f + g1, 400.0f - g1, 110), 0.3f);
            {   // îlot : polygone fixe (cercle)
                b2BodyDef bd; bd.position = rc;
                b2Body* isl = world.CreateBody(&bd);
                b2CircleShape cs2; cs2.m_radius = RING_ISLAND;
                b2FixtureDef fd; fd.shape = &cs2; fd.friction = 0.04f; fd.filter.categoryBits = CAT_WALL;
                isl->CreateFixture(&fd);
            }

            // ===== Tube de sortie de l'anneau jusqu'au pied de l'ascenseur
            b2Vec2 outUp = OnCircle(rc, RING_R, 40.0f - g1), outLo = OnCircle(rc, RING_R, 40.0f + g1);
            std::vector<b2Vec2> o = {{41.1f, 27.1f}, {44.0f, 28.4f}, {46.5f, 29.0f}, {49.0f, 30.2f},
                                     {52.0f, 31.0f}, {55.0f, 32.0f}, {WORLD_W - 5.6f, 32.7f}};
            b2Vec2 oEnd(WORLD_W - 5.6f, 30.2f);
            Tube(o, 1.0f, false, true, &outUp, &outLo, &oEnd, nullptr);
        }

        // ===== Longue goulotte de retour : sommet de l'ascenseur -> entrée de la cuve
        {
            float gIn = std::asin(1.3f / TANK_R) * RAD2DEG;
            b2Vec2 up = OnCircle(tc, TANK_R, -10.0f - gIn), lo = OnCircle(tc, TANK_R, -10.0f + gIn);
            std::vector<b2Vec2> c = {{WORLD_W - 5.3f, 2.3f}, {WORLD_W - 7.0f, 2.45f}, {50.0f, 3.0f}, {40.0f, 4.2f}, {30.0f, 5.6f},
                                     {22.0f, 6.8f}, {16.4f, 7.45f}};
            Tube(c, 1.3f, true, true, nullptr, nullptr, &up, &lo);
        }
    }

    // Les particules naissent dans la cuve, en réseau hexagonal, colorées selon leur angle
    // (roue arc-en-ciel), sous l'ouverture de la goulotte d'entrée.
    void SpawnParticles() {
        const float dx = 2.0f * P_R * 1.03f, dy = dx * 0.8660254f;
        const float yMin = 9.45f, yMax = TANK_CY + TANK_R - 0.45f;
        int placed = 0;
        for (int row = 0; ; ++row) {
            float y = yMax - row * dy;
            if (y < yMin || placed >= TARGET_PARTICLES) break;
            float half = std::sqrt(std::max(0.0f, (TANK_R - 0.35f) * (TANK_R - 0.35f) - (y - TANK_CY) * (y - TANK_CY)));
            float x0 = TANK_CX - half + ((row % 2) ? dx * 0.5f : 0.0f);
            for (float x = x0; x <= TANK_CX + half && placed < TARGET_PARTICLES; x += dx, ++placed) {
                float ang = std::atan2(y - TANK_CY, x - TANK_CX) * RAD2DEG;
                particles.emplace_back(world, x, y, ang);
            }
        }
        prevX.assign(particles.size(), 0.0f);
        for (size_t i = 0; i < particles.size(); ++i) prevX[i] = particles[i].body->GetPosition().x;
        std::printf("%zu particules placees dans la cuve\n", particles.size());
    }

    void Rescue() {
        for (auto& p : particles) {
            b2Vec2 q = p.body->GetPosition();
            if (q.y > WORLD_H + 1.0f || q.x < -1.0f || q.x > WORLD_W + 1.0f || q.y < -5.0f) {
                p.body->SetTransform(b2Vec2(TANK_CX, 10.0f), 0.0f);
                p.body->SetLinearVelocity(b2Vec2(0.0f, 0.0f));
                ++leaks;
            }
        }
    }

    void Step() {
        const float dt = FIXED_DT / SUBSTEPS;
        for (int k = 0; k < SUBSTEPS; ++k) {
            lift.Update(dt);
            world.Step(dt, 8, 3);
        }
        simTime += FIXED_DT;
        // porte de peinture : au sommet de l'ascenseur, la particule prend la teinte du moment
        const float gateX = WORLD_W - 7.0f;
        for (size_t i = 0; i < particles.size(); ++i) {
            b2Vec2 q = particles[i].body->GetPosition();
            if (prevX[i] >= gateX && q.x < gateX && q.y < 6.0f) {
                particles[i].hue = std::fmod(simTime * HUE_SPEED, 360.0f);
                particles[i].UpdateColor();
                ++paintCount;
            }
            prevX[i] = q.x;
        }
        if (frame % 30 == 0) Rescue();
        ++frame;
    }
};

// ---------------- DESSIN ----------------

static void DrawScene(const Machine& m) {
    ClearBackground(COL_BG);

    // formes sombres : cuve, chambres, anneau, tubes
    DrawCircleV(WS(Machine::TANK_CX, Machine::TANK_CY), Machine::TANK_R * DRAW_SCALE, COL_DARK);
    DrawCircleV(WS(Machine::TANK_CX, Machine::CH1_Y), Machine::CH_R * DRAW_SCALE, COL_DARK);
    DrawCircleV(WS(Machine::TANK_CX, Machine::CH2_Y), Machine::CH_R * DRAW_SCALE, COL_DARK);
    DrawCircleV(WS(Machine::RING_CX, Machine::RING_CY), Machine::RING_R * DRAW_SCALE, COL_DARK);
    DrawRectangleRec({(Machine::TANK_CX - 0.8f) * DRAW_SCALE, (Machine::TANK_CY + Machine::TANK_R - 0.5f) * DRAW_SCALE,
                      1.6f * DRAW_SCALE, 3.0f * DRAW_SCALE}, COL_DARK);
    for (const auto& t : m.tubes) {
        for (size_t i = 0; i + 1 < t.pts.size(); ++i)
            DrawLineEx(WS(t.pts[i].x, t.pts[i].y), WS(t.pts[i + 1].x, t.pts[i + 1].y), (2.0f * t.hw + 0.25f) * DRAW_SCALE, COL_DARK);
        for (const auto& p : t.pts) DrawCircleV(WS(p.x, p.y), (t.hw + 0.12f) * DRAW_SCALE, COL_DARK);
    }
    // îlot de l'anneau (même gris que le fond)
    DrawCircleV(WS(Machine::RING_CX, Machine::RING_CY), Machine::RING_ISLAND * DRAW_SCALE, COL_BG);

    m.lift.DrawBack();

    // porte de peinture (trait lumineux à la couleur du moment)
    {
        Color c = ColorFromHSV(std::fmod(m.simTime * HUE_SPEED, 360.0f), 0.9f, 1.0f);
        float gx = (WORLD_W - 7.0f) * DRAW_SCALE;
        DrawLineEx({gx, 0.5f * DRAW_SCALE}, {gx, 4.2f * DRAW_SCALE}, 0.14f * DRAW_SCALE, c);
    }

    for (const auto& p : m.particles) p.Draw();

    m.lift.DrawFront();
    for (const auto& s : m.stars) s.Draw();
    m.ring.Draw();
    for (const auto& w : m.walls) w.Draw();
    // îlot recouvre l'extrémité des bras
    DrawCircleV(WS(Machine::RING_CX, Machine::RING_CY), Machine::RING_ISLAND * DRAW_SCALE, COL_BG);
    DrawCircleV(WS(Machine::RING_CX, Machine::RING_CY), 0.25f * DRAW_SCALE, COL_DARK);

    DrawText("THE RAINBOW CIRCUIT", 90, 2040, 38, {60, 60, 64, 255});
}

// ---------------- PROGRAMME PRINCIPAL ----------------

#ifndef CIRCUIT_TEST
int main() {
    SetConfigFlags(USE_MSAA ? FLAG_MSAA_4X_HINT : 0);
    InitWindow(1280, 720, "Rainbow Circuit - 4K");
    SetTargetFPS(60);

    Machine machine;
    machine.SpawnParticles();

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

    if (machine.leaks > 0) std::printf("ATTENTION : %d particules remises en jeu par le filet de securite\n", machine.leaks);

    if (ffmpeg) PIPE_CLOSE(ffmpeg);
    UnloadRenderTexture(target);
    CloseWindow();
    return exportFailed ? 2 : 0;
}
#endif