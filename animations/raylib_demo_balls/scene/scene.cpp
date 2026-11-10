// Planche de Galton : des billes tombent à travers des rangées de clous et s'empilent dans des bacs.
// Exemple de scène écrite à la main qui respecte le contrat anim_runtime.h.
#include "anim_runtime.h"
#include "box2d/box2d.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

const float PPM = 40.0f;   // pixels (logiques) par mètre

struct Ball {
    b2BodyId body;
    float radius;
    Color color;
};

class GaltonScene : public Scene {
public:
    void init(AnimContext& ctx) override {
        worldW = ctx.width / PPM;    // 27 m
        worldH = ctx.height / PPM;   // 48 m

        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = b2Vec2{0.0f, -12.0f};
        world = b2CreateWorld(&wd);

        b2BodyDef gd = b2DefaultBodyDef();
        ground = b2CreateBody(world, &gd);

        b2ShapeDef sd = b2DefaultShapeDef();
        sd.material.friction = 0.3f;
        sd.material.restitution = 0.2f;

        // Murs et sol
        addSegment(sd, {0.5f, 0.5f}, {worldW - 0.5f, 0.5f});
        addSegment(sd, {0.5f, 0.5f}, {0.5f, worldH});
        addSegment(sd, {worldW - 0.5f, 0.5f}, {worldW - 0.5f, worldH});

        // Entonnoir au sommet
        addSegment(sd, {3.0f, 44.0f}, {worldW / 2 - 1.2f, 40.5f});
        addSegment(sd, {worldW - 3.0f, 44.0f}, {worldW / 2 + 1.2f, 40.5f});

        // Clous en quinconce
        const int rows = 13;
        const float spacing = 1.9f;
        for (int r = 0; r < rows; ++r) {
            float y = 37.5f - r * 1.65f;
            float offset = (r % 2) * spacing / 2;
            for (float x = 1.6f + offset; x < worldW - 1.4f; x += spacing) {
                b2Circle peg = {b2Vec2{x, y}, 0.22f};
                b2CreateCircleShape(ground, &sd, &peg);
                pegs.push_back(b2Vec2{x, y});
            }
        }

        // Séparateurs des bacs
        for (float x = 0.5f + 1.5f; x < worldW - 0.6f; x += 1.5f) {
            addSegment(sd, {x, 0.5f}, {x, 12.0f});
            dividers.push_back(x);
        }
    }

    void update(AnimContext& ctx, float dt) override {
        // Une bille toutes les 3 images pendant les 2/3 de la vidéo
        if (ctx.frame % 3 == 0 && ctx.time < ctx.duration * 0.66 && balls.size() < 450) spawnBall(ctx);

        b2World_Step(world, dt, 4);

        // Son à chaque impact assez fort
        b2ContactEvents ev = b2World_GetContactEvents(world);
        for (int i = 0; i < ev.hitCount; ++i) {
            float speed = ev.hitEvents[i].approachSpeed;
            if (speed < 4.0f) continue;
            ctx.sound(speed > 10.0f ? "hit" : "bounce", std::min(1.0f, speed / 14.0f));
        }

        // Métriques de contrôle (lues par l'application)
        int outOfBounds = 0;
        for (const Ball& b : balls) {
            b2Vec2 p = b2Body_GetPosition(b.body);
            if (!std::isfinite(p.x) || p.x < -1 || p.x > worldW + 1 || p.y < -1) outOfBounds++;
        }
        ctx.metric("balls", (double)balls.size());
        ctx.metric("out_of_bounds", outOfBounds);
    }

    void draw(AnimContext& ctx) override {
        DrawRectangleGradientV(0, 0, ctx.width, ctx.height, Color{14, 16, 34, 255}, Color{40, 18, 60, 255});

        // Textes dessinés en premier : les billes passent par-dessus
        const char* title = "GALTON BOARD";
        DrawText(title, (ctx.width - MeasureText(title, 72)) / 2, 90, 72, Fade(RAYWHITE, 0.9f));
        const char* counter = TextFormat("%d balls", (int)balls.size());
        DrawText(counter, (ctx.width - MeasureText(counter, 44)) / 2, 180, 44, Fade(RAYWHITE, 0.7f));

        for (const b2Vec2& p : pegs) DrawCircleV(toScreen(p, ctx), 0.22f * PPM, Color{200, 205, 230, 255});
        for (float x : dividers)
            DrawLineEx(toScreen({x, 0.5f}, ctx), toScreen({x, 12.0f}, ctx), 4.0f, Color{120, 130, 170, 255});
        DrawLineEx(toScreen({3.0f, 44.0f}, ctx), toScreen({worldW / 2 - 1.2f, 40.5f}, ctx), 6.0f, Color{120, 130, 170, 255});
        DrawLineEx(toScreen({worldW - 3.0f, 44.0f}, ctx), toScreen({worldW / 2 + 1.2f, 40.5f}, ctx), 6.0f, Color{120, 130, 170, 255});

        for (const Ball& b : balls) {
            Vector2 s = toScreen(b2Body_GetPosition(b.body), ctx);
            DrawCircleV(s, b.radius * PPM, b.color);
            DrawCircleV(Vector2{s.x - b.radius * PPM * 0.3f, s.y - b.radius * PPM * 0.3f}, b.radius * PPM * 0.3f, Fade(WHITE, 0.35f));
        }
    }

    void shutdown(AnimContext&) override { b2DestroyWorld(world); }

private:
    b2WorldId world;
    b2BodyId ground;
    float worldW = 27.0f, worldH = 48.0f;
    std::vector<Ball> balls;
    std::vector<b2Vec2> pegs;
    std::vector<float> dividers;

    void addSegment(const b2ShapeDef& sd, b2Vec2 a, b2Vec2 b) {
        b2Segment seg = {a, b};
        b2CreateSegmentShape(ground, &sd, &seg);
    }

    Vector2 toScreen(b2Vec2 p, const AnimContext& ctx) const {
        return Vector2{p.x * PPM, ctx.height - p.y * PPM};
    }

    void spawnBall(AnimContext& ctx) {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = b2Vec2{worldW / 2 + ctx.randf(-2.5f, 2.5f), 46.0f};
        b2BodyId body = b2CreateBody(world, &bd);

        float r = ctx.randf(0.26f, 0.34f);
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.density = 1.0f;
        sd.material.friction = 0.1f;
        sd.material.restitution = 0.25f;
        sd.enableHitEvents = true;
        b2Circle c = {b2Vec2{0.0f, 0.0f}, r};
        b2CreateCircleShape(body, &sd, &c);

        float hue = std::fmod(ctx.time * 40.0f, 360.0f);
        balls.push_back({body, r, ColorFromHSV(hue, 0.75f, 1.0f)});
    }
};

}  // namespace

Scene* create_scene() { return new GaltonScene(); }
