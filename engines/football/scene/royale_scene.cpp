// ============================================================================
//  royale_scene.cpp — « Last club in the ring » (sumo) : N écussons sur une plateforme ronde
//  SANS mur. Chaque choc repousse les deux jetons ; celui qui sort du cercle tombe dans le vide
//  et il est éliminé. Règle visible en une seconde (poussé dehors = éliminé), aucun point de vie.
//  La plateforme rétrécit au fil des éliminations. La simulation (SumoSim) est pure et
//  déterministe : on en joue des dizaines avant le rendu et on garde la plus dramatique.
// ============================================================================
#include "fb_common.h"

namespace {

enum { EV_HIT, EV_ELIM, EV_WALL, EV_WIN, EV_EDGE };
struct Ev { int type, a, b; float x, y, amount; bool crit; };

struct SumoSim {
    World w;
    Rng rng{1};
    int n = 0, alive = 0, winner = -1;
    float t = 0;
    bool done = false;
    float cx = 540, cy = 1035, R0 = 440, R = 440, Rmin = 250;
    float radius = 50;
    std::vector<float> stun, wander, elim_t, fall_vx, fall_vy, edge_cd;
    std::vector<int> pusher;              // dernier à avoir poussé ce jeton (crédité de l'élimination)
    std::vector<std::vector<float>> pair_cd;
    std::vector<Ev> ev;
    std::vector<float> elim_times;
    float duel_start = -1, final_hit = -1;
    int edge_saves = 0;                   // jetons revenus du bord in extremis (frissons)

    void init(uint64_t seed, int count) {
        rng.reseed(seed);
        n = count;
        alive = n;
        sched_duel = std::min(17.0f, 6.0f + n * 0.6f);   // 4 clubs : duel vers 8 s ; 18+ clubs : vers 17 s
        radius = n <= 8 ? 62 : n <= 12 ? 55 : n <= 16 ? 50 : 46;
        w = World();
        w.arena_R = -1;                   // pas de mur : on peut tomber
        w.ball_rest = 0.3f;              // choc mou : c'est la poussée réglée (K) qui projette
        stun.assign(n, 0); wander.assign(n, 0); elim_t.assign(n, -1);
        fall_vx.assign(n, 0); fall_vy.assign(n, 0); edge_cd.assign(n, 0);
        pusher.assign(n, -1);
        pair_cd.assign(n, std::vector<float>(n, -10.0f));
        float ring = R0 * (n <= 8 ? 0.46f : 0.58f);
        float a0 = rng.r(0, 2 * PI_F);
        float spin = rng.chance(0.5f) ? 1.0f : -1.0f;
        for (int i = 0; i < n; ++i) {
            Ball b;
            float a = a0 + i * 2 * PI_F / n;
            float rr = (n > 12 && i % 2) ? ring * 0.6f : ring;
            b.x = cx + std::cos(a) * rr;
            b.y = cy + std::sin(a) * rr;
            // départ en tournant autour du centre (légèrement vers l'intérieur) : personne ne file droit au bord
            float va = a + spin * PI_F * 0.5f + rng.r(-0.25f, 0.25f);   // tous dans le même sens
            float vin = rng.r(0.1f, 0.35f);
            b.vx = (std::cos(va) * (1 - vin) - std::cos(a) * vin) * 440;
            b.vy = (std::sin(va) * (1 - vin) - std::sin(a) * vin) * 440;
            b.r = radius;
            b.m = std::exp(rng.r(-0.45f, 0.45f));     // poids caché : les légers volent plus loin
            b.id = i;
            wander[i] = rng.r(0, 2 * PI_F);
            w.balls.push_back(b);
        }
    }

    // Régulateur de rythme : calendrier cible (1re élimination ~1,3 s, duel final vers 17 s).
    // En avance -> chocs plus doux, en retard -> chocs plus violents. Le hasard décide QUI tombe.
    float sched_first = 1.3f, sched_duel = 17.0f;
    float last_elim = -10;
    // retard (>0) ou avance (<0) sur le calendrier, en nombre d'éliminations
    float behind() const {
        int m = n - 2;
        float expected = 0;
        if (t > sched_first - 0.5f)
            expected = m <= 1 ? 1.0f : 1 + (t - sched_first) / ((sched_duel - sched_first) / (m - 1));
        return clampf(expected, 0, (float)m) - (float)(n - alive);
    }
    float pace_mult() const {
        float calm = t - last_elim < 0.9f ? 0.35f : 1.0f;     // pas de chutes en grappe
        if (alive <= 2) return calm * (0.42f + std::max(0.0f, t - duel_start - 2.0f) * 0.16f);
        int m = n - 2;
        float expected = 0;
        if (t > sched_first - 0.5f)
            expected = m <= 1 ? 1.0f : 1 + (t - sched_first) / ((sched_duel - sched_first) / (m - 1));
        expected = clampf(expected, 0, (float)m);
        float e = expected - (float)(n - alive);
        return calm * clampf(std::exp(0.9f * e) * 0.85f, 0.25f, 4.0f);
    }

