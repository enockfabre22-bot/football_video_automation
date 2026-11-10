#include <raylib.h>
#include <rlgl.h>
#include <Box2D/Box2D.h>
#include <vector>
#include <cmath>
#include <cstdio> 
#include <cstdlib> 

// --- CONFIGURATION ---
#define EXPORT_MODE true
const float WINDOW_SCALE = 0.6f;      
const float PHYSICS_SCALE = 50.0f; // 1 mètre Box2D = 50 pixels
const float DRAW_SCALE = PHYSICS_SCALE * WINDOW_SCALE; 
const int TOTAL_BALLS = 380; // 50 billes par couleur

// Palette "Clean Light" (Design épuré et professionnel)
const Color COL_BG    = Color{ 252, 250, 246, 255 }; // Blanc crème
const Color COL_WALL  = Color{ 25, 25, 25, 255 };    // Noir profond
const Color COLORS[7] = {
    Color{ 255, 40, 60, 255 },  // 0: Rouge
    Color{ 255, 140, 0, 255 },  // 1: Orange
    Color{ 255, 220, 0, 255 },  // 2: Jaune
    Color{ 0, 210, 80, 255 },   // 3: Vert
    Color{ 0, 220, 255, 255 },  // 4: Cyan
    Color{ 0, 102, 255, 255 },  // 5: Bleu
    Color{ 160, 32, 240, 255 }  // 6: Violet
};

Vector2 WorldToScreen(const b2Vec2& p) { return { p.x * DRAW_SCALE, p.y * DRAW_SCALE }; }

// Les clés quantiques pour que les billes traversent leurs filtres respectifs
uint16 GetBallCat(int i) { return 1 << (i + 1); }   
uint16 GetBridgeCat(int i) { return 1 << (i + 8); } 

// --- CLASSES DE L'ENVIRONNEMENT ---

struct BoxWall {
    b2Body* body; b2Vec2 p1, p2; float thickness;
    BoxWall(b2World& world, b2Vec2 start, b2Vec2 end, float thick = 0.15f) {
        this->p1 = start; this->p2 = end; this->thickness = thick;
        b2BodyDef bd;
        bd.position = 0.5f * (start + end);
        bd.angle = atan2(end.y - start.y, end.x - start.x);
        body = world.CreateBody(&bd);
        
        b2PolygonShape box;
        float length = b2Distance(start, end);
        box.SetAsBox(length / 2.0f, thick / 2.0f);
        
        b2FixtureDef fd; fd.shape = &box;
        fd.filter.categoryBits = 0x0001; 
        fd.filter.maskBits = 0xFFFF; // Solide pour tout
        fd.friction = 0.0f; // Magie anti-bouchon : les murs glissent parfaitement !
        body->CreateFixture(&fd);
    }
    void Draw() {
        float length = b2Distance(p1, p2);
        Rectangle rec = { body->GetPosition().x * DRAW_SCALE, body->GetPosition().y * DRAW_SCALE, length * DRAW_SCALE, thickness * DRAW_SCALE };
        Vector2 origin = { rec.width / 2.0f, rec.height / 2.0f };
        DrawRectanglePro(rec, origin, body->GetAngle() * RAD2DEG, COL_WALL);
        DrawCircleV(WorldToScreen(p1), thickness * DRAW_SCALE * 0.5f, COL_WALL);
        DrawCircleV(WorldToScreen(p2), thickness * DRAW_SCALE * 0.5f, COL_WALL);
    }
};

struct SmartFilter {
    b2Body* body; b2Vec2 p1, p2; int typeIndex;
    SmartFilter(b2World& world, b2Vec2 start, b2Vec2 end, int index) : p1(start), p2(end), typeIndex(index) {
        b2BodyDef bd; body = world.CreateBody(&bd);
        b2EdgeShape edge; edge.SetTwoSided(start, end);
        b2FixtureDef fd; fd.shape = &edge;
        
        fd.filter.categoryBits = GetBridgeCat(index);
        // Le pont bloque tout le monde, SAUF la couleur correspondante
        fd.filter.maskBits = 0xFFFF ^ GetBallCat(index); 
        fd.friction = 0.0f;
        body->CreateFixture(&fd);
    }
    void Draw() {
        // Ligne de pont quasi-invisible
        DrawLineEx(WorldToScreen(p1), WorldToScreen(p2), 2.0f, LIGHTGRAY);
        // Indicateur carré esthétique au-dessus (Comme sur ta photo)
        Vector2 mid = WorldToScreen(0.5f * (p1 + p2));
        Rectangle rec = { mid.x - 12, mid.y - 35, 24, 24 };
        DrawRectangleRec(rec, COLORS[typeIndex]);
        DrawRectangleLinesEx(rec, 3.0f, COL_WALL);
    }
};

