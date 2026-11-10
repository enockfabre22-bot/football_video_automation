// Animation 2D - raylib + Box2D, export natif 4K.
// Concept : VRAIE PRESSE HYDRAULIQUE (ZÉRO TRICHE).
// Le piston dégage largement l'ouverture pour laisser entrer les billes,
// puis les écrase physiquement contre une grille pour les réduire en poudre.

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

// L'écran fait 38.4m de large x 21.6m de haut
static constexpr float PIXELS_PER_METER = 100.0f;
static constexpr float DRAW_SCALE = PIXELS_PER_METER;

static constexpr int TOTAL_BALLS = 1800; 
static constexpr int MAX_FEED_ZONE_BALLS = 400; 
static constexpr int MAX_FRAMES = 12000; // ~3 min 20s
static constexpr int SPAWN_INTERVAL_FRAMES = 3; 

static constexpr bool EXPORT_VIDEO = true;
static constexpr bool USE_MSAA = true;

static constexpr float BALL_RADIUS = 0.16f;  
static constexpr float DEBRIS_RADIUS = 0.05f; 
static constexpr float GRAVITY = 9.81f;
static constexpr float FIXED_DT = 1.0f / 60.0f;

// Catégories de collision pour éviter les bugs de physique (Ghosting)
enum CollisionCategory {
    CAT_WALL   = 0x0001,
    CAT_PISTON = 0x0002,
    CAT_BALL   = 0x0004,
    CAT_DEBRIS = 0x0008
};

// Palette "Industrial Clean"
static const Color COL_BG    = {245, 245, 245, 255};
static const Color COL_WALL  = {35, 35, 35, 255};
static const Color COL_PEG   = {180, 180, 180, 255};
static const Color COL_BASIN = {185, 185, 185, 255};
static const Color COL_PISTON= {50, 50, 50, 255};
static const Color COL_PLATE = {200, 45, 45, 255};
static const Color COLORS[7] = {
    {255, 59, 48, 255},
    {255, 149, 0, 255},
    {255, 204, 0, 255},
    {52, 199, 89, 255},
    {90, 200, 250, 255},
    {0, 122, 255, 255},
    {175, 82, 222, 255}
};

static Vector2 WorldToScreen(const b2Vec2& p) {
    return {p.x * DRAW_SCALE, p.y * DRAW_SCALE};
}

// ---------------- GEOMETRIE ----------------

struct BoxWall {
    b2Body* body = nullptr;
    b2Vec2 p1, p2;
    float thickness;
    Color color;

    BoxWall(b2World& world, b2Vec2 start, b2Vec2 end, float thick = 0.3f, Color col = COL_WALL) {
        p1 = start; p2 = end; thickness = thick; color = col;

        b2BodyDef bd;
        bd.position = 0.5f * (start + end);
        bd.angle = std::atan2(end.y - start.y, end.x - start.x);
        body = world.CreateBody(&bd);

        b2PolygonShape box;
        const float length = b2Distance(start, end);
        box.SetAsBox(length * 0.5f, thick * 0.5f);

        b2FixtureDef fd;
        fd.shape = &box;
        fd.friction = 0.1f;
        fd.restitution = 0.0f;
        fd.filter.categoryBits = CAT_WALL;
        fd.filter.maskBits = 0xFFFF;
        body->CreateFixture(&fd);
    }