    void eliminate(int v) {
        Ball& b = w.balls[v];
        b.on = false;
        alive--;
        elim_t[v] = t;
        fall_vx[v] = b.vx; fall_vy[v] = b.vy;
        elim_times.push_back(t);
        last_elim = t;
        ev.push_back({EV_ELIM, v, pusher[v], b.x, b.y, (float)alive, false});
        if (alive == 2 && duel_start < 0) duel_start = t;
        if (alive <= 1) {
            for (int i = 0; i < n; ++i) if (w.balls[i].on) winner = i;
            final_hit = t;
            done = true;
            ev.push_back({EV_WIN, winner, v, b.x, b.y, 0, false});
        }
    }

    void step() {
        float dt = SIM_DT;
        t += dt;
        // la plateforme rétrécit avec les éliminations, puis avec le temps pendant le duel
        float k = n > 2 ? clamp01((alive - 2.0f) / (n - 2.0f)) : 0;
        float target = Rmin + (R0 - Rmin) * k;
        if (alive <= 2) target = std::max(170.0f, Rmin - (t - duel_start) * 9);
        float shrink = t - last_elim < 1.0f ? 0.0f : (alive <= 2 ? 22.0f : 30.0f);   // pause après une chute
        R += clampf(target - R, -shrink * dt, shrink * dt);
        float speed = 430 + (n - alive) * 9.0f;
        // pilotage : chaque jeton erre en revenant vers le centre (plus fort près du bord)
        for (int i = 0; i < n; ++i) {
            Ball& b = w.balls[i];
            if (!b.on) continue;
            edge_cd[i] = std::max(0.0f, edge_cd[i] - dt);
            if (stun[i] > 0) {                // repoussé : il glisse
                stun[i] -= dt;
                float f = std::exp(-1.1f * dt);
                b.vx *= f; b.vy *= f;
                continue;
            }
            if (alive == 2) {                 // duel final : recul puis charge, comme au sumo
                int o = -1;
                for (int j = 0; j < n; ++j) if (j != i && w.balls[j].on) o = j;
                const Ball& ob = w.balls[o];
                float ph = std::fmod(t - duel_start, 1.35f);
                float ax = b.x - ob.x, ay = b.y - ob.y, al = std::max(1.0f, len2(ax, ay));
                float gx, gy, sp;
                if (ph < 0.6f) { gx = cx + ax / al * R * 0.42f; gy = cy + ay / al * R * 0.42f; sp = 380; }
                else { gx = ob.x; gy = ob.y; sp = 980 * (0.85f + 0.3f * (b.m - 0.6f)); }
                float dx = gx - b.x, dy = gy - b.y, d = std::max(1.0f, len2(dx, dy));
                b.vx = lerpf(b.vx, dx / d * sp, clamp01(6 * dt));
                b.vy = lerpf(b.vy, dy / d * sp, clamp01(6 * dt));
                continue;
            }
            wander[i] += rng.r(-2.2f, 2.2f) * dt;
            float dx = cx - b.x, dy = cy - b.y, d = len2(dx, dy);
            float cw = clamp01(d / R);
            cw = 0.25f + 0.75f * cw * cw;
            float ux = d > 1 ? dx / d : 0, uy = d > 1 ? dy / d : 0;
            float wx = std::cos(wander[i]), wy = std::sin(wander[i]);
            float tx = ux * cw + wx * (1 - cw), ty = uy * cw + wy * (1 - cw);
            float tl = len2(tx, ty);
            if (tl > 1e-3f) { tx /= tl; ty /= tl; }
            float grip = (2.6f + 4.0f * clamp01((d / R - 0.6f) / 0.4f))    // se rattrape près du bord
                         * (1 + 0.8f * clamp01(-behind()) + (t - last_elim < 0.9f ? 1.0f : 0.0f));
            b.vx = lerpf(b.vx, tx * speed, clamp01(grip * dt));
            b.vy = lerpf(b.vy, ty * speed, clamp01(grip * dt));
        }
        w.ball_hits.clear();
        w.wall_hits.clear();
        w.step(dt);
        // chocs : les deux jetons sont projetés (le plus léger / le moins lancé recule le plus)
        for (const auto& h : w.ball_hits) {
            int a = h.a, b = h.b;
            if (t - pair_cd[a][b] < 0.18f) continue;
            pair_cd[a][b] = pair_cd[b][a] = t;
            if (h.speed < 110) continue;
            Ball& A = w.balls[a];
            Ball& B = w.balls[b];
            float K = std::min(alive <= 2 ? 1000.0f : 620.0f, (60 + 0.38f * h.speed) * pace_mult());
            float sa = 0.6f + 0.8f * clamp01(h.att_b / std::max(1.0f, h.att_a + h.att_b));   // A recule si B fonçait
            float sb = 0.6f + 0.8f * clamp01(h.att_a / std::max(1.0f, h.att_a + h.att_b));
            A.vx -= h.nx * K * sa / A.m; A.vy -= h.ny * K * sa / A.m;
            B.vx += h.nx * K * sb / B.m; B.vy += h.ny * K * sb / B.m;
            stun[a] = 0.28f + 0.00025f * h.speed;
            stun[b] = 0.28f + 0.00025f * h.speed;
            int att = h.att_a >= h.att_b ? a : b, vic = att == a ? b : a;
            pusher[vic] = att;
            pusher[att == a ? b : a] = att;
            for (Ball* q : {&A, &B}) {          // pas de projection absurde quand plusieurs chocs s'empilent
                float v = len2(q->vx, q->vy);
                if (v > 1350) { q->vx *= 1350 / v; q->vy *= 1350 / v; }
            }
            bool crit = h.speed > 1.8f * speed;
            ev.push_back({EV_HIT, att, vic, h.x, h.y, h.speed, crit});
        }
        // bord : le centre du jeton dépasse le bord -> chute
        for (int i = 0; i < n; ++i) {
            Ball& b = w.balls[i];
            if (!b.on) continue;
            float d = len2(b.x - cx, b.y - cy);
            if (d > R + radius * 0.15f) {
                eliminate(i);
                if (done) return;
            } else if (d > R - radius * 0.55f && edge_cd[i] <= 0) {
                edge_cd[i] = 0.8f;
                ev.push_back({EV_EDGE, i, -1, b.x, b.y, 0, false});
                if (alive <= 3) edge_saves++;
            }
        }
        if (t > 55 && !done) {   // garde-fou : le plus excentré tombe
            int worst = -1; float wd = -1;
            for (int i = 0; i < n; ++i) if (w.balls[i].on) {
                float d = len2(w.balls[i].x - cx, w.balls[i].y - cy);
                if (d > wd) { wd = d; worst = i; }
            }
            if (worst >= 0) eliminate(worst);
        }
    }

