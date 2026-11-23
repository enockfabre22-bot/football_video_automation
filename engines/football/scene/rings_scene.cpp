// ============================================================================
//  rings_scene.cpp — « Escape the Rings » : les jetons sont enfermés au centre d'anneaux
//  concentriques qui tournent, chacun percé d'une ouverture. Le premier jeton qui franchit un
//  anneau le fait voler en éclats ; le premier qui sort du dernier anneau gagne.
// ============================================================================
#include "fb_common.h"

namespace {

enum { RV_BREAK, RV_WIN, RV_WALL, RV_BALL };
struct Ev { int type, a, ring; float x, y, amount; };

const float CX = 540, CY = 1050;

struct RingsSim {
    World w;
    Rng rng{1};
    int n = 0, K = 4;
    float t = 0, radius = 36, speed = 620;
    bool done = false;
    int winner = -1, next_ring = 0;
    std::vector<float> ring_R;
    std::vector<int> breaker;
    std::vector<float> break_t;
    std::vector<Ev> ev;
    float runner_up_gap = 999;
    float gap_min = 0.30f;
    std::vector<float> base_gap;   // distance du 2e au bord quand le vainqueur sort

    void init(uint64_t seed, int count) {
        rng.reseed(seed);
        n = count;
        radius = n <= 6 ? 39 : n <= 8 ? 36 : 33;
        w = World();
        w.ball_rest = 1.0f;
        ring_R.clear();
        base_gap.clear();
        float r0 = 172, r1 = 458;   // écart ~95 px : un jeton tient entre deux anneaux
        for (int k = 0; k < K; ++k) ring_R.push_back(lerpf(r0, r1, (float)k / (K - 1)));
        for (int k = 0; k < K; ++k) {
            ArcWall a;
            a.cx = CX; a.cy = CY; a.R = ring_R[k]; a.t = 7;
            a.gap = std::asin(clampf(radius * 1.22f / a.R, 0, 0.9f)) + 0.015f;   // juste assez large pour passer
            a.gap_ang = rng.r(-PI_F, PI_F);
            a.angv = (k % 2 ? -1.0f : 1.0f) * rng.r(0.75f, 1.25f) * (1.0f + 0.08f * k);
            a.rest = 1.0f;
            a.tag = k;
            w.arcs.push_back(a);
            base_gap.push_back(a.gap);
        }
        breaker.assign(K, -1);
        break_t.assign(K, -1);
        // départ : petit cercle au centre
        for (int i = 0; i < n; ++i) {
            Ball b;
            float a = i * 2 * PI_F / n + rng.r(-0.2f, 0.2f);
            float d = (i % 2 ? 0.45f : 0.75f) * (ring_R[0] - radius - 12);
            if (n <= 4) d = 0.6f * (ring_R[0] - radius - 12);
            b.x = CX + std::cos(a) * d;
            b.y = CY + std::sin(a) * d;
            float va = rng.r(0, 2 * PI_F);
            b.vx = std::cos(va) * speed; b.vy = std::sin(va) * speed;
            b.r = radius; b.id = i;
            w.balls.push_back(b);
        }
    }

