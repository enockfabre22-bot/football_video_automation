// ============================================================================
//  anim_box2d.h — Aide Box2D 2.4 pour les scènes : surveillance automatique du monde.
//
//  Dans init(), après avoir créé le monde :
//      anim_watch_world(ctx, world, [this](b2Vec2 p) { return toScreen(p); });
//  Le runtime échantillonne alors le monde 2 fois par seconde et écrit dans metrics.json
//  le nombre de corps, ceux hors écran / perdus / invalides et la part de corps en mouvement.
//  L'application s'en sert pour détecter automatiquement les défauts (objets qui sortent du
//  circuit, machine bloquée, explosion numérique...).
// ============================================================================
#pragma once

#include "anim_runtime.h"
#include <box2d/box2d.h>

#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>

// "En mouvement" = le corps s'est déplacé à l'écran depuis l'échantillon précédent (0,5 s avant)
// d'au moins min_move_ratio × la plus grande dimension du canevas (0,2 % ≈ 8 px en 4K).
inline void anim_watch_world(AnimContext& ctx, b2World* world,
                             std::function<Vector2(b2Vec2)> to_screen,
                             float min_move_ratio = 0.002f) {
    AnimContext* c = &ctx;
    auto previous = std::make_shared<std::unordered_map<const b2Body*, Vector2>>();
    ctx.watcher = [c, world, to_screen, min_move_ratio, previous]() {
        AnimWatchSample s;
        const float w = (float)c->width, h = (float)c->height;
        const float margin = 0.02f * std::fmax(w, h);
        const float min_move = min_move_ratio * std::fmax(w, h);
        std::unordered_map<const b2Body*, Vector2> current;
        for (b2Body* b = world->GetBodyList(); b; b = b->GetNext()) {
            if (b->GetType() != b2_dynamicBody) continue;
            s.bodies++;
            b2Vec2 p = b->GetPosition();
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(b->GetAngle())) {
                s.invalid++;
                continue;
            }
            Vector2 q = to_screen(p);
            current[b] = q;
            if (q.x < -w || q.y < -h || q.x > 2 * w || q.y > 2 * h) s.lost++;
            else if (q.x < -margin || q.y < -margin || q.x > w + margin || q.y > h + margin) s.offscreen++;
            auto it = previous->find(b);
            if (it != previous->end()) {   // seuls les corps déjà présents à l'échantillon précédent comptent
                s.tracked++;
                float dx = q.x - it->second.x, dy = q.y - it->second.y;
                if (dx * dx + dy * dy >= min_move * min_move) s.moving++;
            }
        }
        previous->swap(current);
        return s;
    };
}
