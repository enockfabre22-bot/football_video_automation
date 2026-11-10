// Animation 2D - raylib + Box2D, export natif 4K.
// Concept : LA MÉGA-USINE (Roue -> Broyeur -> Convoyeur à rouleaux -> Plinko géant).
// Utilisation de tout l'espace 4K avec un parcours en "S" complexe et hypnotique.

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
static constexpr int FPS = 60;

// NOUVELLE ÉCHELLE : On "recule" la caméra. 
// L'écran représente maintenant 64m de large x 36m de haut !
static constexpr float PIXELS_PER_METER = 60.0f;
static constexpr float DRAW_SCALE = PIXELS_PER_METER;

static constexpr int TOTAL_BALLS = 2200; // 1200 billes = 4800 débris finaux. 
static constexpr int MAX_FEED_ZONE_BALLS = 250; 
static constexpr int MAX_FRAMES = 35000; // ~4 minutes de vidéo
static constexpr int SPAWN_INTERVAL_FRAMES = 4; 

static constexpr bool EXPORT_VIDEO = true;
static constexpr bool USE_MSAA = true;

static constexpr float BALL_RADIUS = 0.25f;  
static constexpr float DEBRIS_RADIUS = 0.08f; 
static constexpr float GRAVITY = 9.81f;
static constexpr float FIXED_DT = 1.0f / 60.0f;

enum CollisionCategory {
    CAT_WALL     = 0x0001,
    CAT_CRUSHER  = 0x0002,
    CAT_BALL     = 0x0004,
    CAT_DEBRIS   = 0x0008,
    CAT_CONVEYOR = 0x0010
};

// Palette "Mega Factory"
static const Color COL_BG       = {235, 240, 245, 255}; 
static const Color COL_WALL     = {45, 50, 60, 255};    
static const Color COL_PEG      = {160, 170, 180, 255};
static const Color COL_BASIN    = {190, 200, 210, 255};
static const Color COL_CRUSHER  = {60, 65, 75, 255};
static const Color COL_CONVEYOR = {100, 110, 120, 255};
static const Color COLORS[7] = {
    {255, 59, 48, 255},  {255, 149, 0, 255},  {255, 204, 0, 255},
    {52, 199, 89, 255},  {90, 200, 250, 255}, {0, 122, 255, 255},
    {175, 82, 222, 255}
};

static Vector2 WorldToScreen(const b2Vec2& p) {
    return {p.x * DRAW_SCALE, p.y * DRAW_SCALE};
}

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
        
        if (length > thickness) {
            DrawCircleV(WorldToScreen(p1), thickness * DRAW_SCALE * 0.5f, color);
            DrawCircleV(WorldToScreen(p2), thickness * DRAW_SCALE * 0.5f, color);
        }
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
        fd.restitution = 0.2f;
        fd.filter.categoryBits = CAT_WALL;
        body->CreateFixture(&fd);
    }

    void Draw() const {
        Vector2 p = WorldToScreen(body->GetPosition());
        DrawCircleV(p, radius * DRAW_SCALE, COL_PEG);
    }
};

// ---------------- MÉCANISMES ----------------

struct WaterWheel {
    b2Body* body = nullptr;
    float radius = 5.0f; 
    int blades = 10;
    float speed = -1.5f; 