    void Draw() const {
        const float length = b2Distance(p1, p2);
        Rectangle rec = {
            body->GetPosition().x * DRAW_SCALE,
            body->GetPosition().y * DRAW_SCALE,
            length * DRAW_SCALE,
            thickness * DRAW_SCALE
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
    float radius = 0.15f;

    Peg(b2World& world, float x, float y, float r = 0.15f) : radius(r) {
        b2BodyDef bd; bd.position.Set(x, y);
        body = world.CreateBody(&bd);
        b2CircleShape shape; shape.m_radius = radius;

        b2FixtureDef fd;
        fd.shape = &shape;
        fd.friction = 0.25f;
        fd.restitution = 0.05f;
        fd.filter.categoryBits = CAT_WALL;
        body->CreateFixture(&fd);
    }

    void Draw() const {
        Vector2 p = WorldToScreen(body->GetPosition());
        DrawCircleV(p, radius * DRAW_SCALE, COL_PEG);
    }
};

// --- LE PISTON MASSIF ---
struct HydraulicPress {
    b2Body* body = nullptr;
    float width = 4.36f; // Rentre parfaitement dans le silo de 4.4m
    float height = 20.0f; // Très grand pour boucher l'ouverture même en position basse
    
    // Le piston oscille entre Y = -4.0 (Haut) et Y = 3.8 (Bas)
    float midY = -0.1f; 
    float amplitude = 3.9f; 
    float speed = 2.0f; 

    HydraulicPress(b2World& world, float x) {
        b2BodyDef bd;
        bd.type = b2_kinematicBody; 
        bd.position.Set(x, midY);
        body = world.CreateBody(&bd);

        b2PolygonShape box;
        box.SetAsBox(width * 0.5f, height * 0.5f);

        b2FixtureDef fd;
        fd.shape = &box;
        fd.friction = 0.1f;
        fd.filter.categoryBits = CAT_PISTON;
        // Le piston écrase uniquement les grosses billes (pour éviter de glitcher avec les débris fins)
        fd.filter.maskBits = CAT_BALL | CAT_WALL; 
        body->CreateFixture(&fd);
    }

    void Update(float time) {
        float currentY = midY + std::sin(time * speed) * amplitude;
        float velY = std::cos(time * speed) * speed * amplitude;
        body->SetLinearVelocity(b2Vec2(0.0f, velY));
        body->SetTransform(b2Vec2(body->GetPosition().x, currentY), 0.0f);
    }

    void Draw() const {
        b2Vec2 pos = body->GetPosition();

        Rectangle rec = { pos.x * DRAW_SCALE, pos.y * DRAW_SCALE, width * DRAW_SCALE, height * DRAW_SCALE };
        Vector2 origin = { rec.width * 0.5f, rec.height * 0.5f };
        DrawRectanglePro(rec, origin, 0.0f, COL_PISTON);

        Rectangle inner = { pos.x * DRAW_SCALE, pos.y * DRAW_SCALE, (width - 0.2f) * DRAW_SCALE, (height - 0.2f) * DRAW_SCALE };
        DrawRectanglePro(inner, { inner.width * 0.5f, inner.height * 0.5f }, 0.0f, {60, 60, 60, 255});

        Rectangle plate = { pos.x * DRAW_SCALE, (pos.y + height * 0.5f - 0.15f) * DRAW_SCALE, width * DRAW_SCALE, 0.3f * DRAW_SCALE };
        DrawRectanglePro(plate, { plate.width * 0.5f, plate.height * 0.5f }, 0.0f, COL_PLATE);
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
        
        bd.linearDamping = debris ? 0.8f : 0.02f; 
        bd.angularDamping = debris ? 0.8f : 0.05f;
        body = world.CreateBody(&bd);

        b2CircleShape shape;
        shape.m_radius = radius;

        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = debris ? 2.5f : 1.0f; 
        fd.friction = debris ? 0.7f : 0.1f; 
        fd.restitution = 0.0f; 
        
        if (debris) {
            fd.filter.categoryBits = CAT_DEBRIS;
            // Les débris ignorent le piston pour tomber proprement à travers la grille sans glitcher
            fd.filter.maskBits = CAT_WALL | CAT_BALL | CAT_DEBRIS;
        } else {
            fd.filter.categoryBits = CAT_BALL;
            // Les grosses billes entrent en collision avec tout (incluant le piston)
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
            DrawCircleV(p, r * 1.3f, COLORS[typeIndex]);
        }
    }
};

// ---------------- DESSIN SCÈNE ----------------

static void DrawScene(const std::vector<BoxWall>& walls,
                      const std::vector<Peg>& pegs,
                      const HydraulicPress& press,
                      const std::vector<Ball>& balls,
                      int spawnedBalls, int frame) {
    ClearBackground(COL_BG);
    DrawRectangle(0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT, COL_BG);

    for (const auto& peg : pegs) peg.Draw();
    
    // Ordre de dessin
    for (const auto& ball : balls) ball.Draw();
    press.Draw();
    for (const auto& wall : walls) wall.Draw();

    DrawText("REAL HYDRAULIC PRESS", 90, 70, 34, {65,65,65,255});
    DrawText(TextFormat("BALLS  %d / %d", spawnedBalls, TOTAL_BALLS), 90, 118, 23, {105,105,105,255});
    DrawLine(90, 160, 560, 160, {205,205,205,255});
}

// ---------------- PROGRAMME PRINCIPAL ----------------

int main() {
    SetConfigFlags(USE_MSAA ? FLAG_MSAA_4X_HINT : 0);
    InitWindow(1280, 720, "Real Hydraulic Press - 4K");
    SetTargetFPS(60);

    // Le centre exact de l'écran 4K (38.4m / 2)
    const float centerX = 19.2f; 
    
    b2World world(b2Vec2(0.0f, GRAVITY));
    world.SetAllowSleeping(true);
    world.SetWarmStarting(true);
    world.SetContinuousPhysics(true);

    std::vector<BoxWall> walls;
    std::vector<Peg> pegs;
    std::vector<Ball> balls;
    balls.reserve(TOTAL_BALLS * 6); 

    // Paramètres verticaux
    float topY = 1.0f;
    float funnelY = 7.5f;
    float gridY = 14.0f;
    float basinBottom = 21.0f;

    // 1. L'Entonnoir Géant (Rentre parfaitement dans l'écran)
    walls.emplace_back(world, b2Vec2(centerX - 10.0f, topY), b2Vec2(centerX - 2.2f, funnelY), 0.5f, COL_WALL);
    walls.emplace_back(world, b2Vec2(centerX + 10.0f, topY), b2Vec2(centerX + 2.2f, funnelY), 0.5f, COL_WALL);

    // 2. Le Silo de Compression (Largeur : 4.4m)
    walls.emplace_back(world, b2Vec2(centerX - 2.2f, funnelY), b2Vec2(centerX - 2.2f, gridY), 0.5f, COL_WALL);
    walls.emplace_back(world, b2Vec2(centerX + 2.2f, funnelY), b2Vec2(centerX + 2.2f, gridY), 0.5f, COL_WALL);

    // 3. Les Clous de l'entonnoir (Plinko)
    for (int row = 0; row < 4; ++row) {
        const int cols = 10 + (row % 2);
        const float startX = centerX - (cols * 1.5f) * 0.5f + 0.75f;
        for (int col = 0; col < cols; ++col) {
            float px = startX + col * 1.5f;
            // Ne place des clous que sur les parois, pas au-dessus du piston
            if (px < centerX - 2.5f || px > centerX + 2.5f) {
                pegs.emplace_back(world, px, 2.5f + row * 1.2f);
            }
        }
    }

    // 4. Le Tamis (Grille renforcée)
    float barWidth = 0.08f;
    float gapWidth = 0.18f; // Les débris (0.10) passent, les billes (0.32) bloquent
    for (float x = centerX - 2.2f; x < centerX + 2.2f; x += (barWidth + gapWidth)) {
        float bw = std::min(barWidth, (centerX + 2.2f) - x);
        if (bw > 0.01f) {
            walls.emplace_back(world, b2Vec2(x, gridY), b2Vec2(x + bw, gridY), 0.4f, COL_WALL);
        }
    }

    // 5. La Cuve de réception (Largeur 10m)
    walls.emplace_back(world, b2Vec2(centerX - 5.0f, gridY), b2Vec2(centerX - 2.2f, gridY), 0.5f, COL_WALL);
    walls.emplace_back(world, b2Vec2(centerX + 2.2f, gridY), b2Vec2(centerX + 5.0f, gridY), 0.5f, COL_WALL);
    
    walls.emplace_back(world, b2Vec2(centerX - 5.0f, gridY), b2Vec2(centerX - 5.0f, basinBottom), 0.4f, COL_BASIN);
    walls.emplace_back(world, b2Vec2(centerX + 5.0f, gridY), b2Vec2(centerX + 5.0f, basinBottom), 0.4f, COL_BASIN);
    walls.emplace_back(world, b2Vec2(centerX - 5.0f, basinBottom), b2Vec2(centerX + 5.0f, basinBottom), 0.4f, COL_BASIN);

    // Le Piston de la machine
    HydraulicPress press(world, centerX);

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

        // Les billes spawnent en haut de l'écran, au-dessus du piston
        if (spawned < TOTAL_BALLS && inFeedZone < MAX_FEED_ZONE_BALLS && frame % SPAWN_INTERVAL_FRAMES == 0) {
            // Spawn large pour utiliser l'entonnoir
            float spawnX = centerX + (GetRandomValue(-800, 800) / 100.0f);
            balls.emplace_back(world, spawnX, 1.0f, spawned % 7);
            ++spawned;
        }

        float time = frame * FIXED_DT;
        world.Step(FIXED_DT, 12, 8);
        press.Update(time);

        // ---------------- DÉCLENCHEUR DE FRACTURATION RÉALISTE ----------------
        float pistonBottom = press.body->GetPosition().y + press.height * 0.5f;
        float pistonVel = press.body->GetLinearVelocity().y;

        std::vector<Ball> newDebris; 

        for (auto it = balls.begin(); it != balls.end(); ) {
            if (!it->isDebris) {
                const b2Vec2 p = it->body->GetPosition();
                
                // ZÉRO TRICHE : L'écrasement ne s'active que si la bille est dans le silo
                // (Y > 7.5) ET que le piston métallique l'écrase physiquement !
                if (p.x > centerX - 2.2f && p.x < centerX + 2.2f && p.y > 7.5f) {
                    
                    if (pistonVel > 0.0f && (p.y - BALL_RADIUS) < pistonBottom) {
                        
                        b2Vec2 vel = it->body->GetLinearVelocity();
                        int t = it->typeIndex;
                        world.DestroyBody(it->body);
                        it = balls.erase(it);

                        // La bille éclate en 6 débris
                        for (int i = 0; i < 6; ++i) {
                            float ox = (GetRandomValue(-100, 100) / 100.0f) * 0.12f;
                            float oy = (GetRandomValue(-100, 100) / 100.0f) * 0.12f;
                            newDebris.emplace_back(world, p.x + ox, p.y + oy, t, true);
                            
                            // Propulse légèrement la poudre vers le bas à travers la grille
                            b2Vec2 dVel = vel;
                            dVel.y += GetRandomValue(20, 50) / 10.0f; 
                            newDebris.back().body->SetLinearVelocity(dVel);
                        }
                        continue; 
                    }
                }
            }
            ++it;
        }

        for (const auto& d : newDebris) {
            balls.push_back(d);
        }
        // ---------------------------------------------------------

        BeginTextureMode(target);
        DrawScene(walls, pegs, press, balls, spawned, frame);
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