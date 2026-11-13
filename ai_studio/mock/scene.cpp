// Scène renvoyée par le FAUX client Claude (tests du studio sans clé API).
// Planche de Galton qui s'adapte à n'importe quel format (4K paysage ou portrait), sans son ni texte.
#include "anim_runtime.h"
#include "anim_box2d.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct Ball {
    b2Body* body;
    float radius;
    Color color;
};

class MockGaltonScene : public Scene {
public:
    void init(AnimContext& ctx) override {
        ppm = ctx.height / 48.0f;          // le monde fait toujours 48 m de haut
        worldW = ctx.width / ppm;
        world = new b2World(b2Vec2(0.0f, -10.0f));
        anim_watch_world(ctx, world, [this](b2Vec2 p) { return toScreen(p); });

        b2BodyDef gd;
        ground = world->CreateBody(&gd);
        addSegment({0.5f, 0.5f}, {worldW - 0.5f, 0.5f});
        addSegment({0.5f, 0.5f}, {0.5f, 47.5f});
        addSegment({worldW - 0.5f, 0.5f}, {worldW - 0.5f, 47.5f});
        addSegment({1.0f, 46.0f}, {worldW / 2 - 1.4f, 40.5f});
        addSegment({worldW - 1.0f, 46.0f}, {worldW / 2 + 1.4f, 40.5f});

        for (int r = 0; r < 13; ++r) {
            float y = 37.5f - r * 1.65f;
            float offset = (r % 2) * 0.95f;
            for (float x = 1.6f + offset; x < worldW - 1.4f; x += 1.9f) {
                b2CircleShape peg;
                peg.m_p.Set(x, y);
                peg.m_radius = 0.22f;
                b2FixtureDef fd;
                fd.shape = &peg;
                fd.friction = 0.3f;
                fd.restitution = 0.2f;
                ground->CreateFixture(&fd);
                pegs.push_back(b2Vec2(x, y));
            }
        }
        for (float x = 2.0f; x < worldW - 0.6f; x += 1.5f) {
            addSegment({x, 0.5f}, {x, 12.0f});
            dividers.push_back(x);
        }

    }

    void update(AnimContext& ctx, float dt) override {
        // 400 billes réparties sur les 3/4 de la vidéo
        int every = std::max(1, (int)(ctx.total_frames * 0.75f / 400.0f));
        if (ctx.frame % every == 0 && balls.size() < 400)
            spawnBall(ctx, worldW / 2 + ctx.randf(-2.5f, 2.5f), 46.5f, (int)balls.size());
        world->Step(dt, 8, 3);
        // Fin naturelle (teste ctx.finish) : la vidéo s'arrête à 70 % de la durée maximale
        if (ctx.time >= ctx.duration * 0.7) ctx.finish();
    }

    void draw(AnimContext& ctx) override {
        DrawRectangleGradientV(0, 0, ctx.width, ctx.height, Color{14, 16, 34, 255}, Color{40, 18, 60, 255});
        const float lw = ppm * 0.18f;
        for (const b2Vec2& p : pegs) DrawCircleV(toScreen(p), 0.22f * ppm, Color{200, 205, 230, 255});
        for (float x : dividers) DrawLineEx(toScreen({x, 0.5f}), toScreen({x, 12.0f}), lw, Color{120, 130, 170, 255});
        DrawLineEx(toScreen({1.0f, 46.0f}), toScreen({worldW / 2 - 1.4f, 40.5f}), lw, Color{120, 130, 170, 255});
        DrawLineEx(toScreen({worldW - 1.0f, 46.0f}), toScreen({worldW / 2 + 1.4f, 40.5f}), lw, Color{120, 130, 170, 255});
        for (const Ball& b : balls) DrawCircleV(toScreen(b.body->GetPosition()), b.radius * ppm, b.color);
    }

    void shutdown(AnimContext&) override { delete world; }

private:
    b2World* world = nullptr;
    b2Body* ground = nullptr;
    float ppm = 40.0f, worldW = 27.0f;
    std::vector<Ball> balls;
    std::vector<b2Vec2> pegs;
    std::vector<float> dividers;

    Vector2 toScreen(b2Vec2 p) const { return Vector2{p.x * ppm, (48.0f - p.y) * ppm}; }

    void addSegment(b2Vec2 a, b2Vec2 b) {
        b2EdgeShape edge;
        edge.SetTwoSided(a, b);
        b2FixtureDef fd;
        fd.shape = &edge;
        fd.friction = 0.3f;
        ground->CreateFixture(&fd);
    }

    void spawnBall(AnimContext& ctx, float x, float y, int i) {
        b2BodyDef bd;
        bd.type = b2_dynamicBody;
        bd.position.Set(x, y);
        b2Body* body = world->CreateBody(&bd);
        float r = ctx.randf(0.26f, 0.32f);
        b2CircleShape c;
        c.m_radius = r;
        b2FixtureDef fd;
        fd.shape = &c;
        fd.density = 1.0f;
        fd.friction = 0.1f;
        fd.restitution = 0.2f;
        body->CreateFixture(&fd);
        balls.push_back({body, r, ColorFromHSV(std::fmod(i * 0.9f, 360.0f), 0.75f, 1.0f)});
    }
};

}  // namespace

Scene* create_scene() { return new MockGaltonScene(); }