    WaterWheel(b2World& world, b2Vec2 center) {
        b2BodyDef bd;
        bd.type = b2_kinematicBody; 
        bd.position = center;
        body = world.CreateBody(&bd);

        b2CircleShape centerCircle;
        centerCircle.m_radius = 1.0f;
        b2FixtureDef fdCenter;
        fdCenter.shape = &centerCircle;
        fdCenter.filter.categoryBits = CAT_WALL;
        body->CreateFixture(&fdCenter);

        for (int i = 0; i < blades; ++i) {
            b2PolygonShape blade;
            float angle = (i * 360.0f / blades) * DEG2RAD;
            float hx = (radius - 1.0f) * 0.5f;
            float hy = 0.2f; 
            float dist = 1.0f + hx;
            b2Vec2 localCenter(std::cos(angle) * dist, std::sin(angle) * dist);
            blade.SetAsBox(hx, hy, localCenter, angle);

            b2FixtureDef fdBlade;
            fdBlade.shape = &blade;
            fdBlade.friction = 0.5f;
            fdBlade.filter.categoryBits = CAT_WALL;
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

        DrawCircle(0, 0, 1.0f * DRAW_SCALE, COL_WALL);
        for (int i = 0; i < blades; ++i) {
            rlPushMatrix();
            rlRotatef(i * (360.0f / blades), 0.0f, 0.0f, 1.0f);
            Rectangle rec = { 1.0f * DRAW_SCALE, -0.2f * DRAW_SCALE, (radius - 1.0f) * DRAW_SCALE, 0.4f * DRAW_SCALE };
            DrawRectangleRec(rec, COL_WALL);
            rlPopMatrix();
        }
        DrawCircle(0, 0, 0.3f * DRAW_SCALE, COL_BG); 
        rlPopMatrix();
    }
};

struct CrusherRoller {
    b2Body* body = nullptr;
    float radius;
    int teethCount;
    float speed;
    
    CrusherRoller(b2World& world, b2Vec2 center, float r, int teeth, float s) : radius(r), teethCount(teeth), speed(s) {
        b2BodyDef bd;
        bd.type = b2_kinematicBody;
        bd.position = center;
        body = world.CreateBody(&bd);

        float coreRadius = radius * 0.85f;
        b2CircleShape circle;
        circle.m_radius = coreRadius;
        
        b2FixtureDef fd;
        fd.shape = &circle;
        fd.friction = 0.9f; 
        fd.filter.categoryBits = CAT_CRUSHER;
        fd.filter.maskBits = CAT_BALL | CAT_WALL; 
        body->CreateFixture(&fd);

        float toothLength = radius * 0.15f;
        for (int i = 0; i < teeth; ++i) {
            b2PolygonShape tooth;
            float angle = (i * 360.0f / teeth) * DEG2RAD;
            float hx = toothLength * 0.5f;
            float hy = radius * 0.12f; 
            float dist = coreRadius + hx;
            b2Vec2 tc(std::cos(angle) * dist, std::sin(angle) * dist);
            tooth.SetAsBox(hx, hy, tc, angle);

            b2FixtureDef tfd;
            tfd.shape = &tooth;
            tfd.friction = 0.9f;
            tfd.filter.categoryBits = CAT_CRUSHER;
            tfd.filter.maskBits = CAT_BALL | CAT_WALL;
            body->CreateFixture(&tfd);
        }
        body->SetAngularVelocity(speed);
    }
    
    void Update() { body->SetAngularVelocity(speed); }
    
    void Draw() const {
        b2Vec2 pos = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(pos.x * DRAW_SCALE, pos.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);

        float coreRadius = radius * 0.85f;
        DrawCircle(0, 0, coreRadius * DRAW_SCALE, COL_CRUSHER);

        float toothLength = radius * 0.15f;
        for (int i = 0; i < teethCount; ++i) {
            rlPushMatrix();
            rlRotatef(i * (360.0f / teethCount), 0.0f, 0.0f, 1.0f);
            float w = toothLength * DRAW_SCALE;
            float h = radius * 0.24f * DRAW_SCALE;
            Rectangle rec = { coreRadius * DRAW_SCALE, 0.0f, w, h };
            DrawRectanglePro(rec, { 0.0f, h * 0.5f }, 0.0f, COL_CRUSHER);
            rlPopMatrix();
        }
        DrawCircle(0, 0, 0.5f * DRAW_SCALE, COL_BG);
        rlPopMatrix();
    }
};

struct ConveyorRoller {
    b2Body* body = nullptr;
    float radius;
    float speed;

