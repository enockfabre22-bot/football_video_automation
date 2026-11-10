// Planche de Galton : des billes tombent à travers des rangées de clous et s'empilent dans des bacs.
// Exemple de scène écrite à la main qui respecte le contrat anim_runtime.h (Box2D 2.4, API C++).
#include "anim_runtime.h"
#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

const float PPM = 40.0f;   // pixels (logiques) par mètre

struct Ball {
    b2Body* body;
    float radius;
    Color color;
};

// Récupère la vitesse d'impact de chaque nouveau contact (pour déclencher les sons)
class ImpactListener : public b2ContactListener {
public:
    std::vector<float> impacts;
    void BeginContact(b2Contact* contact) override {
        b2WorldManifold wm;
        contact->GetWorldManifold(&wm);
        b2Body* a = contact->GetFixtureA()->GetBody();
        b2Body* b = contact->GetFixtureB()->GetBody();
        b2Vec2 p = wm.points[0];
        b2Vec2 rel = b->GetLinearVelocityFromWorldPoint(p) - a->GetLinearVelocityFromWorldPoint(p);
        impacts.push_back(std::fabs(b2Dot(rel, wm.normal)));
    }
};

class GaltonScene : public Scene {
public:
    void init(AnimContext& ctx) override {
        worldW = ctx.width / PPM;    // 27 m
        worldH = ctx.height / PPM;   // 48 m

        world = new b2World(b2Vec2(0.0f, -12.0f));
        world->SetContactListener(&listener);

        b2BodyDef gd;
        ground = world->CreateBody(&gd);

        // Murs et sol
        addSegment({0.5f, 0.5f}, {worldW - 0.5f, 0.5f});
        addSegment({0.5f, 0.5f}, {0.5f, worldH});
        addSegment({worldW - 0.5f, 0.5f}, {worldW - 0.5f, worldH});

        // Entonnoir au sommet
        addSegment({3.0f, 44.0f}, {worldW / 2 - 1.2f, 40.5f});
        addSegment({worldW - 3.0f, 44.0f}, {worldW / 2 + 1.2f, 40.5f});

        // Clous en quinconce
        const int rows = 13;
        const float spacing = 1.9f;
        for (int r = 0; r < rows; ++r) {
            float y = 37.5f - r * 1.65f;
            float offset = (r % 2) * spacing / 2;
            for (float x = 1.6f + offset; x < worldW - 1.4f; x += spacing) {
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

        // Séparateurs des bacs
        for (float x = 0.5f + 1.5f; x < worldW - 0.6f; x += 1.5f) {
            addSegment({x, 0.5f}, {x, 12.0f});
            dividers.push_back(x);
        }
    }

    void update(AnimContext& ctx, float dt) override {
        // Une bille toutes les 3 images pendant les 2/3 de la vidéo
        if (ctx.frame % 3 == 0 && ctx.time < ctx.duration * 0.66 && balls.size() < 450) spawnBall(ctx);

        listener.impacts.clear();
        world->Step(dt, 8, 3);

        // Son à chaque impact assez fort
        for (float speed : listener.impacts) {
            if (speed < 4.0f) continue;
            ctx.sound(speed > 10.0f ? "hit" : "bounce", std::min(1.0f, speed / 14.0f));
        }

        // Métriques de contrôle (lues par l'application)
        int outOfBounds = 0;
        for (const Ball& b : balls) {
            b2Vec2 p = b.body->GetPosition();
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
            Vector2 s = toScreen(b.body->GetPosition(), ctx);
            DrawCircleV(s, b.radius * PPM, b.color);
            DrawCircleV(Vector2{s.x - b.radius * PPM * 0.3f, s.y - b.radius * PPM * 0.3f}, b.radius * PPM * 0.3f, Fade(WHITE, 0.35f));
        }
    }

    void shutdown(AnimContext&) override { delete world; }

private:
    b2World* world = nullptr;
    b2Body* ground = nullptr;
    ImpactListener listener;
    float worldW = 27.0f, worldH = 48.0f;
    std::vector<Ball> balls;
    std::vector<b2Vec2> pegs;
    std::vector<float> dividers;

    void addSegment(b2Vec2 a, b2Vec2 b) {
        b2EdgeShape edge;
        edge.SetTwoSided(a, b);
        b2FixtureDef fd;
        fd.shape = &edge;
        fd.friction = 0.3f;
        fd.restitution = 0.2f;
        ground->CreateFixture(&fd);
    }

    Vector2 toScreen(b2Vec2 p, const AnimContext& ctx) const {
        return Vector2{p.x * PPM, ctx.height - p.y * PPM};
    }

    void spawnBall(AnimContext& ctx) {
        b2BodyDef bd;
        bd.type = b2_dynamicBody;
        bd.position.Set(worldW / 2 + ctx.randf(-2.5f, 2.5f), 46.0f);
        b2Body* body = world->CreateBody(&bd);

        float r = ctx.randf(0.26f, 0.34f);
        b2CircleShape c;
        c.m_radius = r;
        b2FixtureDef fd;
        fd.shape = &c;
        fd.density = 1.0f;
        fd.friction = 0.1f;
        fd.restitution = 0.25f;
        body->CreateFixture(&fd);

        float hue = std::fmod((float)ctx.time * 40.0f, 360.0f);
        balls.push_back({body, r, ColorFromHSV(hue, 0.75f, 1.0f)});
    }
};

}  // namespace

Scene* create_scene() { return new GaltonScene(); }