    float score() const {
        if (!done) return -1e9f;
        float s = 0;
        float ideal = sched_duel + 6.0f;
        s -= std::fabs(t - ideal) * 1.2f;
        if (t < ideal - 6 || t > ideal + 8) s -= 12;
        float first = elim_times.empty() ? 99 : elim_times[0];
        if (first > 2.0f) s -= (first - 2.0f) * 5;          // accroche : quelqu'un tombe tout de suite
        if (first < 0.5f) s -= 2;
        int m = (int)elim_times.size() - 1;
        if (m >= 2) {
            float span = elim_times[m - 1] - first;
            float g0 = span / std::max(1, m - 1);
            float var = 0;
            for (int i = 1; i < m; ++i) {
                float gap = elim_times[i] - elim_times[i - 1];
                var += (gap - g0) * (gap - g0);
                if (gap > 3.0f) s -= (gap - 3.0f) * 2.5f;
            }
            s -= var / std::max(1, m - 1) * 3.0f;
        }
        float duel = duel_start >= 0 ? t - duel_start : 0;
        if (duel < 4.5f) s -= (4.5f - duel) * 3;
        if (duel > 9.0f) s -= (duel - 9.0f) * 2.0f;
        s += std::min(edge_saves, 4) * 1.5f;
        return s;
    }
};

class RoyaleScene : public FootballScene {
    SumoSim sim;
    int n = 0;
    float final_hit = -1;
    std::vector<float> flash, death_t, death_x, death_y, edge_flash;
    float left_pop = 0;
    float win_t = -1, duel_t = -1, last_hitstop = -10;
    bool whistle = false, celebrated = false;

public:
    void setup(AnimContext& ctx) override {
        n = (int)teams.size();
        if (n < 2) { printf("[FOOT] Pas assez d'équipes\n"); n = 0; return; }
        start_time = cfg.num("start_time", 0.0f);
        Rng pick(ctx.seed);
        uint64_t best_seed = 1;
        float best = -1e9f;
        int candidates = cfg.integer("candidates", 40);
        for (int c = 0; c < candidates; ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            SumoSim test;
            test.init(s, n);
            while (!test.done) { test.step(); test.ev.clear(); }
            float sc = test.score();
            if (sc > best) { best = sc; best_seed = s; final_hit = test.final_hit; }
        }
        sim.init(best_seed, n);
        printf("[FOOT] Royale (sumo) : %d équipes, meilleur scénario %.1f (fin à %.1f s)\n", n, best, final_hit);
        if (getenv("FB_DEBUG")) {
            SumoSim d;
            d.init(best_seed, n);
            while (!d.done) { d.step(); d.ev.clear(); }
            printf("[FOOT] Chutes :");
            for (float e : d.elim_times) printf(" %.1f", e);
            printf("  | duel %.1f s, %d frôlements\n", d.t - d.duel_start, d.edge_saves);
        }
        ctx.metric("drama_score", best);
        ctx.metric("thumb_time", start_time + final_hit * 0.5f);
        flash.assign(n, 0);
        edge_flash.assign(n, 0);
        death_t.assign(n, -1);
        death_x.assign(n, 0);
        death_y.assign(n, 0);
    }

