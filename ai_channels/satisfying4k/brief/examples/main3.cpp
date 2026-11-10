// Animation 2D - raylib + Box2D, export natif 4K.
// Concept : LE CŒUR MÉCANIQUE (Symétrie Parfaite & Dark Mode).
// Correction : Les plafonds des rampes convergentes ont été reculés
// pour ne plus traverser les roues géantes et libérer le passage des billes.

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

static constexpr float PIXELS_PER_METER = 60.0f;
static constexpr float DRAW_SCALE = PIXELS_PER_METER;

static constexpr int TOTAL_BALLS = 10600; 
static constexpr int MAX_FEED_ZONE_BALLS = 550; 
static constexpr int MAX_FRAMES = 16000; 
static constexpr int SPAWN_INTERVAL_FRAMES = 4; 

static constexpr bool EXPORT_VIDEO = true;
static constexpr bool USE_MSAA = true;

static constexpr float BALL_RADIUS = 0.25f;  
static constexpr float DEBRIS_RADIUS = 0.08f; 
static constexpr float GRAVITY = 9.81f;
static constexpr float FIXED_DT = 1.0f / 60.0f;

enum CollisionCategory {
    CAT_WALL     = 0x0001,
    CAT_SHREDDER = 0x0002,
    CAT_BALL     = 0x0004,
    CAT_DEBRIS   = 0x0008
};

// Palette "Dark Mode Industrial"
static const Color COL_BG       = {15, 18, 22, 255}; 
static const Color COL_WALL     = {40, 45, 52, 255};    
static const Color COL_WHEEL    = {25, 28, 33, 255};
static const Color COL_SHREDDER = {70, 75, 85, 255};
static const Color COL_BASIN    = {30, 35, 42, 255};