struct WheelMixer {
    b2Body* body; float radius;
    WheelMixer(b2World& world, float x, float y, float r) : radius(r) {
        b2BodyDef bd; bd.type = b2_kinematicBody; bd.position.Set(x, y);
        body = world.CreateBody(&bd);
        
        for (int i = 0; i < 4; i++) {
            b2PolygonShape spoke;
            spoke.SetAsBox(r * 0.95f, 0.2f, b2Vec2(0,0), (i * 45.0f) * DEG2RAD);
            body->CreateFixture(&spoke, 0.0f);
        }
        b2CircleShape center; center.m_radius = 0.6f;
        body->CreateFixture(&center, 0.0f);
    }
    void Update(float dt) { 
        // Rotation Anti-horaire (-). Les pales poussent les billes vers le trou à droite.
        body->SetTransform(body->GetPosition(), body->GetAngle() - 0.4f * dt); 
    }
    void Draw() {
        b2Vec2 pos = body->GetPosition();
        rlPushMatrix();
        rlTranslatef(pos.x * DRAW_SCALE, pos.y * DRAW_SCALE, 0);
        rlRotatef(body->GetAngle() * RAD2DEG, 0, 0, 1);
        
        for (int i = 0; i < 4; i++) {
            rlPushMatrix(); rlRotatef(i * 45.0f, 0, 0, 1);
            DrawRectanglePro({0, 0, radius * 1.9f * DRAW_SCALE, 0.4f * DRAW_SCALE}, 
                             {radius * 0.95f * DRAW_SCALE, 0.2f * DRAW_SCALE}, 0.0f, COL_WALL);
            rlPopMatrix();
        }
        DrawCircleV({0,0}, 0.6f * DRAW_SCALE, COL_WALL);
        rlPopMatrix();
    }
};

struct Ball {
    b2Body* body; int typeIndex;
    Ball(b2World& world, float x, float y, int type) : typeIndex(type) {
        b2BodyDef bd; bd.type = b2_dynamicBody; bd.position.Set(x, y);
        body = world.CreateBody(&bd);
        b2CircleShape shape; shape.m_radius = 0.18f; // Taille de bille ajustée
        
        b2FixtureDef fd; fd.shape = &shape;
        fd.density = 1.0f; 
        fd.friction = 0.0f; // Magie anti-bouchon : billes ultra-glissantes !
        fd.restitution = 0.1f; 
        
        fd.filter.categoryBits = GetBallCat(type);
        fd.filter.maskBits = 0xFFFF;
        body->CreateFixture(&fd);
    }
    void Draw() {
        Vector2 pos = WorldToScreen(body->GetPosition());
        float r = 0.18f * DRAW_SCALE;
        DrawCircleV(pos, r, COLORS[typeIndex]);
        DrawCircleLines(pos.x, pos.y, r, ColorAlpha(COL_WALL, 0.4f)); 
    }
};