    float desired_time_scale() override {
        if (final_hit < 0) return 1;
        float d = (float)sim_time - final_hit;
        if (d > -0.55f && d < 0.12f) return 0.22f;
        return 1;
    }

    void sim_step() override { sim.step(); sim_done = sim.done; }

    void after_frame(AnimContext& ctx, float dt) override {
        float t = (float)ctx.time;
        if (!whistle) { whistle = true; ctx.sound("whistle_long", 0.75f); ctx.sound("airhorn", 0.35f); }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        for (int i = 0; i < n; ++i) {
            flash[i] = std::max(0.0f, flash[i] - dt * 4.5f);
            edge_flash[i] = std::max(0.0f, edge_flash[i] - dt * 2.5f);
        }
        left_pop = std::max(0.0f, left_pop - dt * 3);
        if (win_t >= 0 && !celebrated && t >= win_t) {
            celebrated = true;
            const Team& w = teams[sim.winner];
            ctx.sound("fanfare", 0.7f);
            ctx.sound("confetti", 0.8f);
            ctx.sound("crowd_cheer", 0.9f);
            fx.confetti(DW / 2, 940, 90, {w.c1, w.c2, WHITE, FB_GOLD}, 1500, PI_F * 0.45f);
        }
        if (win_t >= 0) {
            float age = t - win_t;
            if (age > 0.3f && (int)(age * 10) != (int)((age - dt) * 10) && age < 3.0f) {
                const Team& w = teams[sim.winner];
                fx.confetti(frng.r(100, 980), -20, 10, {w.c1, w.c2, WHITE, FB_GOLD}, 300, 0.5f, PI_F / 2);
            }
        }
    }

    void handle(AnimContext& ctx, const Ev& e, float t) {
        switch (e.type) {
        case EV_HIT:
            flash[e.b] = 1.0f;
            fx.sparks(e.x, e.y, e.crit ? 24 : 10, mixc(teams[e.a].glow, WHITE, 0.3f), e.crit ? 1200 : 800);
            if (e.crit) {
                sound(ctx, "hit_heavy", 2, 0.8f);
                kick_shake(12);
                if (t - last_hitstop > 1.2f) { hitstop = 0.04f; last_hitstop = t; }
            } else {
                sound(ctx, "hit", 3, 0.28f + 0.35f * clamp01((e.amount - 110) / 900));
                kick_shake(3);
            }
            break;
        case EV_EDGE:
            edge_flash[e.a] = 1;
            if (sim.alive <= 4) ctx.sound("crowd_ooh", 0.4f);
            break;
        case EV_ELIM: {
            const Team& tv = teams[e.a];
            death_t[e.a] = t; death_x[e.a] = e.x; death_y[e.a] = e.y;
            fx.shock(e.x, e.y, sim.radius * 0.5f, sim.radius * 3.5f, 0.6f, tv.glow, 1.0f);
            fx.sparks(e.x, e.y, 26, tv.glow, 1100, 1.2f);
            bool last = e.amount <= 1;
            ctx.sound("whoosh_low", 0.7f);
            ctx.sound(last ? "explode_big" : "splat_big", last ? 1.0f : 0.55f);
            if (!last) ctx.sound(frng.chance(0.5f) ? "crowd_ooh" : "crowd_cheer_small", 0.55f);
            kick_shake(last ? 26 : 12);
            left_pop = 1;
            toast(upper(tv.name) + " FELL OUT", e.a, t, rgba(255, 92, 92));
            if ((int)e.amount == 2) {
                ctx.sound("drumroll", 0.45f);
                toast("FINAL DUEL", -1, t + 0.05f, FB_GOLD);
                duel_t = t;
            }
            break;
        }
        case EV_WIN:
            win_t = t + 0.7f;
            ctx.sound("impact_bass", 0.9f);
            ctx.sound("whistle_final", 0.8f);
            ctx.sound("crowd_roar", 1.0f);
            break;
        }
    }