    void step() {
        t += SIM_DT;
        // l'ouverture de l'anneau courant s'élargit si personne ne passe (rythme régulier)
        if (next_ring < K) {
            float last = next_ring > 0 ? break_t[next_ring - 1] : 0.0f;
            static const float target_gap[4] = {1.8f, 4.5f, 5.0f, 6.0f};
            float extra = std::max(0.0f, (t - last) - target_gap[std::min(next_ring, 3)]) * 0.05f;
            w.arcs[next_ring].gap = std::min(base_gap[next_ring] + extra, 1.1f);
        }
        w.ball_hits.clear();
        w.wall_hits.clear();
        w.step(SIM_DT);
        w.normalize_speed(speed + t * 4, 3, SIM_DT);
        // contrainte dure : chaque jeton reste dans l'anneau intact suivant, sauf dans l'ouverture
        // (évite qu'une mêlée pousse un jeton à travers un mur)
        if (next_ring < K) {
            const ArcWall& a = w.arcs[next_ring];
            for (auto& b : w.balls) {
                float dx = b.x - CX, dy = b.y - CY;
                float d = len2(dx, dy);
                float lim = a.R - a.t - b.r;
                if (d <= lim || d < 1e-3f) continue;
                float phi = wrap_angle(std::atan2(dy, dx) - a.gap_ang);
                // dans l'ouverture : la collision avec les bords de l'arc suffit
                if (std::fabs(phi) < a.gap - b.r / a.R * 0.5f) continue;
                if (d > a.R + a.t + b.r) continue;   // déjà franchi (traité plus bas)
                float nx = dx / d, ny = dy / d;
                b.x = CX + nx * lim; b.y = CY + ny * lim;
                float vn = b.vx * nx + b.vy * ny;
                if (vn > 0) { b.vx -= 2 * vn * nx; b.vy -= 2 * vn * ny; }
            }
        }
        for (const auto& h : w.wall_hits)
            if (h.speed > 250) ev.push_back({RV_WALL, h.ball, h.tag, h.x, h.y, h.speed});
        for (const auto& h : w.ball_hits)
            if (h.speed > 250) ev.push_back({RV_BALL, h.a, -1, h.x, h.y, h.speed});
        // franchissement de l'anneau courant
        if (next_ring < K) {
            float R = ring_R[next_ring] + 7 + radius;
            for (int i = 0; i < n; ++i) {
                const Ball& b = w.balls[i];
                float d = len2(b.x - CX, b.y - CY);
                if (d <= R) continue;
                breaker[next_ring] = i;
                break_t[next_ring] = t;
                if (next_ring == K - 1) {
                    winner = i;
                    done = true;
                    float best = 1e9f;
                    for (int j = 0; j < n; ++j)
                        if (j != i) best = std::min(best, ring_R[K - 1] - len2(w.balls[j].x - CX, w.balls[j].y - CY));
                    runner_up_gap = best;
                    ev.push_back({RV_WIN, i, next_ring, b.x, b.y, 0});
                } else {
                    w.arcs[next_ring].on = false;
                    ev.push_back({RV_BREAK, i, next_ring, b.x, b.y, 0});
                }
                next_ring++;
                break;
            }
        }
        if (t > 60) { done = true; winner = 0; }
    }

    float score() const {
        if (!done || winner < 0 || t > 59) return -1e9f;
        float s = -std::fabs(t - 18) * 1.3f;
        if (t < 13 || t > 26) s -= 10;
        int distinct = 0;
        std::vector<int> seen;
        for (int b : breaker)
            if (b >= 0 && std::find(seen.begin(), seen.end(), b) == seen.end()) { seen.push_back(b); distinct++; }
        s += distinct * 1.5f;
        if (breaker[K - 2] == winner) s -= 1.5f;        // retournement de dernière seconde = mieux
        float prev = 0;
        for (int k = 0; k < K; ++k) {
            float gap = break_t[k] - prev;
            if (gap > 6.5f) s -= (gap - 6.5f) * 2;
            if (k == 0 && gap > 3.0f) s -= (gap - 3.0f) * 3;   // accroche : 1er anneau vite brisé
            prev = break_t[k];
        }
        if (runner_up_gap < 30) s += 3;                  // le 2e était tout près de sortir
        if (break_t[K - 1] - break_t[K - 2] < 1.5f) s -= 6;   // le dernier anneau mérite son moment
        return s;
    }
};

class RingsScene : public FootballScene {
    RingsSim sim;
    int n = 0;
    float final_hit = -1, win_t = -1, last_wall = -10;
    std::vector<float> break_vis;   // instant vidéo de chaque bris
    std::vector<Color> ring_col;
    bool started = false, celebrated = false;
    int intro_pops = 0;

public:
    void setup(AnimContext& ctx) override {
        n = (int)teams.size();
        Rng pick(ctx.seed);
        uint64_t best_seed = 1;
        float best = -1e9f;
        for (int c = 0; c < cfg.integer("candidates", 40); ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            RingsSim r;
            r.init(s, n);
            while (!r.done) { r.step(); r.ev.clear(); }
            float sc = r.score();
            if (sc > best) { best = sc; best_seed = s; final_hit = r.t; }
        }
        sim.init(best_seed, n);
        printf("[FOOT] Rings : %d équipes, meilleur scénario %.1f (sortie à %.1f s)\n", n, best, final_hit);
        if (getenv("FB_DEBUG")) {
            RingsSim d; d.init(best_seed, n);
            while (!d.done) { d.step(); d.ev.clear(); }
            printf("[FOOT] Bris :");
            for (int k = 0; k < d.K; ++k) printf(" %.1f(%d)", d.break_t[k], d.breaker[k]);
            printf("\n");
        }
        ctx.metric("drama_score", best);
        ctx.metric("thumb_time", start_time + final_hit * 0.4f);
        break_vis.assign(sim.K, -1);
        // dégradé de couleurs néon autour de la teinte du thème
        Vector3 hsvA = ColorToHSV(accent);
        for (int k = 0; k < sim.K; ++k)
            ring_col.push_back(ColorFromHSV(std::fmod(hsvA.x + 360 - k * 55.0f, 360.0f), 0.75f, 1.0f));
    }