    ConveyorRoller(b2World& world, b2Vec2 center, float r, float s) : radius(r), speed(s) {
        b2BodyDef bd;
        bd.type = b2_kinematicBody;
        bd.position = center;
        body = world.CreateBody(&bd);

        b2CircleShape circle;
        circle.m_radius = radius;
        b2FixtureDef fd;
        fd.shape = &circle;
        fd.friction = 0.95f; 
        fd.filter.categoryBits = CAT_CONVEYOR;
        fd.filter.maskBits = 0xFFFF;
        body->CreateFixture(&fd);
        
        body->SetAngularVelocity(speed);
    }

    void Update() { body->SetAngularVelocity(speed); }

    void Draw() const {
        b2Vec2 pos = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(pos.x * DRAW_SCALE, pos.y * DRAW_SCALE, 0.0f);
        rlRotatef(body->GetAngle() * RAD2DEG, 0.0f, 0.0f, 1.0f);

        DrawCircle(0, 0, radius * DRAW_SCALE, COL_CONVEYOR);
        DrawLineEx({ -radius * DRAW_SCALE * 0.8f, 0 }, { radius * DRAW_SCALE * 0.8f, 0 }, 2.0f, {50, 55, 60, 255});
        DrawLineEx({ 0, -radius * DRAW_SCALE * 0.8f }, { 0, radius * DRAW_SCALE * 0.8f }, 2.0f, {50, 55, 60, 255});
        
        rlPopMatrix();
    }
};

// ---------------- BILLES ET DÉBRIS ----------------

struct Ball {
    b2Body* body = nullptr;
    b2Fixture* fixture = nullptr;
    int typeIndex = 0;
    bool isDebris = false;
    float radius;

    Ball(b2World& world, float x, float y, int type, bool debris = false) 
        : typeIndex(type), isDebris(debris) {
        
        radius = debris ? DEBRIS_RADIUS : BALL_RADIUS;

        b2BodyDef bd;
        bd.type = b2_dynamicBody;
        bd.position.Set(x, y);
        bd.allowSleep = true;
        bd.bullet = debris; 
        
        bd.linearDamping = debris ? 1.2f : 0.02f; 
        bd.angularDamping = debris ? 1.2f : 0.05f;
        body = world.CreateBody(&bd);

        b2CircleShape shape;
        shape.m_radius = radius;

        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = debris ? 2.5f : 1.0f; 
        fd.friction = debris ? 0.8f : 0.2f; 
        fd.restitution = 0.0f; 
        
        if (debris) {
            fd.filter.categoryBits = CAT_DEBRIS;
            fd.filter.maskBits = CAT_WALL | CAT_BALL | CAT_DEBRIS | CAT_CONVEYOR;
        } else {
            fd.filter.categoryBits = CAT_BALL;
            fd.filter.maskBits = 0xFFFF;
        }
        
        fixture = body->CreateFixture(&fd);
    }