    // ------------------------------------------------------------------ dessin
    void draw_platform(float t) {
        const float cx = sim.cx, cy = sim.cy, R = sim.R;
        // le vide autour : halo sombre + lueur rouge sous le bord
        g.disc_grad(cx, cy, sim.R0 + 120, rgba(4, 6, 12), rgba(2, 3, 8, 0), 1.2f);
        g.ring_(cx, cy, R + 34, 30, withA(rgba(255, 40, 60), 0.10f + 0.05f * std::sin(t * 4)));
        // épaisseur de la plateforme (tranche) puis le dessus
        g.disc(cx, cy + 26, R, rgba(12, 40, 26));
        g.disc_grad(cx, cy, R, mixc(rgba(36, 140, 74), accent, 0.08f), rgba(18, 82, 44), 1.6f);
        for (int i = 1; i < 8; i += 2)
            g.ring_(cx, cy, R * (i + 0.5f) / 8.0f, R / 16.0f, withA(WHITE, 0.04f));
        g.ring_(cx, cy, std::min(120.0f, R * 0.4f), 2.5f, withA(WHITE, 0.35f));
        g.disc(cx, cy, 7, withA(WHITE, 0.5f));
        // bord : bande de danger blanche et rouge
        int seg = 48;
        for (int i = 0; i < seg; ++i) {
            float a = (i + 0.5f) * 2 * PI_F / seg + t * 0.1f;
            g.arc(cx, cy, R - 9, 7, a, PI_F / seg * 0.82f, i % 2 ? rgba(255, 60, 70) : rgba(250, 250, 255));
        }
    }

    void draw_world(AnimContext& ctx) override {
        if (n == 0) return;
        float t = (float)ctx.time;
        draw_platform(t);
        // éliminés : ils tombent dans le vide (s'éloignent, rapetissent, s'assombrissent)
        for (int i = 0; i < n; ++i) {
            if (death_t[i] < 0) continue;
            float age = t - death_t[i];
            if (age > 0.75f) continue;
            float k = age / 0.75f;
            float dx = death_x[i] - sim.cx, dy = death_y[i] - sim.cy, d = std::max(1.0f, len2(dx, dy));
            float x = death_x[i] + dx / d * 160 * k, y = death_y[i] + dy / d * 160 * k + 220 * k * k;
            TokenFx f; f.scale = 1 - 0.65f * k; f.alpha = 1 - k * k; f.gray = k; f.tilt = age * 520;
            draw_token(g, teams[i], x, y, sim.radius, f);
        }
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            if (!b.on) continue;
            float r = sim.radius;
            if (edge_flash[i] > 0) g.ring_(b.x, b.y, r + 9, 5, withA(rgba(255, 60, 70), edge_flash[i]));
            TokenFx f; f.flash = flash[i] * 0.8f;
            draw_token(g, teams[i], b.x, b.y, r, f);
        }
    }

    void draw_world_glow(AnimContext&) override {
        if (n == 0) return;
        g.ring_(sim.cx, sim.cy, sim.R, 8, withA(rgba(255, 70, 80), 0.55f));
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            if (b.on) draw_token_glow(g, teams[i], b.x, b.y, sim.radius, 0.5f + flash[i] + edge_flash[i]);
        }
    }

    void draw_hud(AnimContext& ctx) override {
        if (n == 0) return;
        float t = (float)ctx.time;
        header(t);
        if (win_t < 0) {
            std::string s;
            if (sim.alive > 3) s = std::to_string(sim.alive) + " " + upper(cfg.str("noun", "clubs")) + " LEFT";
            else if (sim.alive == 3) s = "FINAL 3";
            else s = "FINAL DUEL";
            float k = 1 + 0.25f * ease_out_back(left_pop);
            chip(s, DW / 2, 470, withA(sim.alive == 2 ? FB_GOLD : rgba(255, 255, 255), 0.95f), rgba(12, 14, 24), 1, 34 * k);
            draw_toasts(t, 1580);
        }
        if (win_t >= 0 && t >= win_t) winner_card(t - win_t, sim.winner, cfg.str("win_line", "LAST CLUB STANDING"));
    }
};

}  // namespace

Scene* make_royale_scene() { return new RoyaleScene(); }