// Fonction pour calculer la hauteur parfaite de la rampe
float GetRampY(float x) {
    float xStart = 35.5f, yStart = 10.0f;
    float xEnd = 3.0f, yEnd = 14.5f;
    return yStart + (x - xStart) * ((yEnd - yStart) / (xEnd - xStart));
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT); // Lissage des courbes et des obliques
    InitWindow(1920 * WINDOW_SCALE, 1080 * WINDOW_SCALE, "Machine de Tri - Parfaite");
    SetTargetFPS(60);

    // --- 0. INITIALISATION DE L'EXPORT (Déplacé avant la boucle) ---
    FILE *ffmpeg = nullptr;
    FILE *notes = nullptr;
    if (EXPORT_MODE) {
        notes = fopen("notes.txt", "w");
        char cmd[512];
        // Correction de l'inversion d'image en retirant le filtre 'vflip'
        sprintf(cmd, "ffmpeg -y -f rawvideo -vcodec rawvideo -pix_fmt rgba -s %dx%d -r 60 -i - -vf scale=1920:1080 -c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p video_youtube.mp4", GetScreenWidth(), GetScreenHeight());
        ffmpeg = popen(cmd, "wb");
    }

    b2World world(b2Vec2(0.0f, 9.81f));

    std::vector<BoxWall> walls;
    std::vector<SmartFilter> filters;
    std::vector<Ball> balls;
    
    // --- 1. LE TAMBOUR (Coque Extérieure Statique) ---
    b2Vec2 drumCenter(29.0f, 5.0f);
    float drumRadius = 5.0f;
    WheelMixer mixer(world, drumCenter.x, drumCenter.y, drumRadius);
    
    int segments = 64;
    for (int i = 0; i < segments; i++) {
        float angle1 = (i * 360.0f / segments);
        float angle2 = ((i + 1) * 360.0f / segments);
        
        // Trou de sortie précis sur le côté droit (entre 335° et 25°)
        if (angle1 < 25.0f || angle1 > 335.0f) continue;
        
        b2Vec2 p1 = drumCenter + b2Vec2(drumRadius * cos(angle1 * DEG2RAD), drumRadius * sin(angle1 * DEG2RAD));
        b2Vec2 p2 = drumCenter + b2Vec2(drumRadius * cos(angle2 * DEG2RAD), drumRadius * sin(angle2 * DEG2RAD));
        walls.emplace_back(world, p1, p2, 0.15f);
    }

    // --- 2. LE TUBE DE CHUTE ---
    walls.emplace_back(world, b2Vec2(35.5f, 2.0f), b2Vec2(35.5f, 10.0f)); // Mur droit extérieur (attrape tout)
    walls.emplace_back(world, b2Vec2(33.7f, 7.5f), b2Vec2(33.7f, 9.55f));  // Petit mur gauche pour guider

    // --- 3. LA RAMPE DE TRI ET LES BACS DISTINCTS ---
    float currentX = 35.5f;
    for (int i = 6; i >= 0; i--) { // De 6 (Violet) à 0 (Rouge) pour correspondre au sens de la pente
        float binCx = 4.5f + (i * 4.5f); 
        float gapRightX = binCx + 0.6f;
        float gapLeftX = binCx - 0.6f;

        // Pente Solide avant le trou
        walls.emplace_back(world, b2Vec2(currentX, GetRampY(currentX)), b2Vec2(gapRightX, GetRampY(gapRightX)));
        
        // Le Filtre Magique
        filters.emplace_back(world, b2Vec2(gapRightX, GetRampY(gapRightX)), b2Vec2(gapLeftX, GetRampY(gapLeftX)), i);
        
        // Les parois du bocal individuel
        walls.emplace_back(world, b2Vec2(gapRightX, GetRampY(gapRightX)), b2Vec2(gapRightX, 20.5f));
        walls.emplace_back(world, b2Vec2(gapLeftX, GetRampY(gapLeftX)), b2Vec2(gapLeftX, 20.5f));
        
        // Le fond du bocal
        walls.emplace_back(world, b2Vec2(gapRightX + 0.05f, 20.5f), b2Vec2(gapLeftX - 0.05f, 20.5f));
        
        currentX = gapLeftX;
    }
    // Fin de la rampe pour sécuriser
    walls.emplace_back(world, b2Vec2(currentX, GetRampY(currentX)), b2Vec2(2.0f, GetRampY(2.0f)));
    walls.emplace_back(world, b2Vec2(2.0f, GetRampY(2.0f)), b2Vec2(2.0f, 20.5f));

    // --- 4. CHARGEMENT DU TAMBOUR ---
    for (int i = 0; i < TOTAL_BALLS; i++) {
        float randX = drumCenter.x + (GetRandomValue(-300, 300) / 100.0f);
        float randY = drumCenter.y + (GetRandomValue(-300, 300) / 100.0f);
        balls.emplace_back(world, randX, randY, i % 7); 
    }

    // --- BOUCLE D'ANIMATION ---
    while (!WindowShouldClose()) {
        float dt = 1.0f / 60.0f;
        
        world.Step(dt, 8, 3);
        mixer.Update(dt);

        BeginDrawing();
        ClearBackground(COL_BG);

        for (auto& f : filters) f.Draw();
        for (auto& w : walls) w.Draw();
        mixer.Draw();
        
        // Les billes sont dessinées en dernier pour passer par-dessus les murs
        for (auto& b : balls) b.Draw();

        EndDrawing();

        // --- 5. ENREGISTREMENT DES FRAMES (À l'intérieur de la boucle) ---
        if (EXPORT_MODE && ffmpeg != nullptr) {
            unsigned char *pixels = rlReadScreenPixels(GetScreenWidth(), GetScreenHeight());
            fwrite(pixels, 4 * GetScreenWidth() * GetScreenHeight(), 1, ffmpeg);
            free(pixels); // Libération de la mémoire allouée par raylib
        }
    }

    // --- 6. FERMETURE PROPRE (À la fin du programme) ---
    if (EXPORT_MODE) {
        if (ffmpeg) pclose(ffmpeg);
        if (notes) fclose(notes);
    }

    CloseWindow();
    return 0;
}