    void Draw() const {
        const Vector2 p = WorldToScreen(body->GetPosition());
        const float r = radius * DRAW_SCALE;

        if (!isDebris) {
            DrawCircleV({p.x + r * 0.07f, p.y + r * 0.10f}, r * 1.02f, {0,0,0,22});
            DrawCircleV(p, r, COLORS[typeIndex]);
            DrawCircleV({p.x - r * 0.28f, p.y - r * 0.30f}, r * 0.22f, {255,255,255,115});
            DrawCircleLines((int)p.x, (int)p.y, r, {30,30,30,105});
        } else {
            DrawCircleV(p, r * 1.2f, COLORS[typeIndex]);
        }
    }
};

// ---------------- DESSIN SCÈNE ----------------

static void DrawScene(const std::vector<BoxWall>& walls,
                      const std::vector<Peg>& pegs,
                      const WaterWheel& wheel,
                      const CrusherRoller& roller1,
                      const CrusherRoller& roller2,
                      const std::vector<ConveyorRoller>& conveyors,
                      const std::vector<Ball>& balls,
                      int spawnedBalls) {
    ClearBackground(COL_BG);
    DrawRectangle(0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT, COL_BG);

    for (const auto& ball : balls) ball.Draw();
    
    wheel.Draw();
    roller1.Draw();
    roller2.Draw();
    for (const auto& c : conveyors) c.Draw();
    for (const auto& peg : pegs) peg.Draw();
    for (const auto& wall : walls) wall.Draw();

    DrawText("THE MEGA FACTORY", 90, 70, 40, {65,65,65,255});
    DrawText(TextFormat("PROCESSED  %d / %d", spawnedBalls, TOTAL_BALLS), 90, 125, 26, {105,105,105,255});
    DrawLine(90, 170, 600, 170, {205,205,205,255});
}

// ---------------- PROGRAMME PRINCIPAL ----------------

int main() {
    SetConfigFlags(USE_MSAA ? FLAG_MSAA_4X_HINT : 0);
    InitWindow(1280, 720, "Mega Factory - 4K");
    SetTargetFPS(60);

    b2World world(b2Vec2(0.0f, GRAVITY));
    world.SetAllowSleeping(true);
    world.SetWarmStarting(true);
    world.SetContinuousPhysics(true);

    std::vector<BoxWall> walls;
    std::vector<Peg> pegs;
    std::vector<ConveyorRoller> conveyors;
    std::vector<Ball> balls;
    balls.reserve(TOTAL_BALLS * 5); 

    // --- ÉTAGE 1 : L'Entonnoir et la Roue (En haut à gauche) ---
    b2Vec2 wheelCenter(12.0f, 9.5f);
    WaterWheel wheel(world, wheelCenter);

    walls.emplace_back(world, b2Vec2(6.0f, -2.0f), b2Vec2(11.0f, 4.2f), 0.5f, COL_WALL);
    walls.emplace_back(world, b2Vec2(18.0f, -2.0f), b2Vec2(15.5f, 4.2f), 0.5f, COL_WALL);
    
    float statorRadius = 5.1f;
    int segments = 16;
    for (int i = 0; i < segments; ++i) {
        float a1 = (70.0f + (200.0f * i / segments)) * DEG2RAD;
        float a2 = (70.0f + (200.0f * (i + 1) / segments)) * DEG2RAD;
        b2Vec2 p1(wheelCenter.x + std::cos(a1) * statorRadius, wheelCenter.y + std::sin(a1) * statorRadius);
        b2Vec2 p2(wheelCenter.x + std::cos(a2) * statorRadius, wheelCenter.y + std::sin(a2) * statorRadius);
        walls.emplace_back(world, p1, p2, 0.4f, COL_WALL);
    }

    // --- ÉTAGE 2 : La Rampe et le Broyeur (Au Centre) ---
    walls.emplace_back(world, b2Vec2(13.5f, 14.2f), b2Vec2(29.0f, 18.0f), 0.5f, COL_WALL); 
    walls.emplace_back(world, b2Vec2(15.0f, 11.5f), b2Vec2(28.0f, 15.5f), 0.4f, COL_WALL); 

    b2Vec2 r1Center(30.0f, 19.5f);
    b2Vec2 r2Center(34.5f, 19.5f);
    CrusherRoller roller1(world, r1Center, 2.2f, 14, 3.5f); 
    CrusherRoller roller2(world, r2Center, 2.2f, 14, -3.5f);  
    
    walls.emplace_back(world, b2Vec2(36.5f, 13.0f), b2Vec2(33.5f, 17.5f), 0.5f, COL_WALL);

    // --- ÉTAGE 3 : L'Entonnoir sous le broyeur et le Tapis Roulant ---
    // LA CORRECTION EST ICI : 
    // Le mur gauche est baissé pour coller à la perfection au tapis (empêchant la fuite dans le vide)
    walls.emplace_back(world, b2Vec2(27.0f, 22.0f), b2Vec2(30.5f, 26.2f), 0.4f, COL_WALL);
    // Le mur droit est légèrement remonté pour que la poudre s'écoule plus fluidement
    walls.emplace_back(world, b2Vec2(37.5f, 22.0f), b2Vec2(33.0f, 24.5f), 0.4f, COL_WALL);

    // Le Convoyeur à rouleaux 
    for (float cx = 31.0f; cx <= 52.0f; cx += 1.0f) {
        conveyors.emplace_back(world, b2Vec2(cx, 26.2f), 0.45f, 8.0f);
    }
    // Plancher de sécurité
    walls.emplace_back(world, b2Vec2(30.5f, 27.0f), b2Vec2(53.0f, 27.0f), 0.4f, COL_WALL);

    // --- ÉTAGE 4 : La Cascade Plinko géante (En bas à droite) ---
    walls.emplace_back(world, b2Vec2(52.5f, 26.0f), b2Vec2(52.5f, 28.5f), 0.4f, COL_WALL);
    walls.emplace_back(world, b2Vec2(58.0f, 23.0f), b2Vec2(58.0f, 28.5f), 0.4f, COL_WALL);

    for (int row = 0; row < 6; ++row) {
        int cols = 12 + (row % 2);
        float startX = 55.0f - (cols * 0.9f) * 0.5f;
        for (int col = 0; col < cols; ++col) {
            pegs.emplace_back(world, startX + col * 0.9f, 29.5f + row * 0.8f);
        }
    }

    // --- ÉTAGE 5 : Le Méga Silo de Réception ---
    walls.emplace_back(world, b2Vec2(44.0f, 28.5f), b2Vec2(44.0f, 35.5f), 0.5f, COL_BASIN);
    walls.emplace_back(world, b2Vec2(61.0f, 28.5f), b2Vec2(61.0f, 35.5f), 0.5f, COL_BASIN);
    walls.emplace_back(world, b2Vec2(44.0f, 35.5f), b2Vec2(61.0f, 35.5f), 0.5f, COL_BASIN); 

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
    int spawned = 0;
    bool exportFailed = EXPORT_VIDEO && ffmpeg == nullptr;

    while (!WindowShouldClose() && frame < MAX_FRAMES) {
        
        int inFeedZone = 0;
        for (const auto& ball : balls) {
            if (!ball.isDebris) ++inFeedZone;
        }

        if (spawned < TOTAL_BALLS && inFeedZone < MAX_FEED_ZONE_BALLS && frame % SPAWN_INTERVAL_FRAMES == 0) {
            float spawnX = 13.5f + (GetRandomValue(-150, 150) / 100.0f);
            balls.emplace_back(world, spawnX, -1.0f, spawned % 7);
            ++spawned;
        }

        world.Step(FIXED_DT, 12, 8);
        wheel.Update();
        roller1.Update();
        roller2.Update();
        for (auto& c : conveyors) c.Update();

        // ---------------- DÉCLENCHEUR DE FRACTURATION ----------------
        std::vector<Ball> newDebris; 

        for (auto it = balls.begin(); it != balls.end(); ) {
            if (!it->isDebris) {
                const b2Vec2 p = it->body->GetPosition();
                
                if (p.x > 29.5f && p.x < 34.8f && p.y > 18.5f) {
                    
                    b2Vec2 vel = it->body->GetLinearVelocity();
                    int t = it->typeIndex;
                    world.DestroyBody(it->body);
                    it = balls.erase(it);

                    for (int i = 0; i < 4; ++i) {
                        float ox = (GetRandomValue(-100, 100) / 100.0f) * 0.15f;
                        float oy = (GetRandomValue(-100, 100) / 100.0f) * 0.15f;
                        newDebris.emplace_back(world, p.x + ox, p.y + oy, t, true);
                        
                        b2Vec2 dVel = vel;
                        dVel.y += GetRandomValue(30, 60) / 10.0f; 
                        newDebris.back().body->SetLinearVelocity(dVel);
                    }
                    continue; 
                }
            }
            ++it;
        }

        for (const auto& d : newDebris) {
            balls.push_back(d);
        }
        // -------------------------------------------------------------

        BeginTextureMode(target);
        DrawScene(walls, pegs, wheel, roller1, roller2, conveyors, balls, spawned);
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

    if (ffmpeg) PIPE_CLOSE(ffmpeg);
    UnloadRenderTexture(target);
    CloseWindow();

    return exportFailed ? 2 : 0;
}