    float desired_time_scale() override {
        float d = (float)sim_time - final_hit;
        return (final_hit > 0 && d > -0.7f && d < 0.1f) ? 0.25f : 1.0f;
    }
    void sim_step() override { sim.step(); sim_done = sim.done; }

    void after_frame(AnimContext& ctx, float dt) override {
        float t = (float)ctx.time;
        int pops = std::min(n, (int)(t / 0.05f));
        while (intro_pops < pops && t < 0.6f) { sound(ctx, "pop", 4, 0.3f); intro_pops++; }
        if (!started && t >= start_time - 0.12f) { started = true; ctx.sound("whistle_long", 0.7f); }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        if (win_t >= 0 && !celebrated && t >= win_t) {
            celebrated = true;
            const Team& w = teams[sim.winner];
            ctx.sound("fanfare", 0.7f);
            ctx.sound("confetti", 0.8f);
            ctx.sound("crowd_cheer", 0.9f);
            fx.confetti(DW / 2, 940, 100, {w.c1, w.c2, WHITE, FB_GOLD}, 1500, PI_F * 0.45f);
        }
        if (win_t >= 0) {
            float age = t - win_t;
            if (age > 0.3f && age < 3.0f && (int)(age * 10) != (int)((age - dt) * 10)) {
                const Team& w = teams[sim.winner];
                fx.confetti(frng.r(100, 980), -20, 10, {w.c1, w.c2, WHITE, FB_GOLD}, 300, 0.5f, PI_F / 2);
            }
        }
    }

    void shatter(int k, Color c) {
        float R = sim.ring_R[k];
        for (int i = 0; i < 70; ++i) {
            float a = i * 2 * PI_F / 70;
            float x = CX + std::cos(a) * R, y = CY + std::sin(a) * R;
            fx.shards(x, y, std::cos(a) * 250, std::sin(a) * 250, 1, c, 1.0f);
        }
        fx.shock(CX, CY, R * 0.9f, R * 1.35f, 0.6f, c, 1.0f);
    }