static const Color COLORS[7] = {
    {255, 45, 85, 255},   
    {255, 149, 0, 255},   
    {255, 214, 10, 255},  
    {48, 209, 88, 255},   
    {100, 210, 255, 255}, 
    {10, 132, 255, 255},  
    {191, 90, 242, 255}   
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

// ---------------- MÉCANISMES ----------------

struct MegaWheel {
    b2Body* body = nullptr;
    float radius = 9.0f; 
    int blades = 12;
    float speed; 

    MegaWheel(b2World& world, b2Vec2 center, float s) : speed(s) {
        b2BodyDef bd;
        bd.type = b2_kinematicBody; 
        bd.position = center;
        body = world.CreateBody(&bd);

        b2CircleShape centerCircle;
        centerCircle.m_radius = 1.5f;
        b2FixtureDef fdCenter;
        fdCenter.shape = &centerCircle;
        fdCenter.filter.categoryBits = CAT_WALL;
        body->CreateFixture(&fdCenter);

        for (int i = 0; i < blades; ++i) {
            b2PolygonShape blade;
            float angle = (i * 360.0f / blades) * DEG2RAD;
            float hx = (radius - 1.5f) * 0.5f;
            float hy = 0.25f; 
            float dist = 1.5f + hx;
            b2Vec2 localCenter(std::cos(angle) * dist, std::sin(angle) * dist);
            blade.SetAsBox(hx, hy, localCenter, angle);

            b2FixtureDef fdBlade;
            fdBlade.shape = &blade;
            fdBlade.friction = 0.8f; 
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

        DrawCircle(0, 0, 1.5f * DRAW_SCALE, COL_WHEEL);
        for (int i = 0; i < blades; ++i) {
            rlPushMatrix();
            rlRotatef(i * (360.0f / blades), 0.0f, 0.0f, 1.0f);
            Rectangle rec = { 1.5f * DRAW_SCALE, -0.25f * DRAW_SCALE, (radius - 1.5f) * DRAW_SCALE, 0.5f * DRAW_SCALE };
            DrawRectangleRec(rec, COL_WHEEL);
            rlPopMatrix();
        }
        DrawCircle(0, 0, 0.6f * DRAW_SCALE, COL_BG); 
        DrawCircle(0, 0, 0.2f * DRAW_SCALE, COL_WALL); 
        rlPopMatrix();
    }
};

struct AggressiveShredder {
    b2Body* body = nullptr;
    float radius;
    int teethCount;
    float speed;
    
    AggressiveShredder(b2World& world, b2Vec2 center, float r, int teeth, float s) : radius(r), teethCount(teeth), speed(s) {
        b2BodyDef bd;
        bd.type = b2_kinematicBody;
        bd.position = center;
        body = world.CreateBody(&bd);

        float coreRadius = radius * 0.70f;
        b2CircleShape circle;
        circle.m_radius = coreRadius;
        
        b2FixtureDef fd;
        fd.shape = &circle;
        fd.friction = 0.9f; 
        fd.filter.categoryBits = CAT_SHREDDER;
        fd.filter.maskBits = CAT_BALL | CAT_WALL; 
        body->CreateFixture(&fd);

        float toothLength = radius * 0.35f;
        for (int i = 0; i < teeth; ++i) {
            b2PolygonShape tooth;
            float angle = (i * 360.0f / teeth) * DEG2RAD;
            
            b2Vec2 vertices[4];
            float dist = coreRadius;
            vertices[0].Set(std::cos(angle - 0.2f) * dist, std::sin(angle - 0.2f) * dist); 
            vertices[1].Set(std::cos(angle + 0.2f) * dist, std::sin(angle + 0.2f) * dist); 
            
            float tipAngle = speed > 0 ? angle + 0.25f : angle - 0.25f;
            vertices[2].Set(std::cos(tipAngle) * (radius), std::sin(tipAngle) * (radius)); 
            vertices[3].Set(std::cos(angle) * (radius), std::sin(angle) * (radius)); 
            
            tooth.Set(vertices, 4);

            b2FixtureDef tfd;
            tfd.shape = &tooth;
            tfd.friction = 0.9f;
            tfd.filter.categoryBits = CAT_SHREDDER;
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

        float coreRadius = radius * 0.70f;
        DrawCircle(0, 0, coreRadius * DRAW_SCALE, COL_SHREDDER);

        for (int i = 0; i < teethCount; ++i) {
            float angle = (i * 360.0f / teethCount) * DEG2RAD;
            float tipAngle = speed > 0 ? angle + 0.25f : angle - 0.25f;
            
            Vector2 v1 = { std::cos(angle - 0.2f) * coreRadius * DRAW_SCALE, std::sin(angle - 0.2f) * coreRadius * DRAW_SCALE };
            Vector2 v2 = { std::cos(angle + 0.2f) * coreRadius * DRAW_SCALE, std::sin(angle + 0.2f) * coreRadius * DRAW_SCALE };
            Vector2 v3 = { std::cos(tipAngle) * radius * DRAW_SCALE, std::sin(tipAngle) * radius * DRAW_SCALE };
            
            DrawTriangle(v1, v2, v3, COL_SHREDDER);
            DrawTriangle(v3, v2, v1, COL_SHREDDER); 
        }
        
        DrawCircle(0, 0, 0.6f * DRAW_SCALE, COL_BG);
        DrawCircle(0, 0, 0.2f * DRAW_SCALE, {90, 95, 105, 255});
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
        
        bd.linearDamping = debris ? 1.0f : 0.02f; 
        bd.angularDamping = debris ? 1.0f : 0.05f;
        body = world.CreateBody(&bd);

        b2CircleShape shape;
        shape.m_radius = radius;

        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = debris ? 2.5f : 1.0f; 
        fd.friction = debris ? 0.6f : 0.2f; 
        fd.restitution = debris ? 0.0f : 0.1f; 
        
        if (debris) {
            fd.filter.categoryBits = CAT_DEBRIS;
            fd.filter.maskBits = CAT_WALL | CAT_BALL | CAT_DEBRIS;
        } else {
            fd.filter.categoryBits = CAT_BALL;
            fd.filter.maskBits = 0xFFFF;
        }
        
        fixture = body->CreateFixture(&fd);
    }

    void Draw() const {
        const Vector2 p = WorldToScreen(body->GetPosition());
        const float r = radius * DRAW_SCALE;
        Color c = COLORS[typeIndex];

        if (!isDebris) {
            DrawCircleV(p, r * 1.4f, ColorAlpha(c, 0.2f));
            DrawCircleV(p, r * 1.1f, ColorAlpha(c, 0.5f));
            DrawCircleV(p, r, c);
            DrawCircleV({p.x - r * 0.25f, p.y - r * 0.25f}, r * 0.3f, {255,255,255,180});
        } else {
            DrawCircleV(p, r * 1.5f, c);
        }
    }
};

// ---------------- DESSIN SCÈNE ----------------

static void DrawScene(const std::vector<BoxWall>& walls,
                      const MegaWheel& wheelLeft,
                      const MegaWheel& wheelRight,
                      const AggressiveShredder& shredder1,
                      const AggressiveShredder& shredder2,
                      const std::vector<Ball>& balls,
                      int spawnedBalls) {
    ClearBackground(COL_BG);
    DrawRectangle(0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT, COL_BG);

    for (const auto& ball : balls) ball.Draw();
    
    wheelLeft.Draw();
    wheelRight.Draw();
    shredder1.Draw();
    shredder2.Draw();
    for (const auto& wall : walls) wall.Draw();

    DrawText("MECHANICAL HEART", 120, 90, 48, {180,185,190,255});
    DrawText(TextFormat("ENTITIES  %d / %d", spawnedBalls, TOTAL_BALLS), 120, 150, 26, {100,105,115,255});
}

// ---------------- PROGRAMME PRINCIPAL ----------------

int main() {
    SetConfigFlags(USE_MSAA ? FLAG_MSAA_4X_HINT : 0);
    InitWindow(1280, 720, "Mechanical Heart - 4K");
    SetTargetFPS(60);

    b2World world(b2Vec2(0.0f, GRAVITY));
    world.SetAllowSleeping(true);
    world.SetWarmStarting(true);
    world.SetContinuousPhysics(true);

    std::vector<BoxWall> walls;
    std::vector<Ball> balls;
    balls.reserve(TOTAL_BALLS * 5); 

    // 1. Les Léviathans (Roues de 18m)
    b2Vec2 wLeftCenter(12.0f, 18.0f);
    b2Vec2 wRightCenter(52.0f, 18.0f);
    
    MegaWheel wheelLeft(world, wLeftCenter, 1.2f);    // Tourne maintenant vers la droite (Horaire)
    MegaWheel wheelRight(world, wRightCenter, -1.2f); // Tourne maintenant vers la gauche (Anti-horaire) 

    float statorRad = 9.2f;
    int segments = 20; 
    
    // Stator Gauche
    float startAngleL = 180.0f * DEG2RAD; 
    float endAngleL = -20.0f * DEG2RAD;   
    for (int i = 0; i < segments; ++i) {
        float a1 = startAngleL + (endAngleL - startAngleL) * i / segments;
        float a2 = startAngleL + (endAngleL - startAngleL) * (i + 1) / segments;
        b2Vec2 p1(wLeftCenter.x + std::cos(a1) * statorRad, wLeftCenter.y + std::sin(a1) * statorRad);
        b2Vec2 p2(wLeftCenter.x + std::cos(a2) * statorRad, wLeftCenter.y + std::sin(a2) * statorRad);
        walls.emplace_back(world, p1, p2, 0.4f, COL_WALL);
    }
    
    // Stator Droit
    float startAngleR = 0.0f * DEG2RAD;   
    float endAngleR = 200.0f * DEG2RAD;   
    for (int i = 0; i < segments; ++i) {
        float a1 = startAngleR + (endAngleR - startAngleR) * i / segments;
        float a2 = startAngleR + (endAngleR - startAngleR) * (i + 1) / segments;
        b2Vec2 p1(wRightCenter.x + std::cos(a1) * statorRad, wRightCenter.y + std::sin(a1) * statorRad);
        b2Vec2 p2(wRightCenter.x + std::cos(a2) * statorRad, wRightCenter.y + std::sin(a2) * statorRad);
        walls.emplace_back(world, p1, p2, 0.4f, COL_WALL);
    }

    // 2. Les Entonnoirs de Spawn
    walls.emplace_back(world, b2Vec2(1.0f, -5.0f), b2Vec2(2.8f, 18.0f), 0.4f, COL_WALL);
    walls.emplace_back(world, b2Vec2(9.0f, -5.0f), b2Vec2(6.0f, 11.0f), 0.4f, COL_WALL);
    
    walls.emplace_back(world, b2Vec2(63.0f, -5.0f), b2Vec2(61.2f, 18.0f), 0.4f, COL_WALL);
    walls.emplace_back(world, b2Vec2(55.0f, -5.0f), b2Vec2(58.0f, 11.0f), 0.4f, COL_WALL);

    // 3. Les Rampes Convergentes (CORRIGÉES)
    // Le plafond gauche est reculé à X=21.0f pour ne plus traverser les pales
    walls.emplace_back(world, b2Vec2(20.5f, 14.8f), b2Vec2(27.5f, 17.5f), 0.5f, COL_WALL); // Sol
    walls.emplace_back(world, b2Vec2(21.0f, 11.0f), b2Vec2(27.0f, 14.0f), 0.4f, COL_WALL); // Plafond reculé
    
    // Le plafond droit est reculé à X=43.0f pour ne plus traverser les pales
    walls.emplace_back(world, b2Vec2(43.5f, 14.8f), b2Vec2(36.5f, 17.5f), 0.5f, COL_WALL); // Sol
    walls.emplace_back(world, b2Vec2(43.0f, 11.0f), b2Vec2(37.0f, 14.0f), 0.4f, COL_WALL); // Plafond reculé

    // 4. Le Broyeur Agressif Central
    b2Vec2 s1Center(30.0f, 19.0f);
    b2Vec2 s2Center(34.0f, 19.0f);
    float sRadius = 3.2f;
    AggressiveShredder shredder1(world, s1Center, sRadius, 7, 2.5f);  
    AggressiveShredder shredder2(world, s2Center, sRadius, 7, -2.5f); 
    
    walls.emplace_back(world, b2Vec2(26.0f, 17.0f), b2Vec2(26.0f, 21.0f), 0.4f, COL_WALL);
    walls.emplace_back(world, b2Vec2(38.0f, 17.0f), b2Vec2(38.0f, 21.0f), 0.4f, COL_WALL);

    // 5. La Cuve
    walls.emplace_back(world, b2Vec2(24.0f, 21.0f), b2Vec2(24.0f, 35.0f), 0.5f, COL_BASIN);
    walls.emplace_back(world, b2Vec2(40.0f, 21.0f), b2Vec2(40.0f, 35.0f), 0.5f, COL_BASIN);
    walls.emplace_back(world, b2Vec2(24.0f, 35.0f), b2Vec2(40.0f, 35.0f), 0.5f, COL_BASIN);

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
            float offset = GetRandomValue(-40, 40) / 100.0f;
            balls.emplace_back(world, 3.5f + offset, -3.0f, spawned % 7);       
            balls.emplace_back(world, 60.5f + offset, -3.0f, (spawned+1) % 7); 
            spawned += 2;
        }

        world.Step(FIXED_DT, 12, 8);
        wheelLeft.Update();
        wheelRight.Update();
        shredder1.Update();
        shredder2.Update();

        // ---------------- DÉCLENCHEUR DE FRACTURATION ----------------
        std::vector<Ball> newDebris; 

        for (auto it = balls.begin(); it != balls.end(); ) {
            if (!it->isDebris) {
                const b2Vec2 p = it->body->GetPosition();
                
                if (p.x > 29.0f && p.x < 35.0f && p.y > 18.0f) {
                    
                    b2Vec2 vel = it->body->GetLinearVelocity();
                    int t = it->typeIndex;
                    world.DestroyBody(it->body);
                    it = balls.erase(it);

                    for (int i = 0; i < 4; ++i) {
                        float ox = (GetRandomValue(-100, 100) / 100.0f) * 0.15f;
                        float oy = (GetRandomValue(-100, 100) / 100.0f) * 0.15f;
                        newDebris.emplace_back(world, p.x + ox, p.y + oy, t, true);
                        
                        b2Vec2 dVel = vel;
                        dVel.y += GetRandomValue(30, 80) / 10.0f; 
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
        DrawScene(walls, wheelLeft, wheelRight, shredder1, shredder2, balls, spawned);
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