    void handle(AnimContext& ctx, const Ev& e, float t) {
        switch (e.type) {
        case RV_BREAK: {
            break_vis[e.ring] = t;
            shatter(e.ring, ring_col[e.ring]);
            ctx.sound(frng.chance(0.5f) ? "glass_1" : "glass_2", 0.9f);
            ctx.sound("glass_break_1", 0.5f);
            ctx.sound(e.ring >= sim.K - 3 ? "crowd_cheer_small" : "crowd_ooh", 0.5f);
            kick_shake(12);
            int left = sim.K - e.ring - 1;
            char buf[64];
            snprintf(buf, sizeof(buf), "%s BREAKS RING %d", upper(teams[e.a].name).c_str(), e.ring + 1);
            toast(buf, e.a, t, ring_col[e.ring]);
            if (left == 1) { toast("LAST RING!", -1, t + 0.1f, FB_GOLD); ctx.sound("drumroll", 0.45f); }
            break;
        }
        case RV_WIN: {
            break_vis[e.ring] = t;
            shatter(e.ring, ring_col[e.ring]);
            const Team& w = teams[e.a];
            fx.confetti(e.x, e.y, 60, {w.c1, w.c2, WHITE}, 1100, PI_F);
            ctx.sound("glass_1", 1.0f);
            ctx.sound("impact_bass", 0.9f);
            ctx.sound("whistle_final", 0.8f);
            ctx.sound("crowd_roar", 1.0f);
            kick_shake(28);
            win_t = t + 0.5f;
            break;
        }
        case RV_WALL:
            if (t - last_wall > 0.045f) {
                last_wall = t;
                sound(ctx, "bounce", 4, 0.12f + 0.15f * clamp01((e.amount - 250) / 500));
                if (frng.chance(0.25f)) sound(ctx, "glass_small", 2, 0.06f);
            }
            break;
        case RV_BALL:
            sound(ctx, "kick", 3, 0.22f);
            break;
        }
    }

    void draw_world(AnimContext& ctx) override {
        float t = (float)ctx.time;
        g.disc_grad(CX, CY, sim.ring_R.back() + 40, withA(mixc(rgba(10, 14, 28), accent, 0.12f), 0.9f), withA(rgba(6, 8, 16), 0.0f), 1.2f);
        for (int k = 0; k < sim.K; ++k) {
            const ArcWall& a = sim.w.arcs[k];
            bool broken = !a.on || (sim.done && k == sim.K - 1 && break_vis[k] >= 0);
            if (broken) continue;
            Color c = ring_col[k];
            bool current = k == sim.next_ring;
            float half_ap = PI_F - a.gap;
            float center = a.gap_ang + PI_F;
            g.arc(CX, CY, a.R, a.t + 3, center, half_ap, withA(scalec(c, 0.35f), 0.9f));
            g.arc(CX, CY, a.R, a.t, center, half_ap, c);
            g.arc(CX, CY, a.R, 2.0f, center, half_ap, withA(WHITE, current ? 0.85f : 0.45f));
        }
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            if (win_t >= 0 && t > win_t) continue;
            float pop = ease_out_back(clamp01((t - i * 0.05f) / 0.3f), 2.0f);
            TokenFx f; f.scale = pop;
            draw_token(g, teams[i], b.x, b.y, sim.radius, f);
        }
    }

    void draw_world_glow(AnimContext&) override {
        for (int k = 0; k < sim.K; ++k) {
            const ArcWall& a = sim.w.arcs[k];
            if (!a.on || (sim.done && k == sim.K - 1)) continue;
            g.arc(CX, CY, a.R, a.t + 8, a.gap_ang + PI_F, PI_F - a.gap, withA(ring_col[k], k == sim.next_ring ? 1.0f : 0.6f));
        }
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            draw_token_glow(g, teams[i], b.x, b.y, sim.radius, 0.5f);
        }
    }

    void draw_hud(AnimContext& ctx) override {
        float t = (float)ctx.time;
        header(t);
        int left = sim.K - sim.next_ring;
        if (win_t < 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), left == 1 ? "LAST RING" : "%d RINGS LEFT", left);
            chip(buf, DW / 2, 470, withA(WHITE, 0.95f), rgba(12, 14, 24), 1, 34);
        }
        if (win_t < 0 || t < win_t) draw_toasts(t, 1580, 1.8f);
        if (win_t >= 0 && t >= win_t) winner_card(t - win_t, sim.winner, cfg.str("win_line", "escapes first!"));
    }
};

}  // namespace

Scene* make_rings_scene() { return new RingsScene(); }
