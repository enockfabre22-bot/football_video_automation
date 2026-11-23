// ============================================================================
//  royale_scene.cpp — « Last Club Standing » : N jetons dans une arène qui rétrécit,
//  chaque choc enlève des points de vie à celui qui encaisse ; le dernier survivant gagne.
//  La simulation (RoyaleSim) est pure et déterministe : avant le rendu, on en joue des
//  dizaines avec des graines différentes et on garde la plus dramatique.
// ============================================================================
#include "fb_common.h"

namespace {

enum { EV_HIT, EV_ELIM, EV_HEART_SPAWN, EV_HEART_TAKE, EV_WALL, EV_WIN };
struct Ev { int type, a, b; float x, y, amount; bool crit; };

struct RoyaleSim {
    World w;
    Rng rng{1};
    int n = 0, alive = 0, winner = -1;
    float t = 0;
    bool done = false;
    float cx = 540, cy = 1035, R0 = 430, R = 430, Rmin = 235;
    float radius = 50;
    float dmg_crowded = 0.30f;
    std::vector<float> hp, last_hit_t, elim_t;
    std::vector<float> armor;      // multiplicateur de dégâts reçus (caché) : étale les éliminations
    std::vector<int> killer;
    std::vector<std::vector<float>> pair_cd;
    bool heart_on = false;
    float hx = 0, hy = 0, heart_timer = 4.0f;
    std::vector<Ev> ev;
    // suivi pour la note dramatique
    std::vector<float> elim_times;
    float duel_start = -1, final_hit = -1;
    int duel_lead = -1, lead_changes = 0;

    void init(uint64_t seed, int count) {
        rng.reseed(seed);
        n = count;
        alive = n;
        radius = n <= 8 ? 64 : n <= 12 ? 57 : n <= 16 ? 51 : 47;
        w = World();
        w.arena_x = cx; w.arena_y = cy; w.arena_R = R0;
        w.ball_rest = 1.0f;
        hp.assign(n, 100.0f);
        last_hit_t.assign(n, -10.0f);
        elim_t.assign(n, -1.0f);
        killer.assign(n, -1);
        pair_cd.assign(n, std::vector<float>(n, -10.0f));
        armor.assign(n, 1.0f);
        for (int i = 0; i < n; ++i) armor[i] = std::exp(rng.r(-0.7f, 0.7f));
        float ring = R0 * (n <= 8 ? 0.50f : 0.60f);
        float a0 = rng.r(0, 2 * PI_F);
        for (int i = 0; i < n; ++i) {
            Ball b;
            float a = a0 + i * 2 * PI_F / n;
            float rr = (n > 12 && i % 2) ? ring * 0.62f : ring;
            b.x = cx + std::cos(a) * rr;
            b.y = cy + std::sin(a) * rr;
            float va = rng.r(0, 2 * PI_F);
            b.vx = std::cos(va) * 560;
            b.vy = std::sin(va) * 560;
            b.r = radius;
            b.id = i;
            w.balls.push_back(b);
        }
    }

    float speed() const { return 560 + (n - alive) * 11.0f; }

    // Régulateur de rythme : calendrier cible (1re élimination ~1,8 s, puis régulières jusqu'au
    // duel final vers 18 s). En avance -> dégâts réduits, en retard -> dégâts augmentés.
    // Le hasard (et la résistance cachée de chaque équipe) décide toujours QUI tombe.
    float sched_first = 1.8f, sched_duel = 18.0f;
    float pace_mult() const {
        if (alive <= 2) return 1.0f + std::max(0.0f, t - duel_start - 3.5f) * 0.55f;
        int m = n - 2;
        float expected = 0;
        if (t > sched_first - 0.6f)
            expected = m <= 1 ? 1.0f : 1 + (t - sched_first) / ((sched_duel - sched_first) / (m - 1));
        expected = clampf(expected, 0, (float)m);
        float e = expected - (float)(n - alive);
        return clampf(std::exp(1.15f * e) * 0.55f, 0.06f, 6.0f);
    }

    void spawn_heart() {
        for (int tries = 0; tries < 30; ++tries) {
            float a = rng.r(0, 2 * PI_F), d = rng.r(0, R * 0.62f);
            float x = cx + std::cos(a) * d, y = cy + std::sin(a) * d;
            bool ok = true;
            for (const auto& b : w.balls)
                if (b.on && len2(b.x - x, b.y - y) < b.r + 60) { ok = false; break; }
            if (ok) { hx = x; hy = y; heart_on = true; ev.push_back({EV_HEART_SPAWN, -1, -1, x, y, 0, false}); return; }
        }
    }

    void eliminate(int v, int by) {
        Ball& b = w.balls[v];
        b.on = false;
        alive--;
        elim_t[v] = t;
        killer[v] = by;
        elim_times.push_back(t);
        ev.push_back({EV_ELIM, v, by, b.x, b.y, (float)alive, false});
        if (alive == 2 && duel_start < 0) duel_start = t;
        if (alive <= 1) {
            for (int i = 0; i < n; ++i) if (w.balls[i].on) winner = i;
            final_hit = t;
            done = true;
            ev.push_back({EV_WIN, winner, v, b.x, b.y, 0, false});
        }
    }

    void step() {
        t += SIM_DT;
        // l'arène se resserre avec les éliminations, puis avec le temps (finale garantie)
        float k = n > 2 ? clamp01((alive - 2.0f) / (n - 2.0f)) : 0;
        float target = Rmin + (R0 - Rmin) * k;
        if (alive <= 2) target = std::max(190.0f, Rmin - (t - duel_start) * 6);
        if (t > 30) target = std::min(target, std::max(140.0f, 190 - (t - 30) * 9));
        R += clampf(target - R, -70 * SIM_DT, 70 * SIM_DT);
        w.arena_R = R;
        w.ball_hits.clear();
        w.wall_hits.clear();
        w.step(SIM_DT);
        w.normalize_speed(speed(), 3.0f, SIM_DT);

        for (const auto& h : w.ball_hits) {
            int a = h.a, b = h.b;
            if (t - pair_cd[a][b] < 0.22f) continue;
            pair_cd[a][b] = pair_cd[b][a] = t;
            if (h.speed < 120) continue;
            // celui qui fonçait le plus vers l'autre inflige les dégâts
            int att = h.att_a >= h.att_b ? a : b;
            int vic = att == a ? b : a;
            bool crit = h.speed > 1.75f * speed();
            float dmg = (6 + 0.021f * h.speed) * (crit ? 1.45f : 1.0f) * pace_mult();
            if (alive > 2) dmg *= armor[vic];
            hp[vic] -= dmg;
            last_hit_t[vic] = t;
            ev.push_back({EV_HIT, att, vic, h.x, h.y, dmg, crit});
            if (hp[vic] <= 0) { hp[vic] = 0; eliminate(vic, att); if (done) return; }
        }
        for (const auto& h : w.wall_hits)
            if (h.speed > 250) ev.push_back({EV_WALL, h.ball, -1, h.x, h.y, h.speed, false});

        // cœur de soin
        if (!heart_on && alive >= 2) {
            heart_timer -= SIM_DT;
            if (heart_timer <= 0) spawn_heart();
        }
        if (heart_on) {
            for (int i = 0; i < n; ++i) {
                const Ball& b = w.balls[i];
                if (!b.on || len2(b.x - hx, b.y - hy) > b.r + 30) continue;
                float heal = std::min(alive > 2 ? 32.0f : 20.0f, 100 - hp[i]);
                hp[i] += heal;
                heart_on = false;
                heart_timer = rng.r(3.5f, 6.0f);
                ev.push_back({EV_HEART_TAKE, i, -1, hx, hy, heal, false});
                break;
            }
            // le cœur reste dans l'arène qui rétrécit
            float d = len2(hx - cx, hy - cy);
            if (d > R - 40) { hx = cx + (hx - cx) * (R - 40) / d; hy = cy + (hy - cy) * (R - 40) / d; }
        }

        // suivi du meneur pendant le duel final
        if (alive == 2) {
            int lead = -1; float best = -1;
            for (int i = 0; i < n; ++i) if (w.balls[i].on && hp[i] > best) { best = hp[i]; lead = i; }
            if (duel_lead >= 0 && lead != duel_lead) lead_changes++;
            duel_lead = lead;
        }
        if (t > 55 && !done) {   // garde-fou
            int best = -1;
            for (int i = 0; i < n; ++i) if (w.balls[i].on && (best < 0 || hp[i] > hp[best])) best = i;
            for (int i = 0; i < n; ++i) if (w.balls[i].on && i != best) { eliminate(i, best); if (done) break; }
        }
    }

    float score() const {
        if (!done) return -1e9f;
        float s = 0;
        s -= std::fabs(t - 24.5f) * 1.2f;
        if (t < 19 || t > 32) s -= 12;
        float first = elim_times.empty() ? 99 : elim_times[0];
        if (first > 2.8f) s -= (first - 2.8f) * 4;          // accroche : une élimination tout de suite
        if (first < 0.8f) s -= 2;
        // éliminations régulières jusqu'au duel final (pas de tir groupé, pas de temps mort)
        int m = (int)elim_times.size() - 1;                  // la dernière = coup final
        if (m >= 2) {
            float span = elim_times[m - 1] - first;
            float g0 = span / std::max(1, m - 1);
            float var = 0;
            for (int i = 1; i < m; ++i) {
                float gap = elim_times[i] - elim_times[i - 1];
                var += (gap - g0) * (gap - g0);
                if (gap > 3.2f) s -= (gap - 3.2f) * 2.5f;
            }
            s -= var / std::max(1, m - 1) * 3.0f;
        }
        float duel = duel_start >= 0 ? t - duel_start : 0;
        if (duel < 4.5f) s -= (4.5f - duel) * 3;
        if (duel > 8.0f) s -= (duel - 8.0f) * 2.0f;
        s += std::min(lead_changes, 4) * 1.6f;
        if (winner >= 0) {
            if (hp[winner] < 35) s += 3;
            else if (hp[winner] > 70) s -= 2.5f;
        }
        return s;
    }
};

class RoyaleScene : public FootballScene {
    RoyaleSim sim;
    int n = 0;
    float final_hit = -1;
    std::vector<float> flash, death_t, death_x, death_y, hp_shown;
    float heart_t0 = -1, left_pop = 0;
    int last_alive = 0;
    float win_t = -1, duel_t = -1, last_hitstop = -10, last_dmg_text = -10;
    int intro_pops = 0;
    bool started = false, celebrated = false;

public:
    void setup(AnimContext& ctx) override {
        n = (int)teams.size();
        if (n < 2) { printf("[FOOT] Pas assez d'équipes\n"); n = 0; return; }
        // recherche du scénario le plus dramatique
        Rng pick(ctx.seed);
        uint64_t best_seed = 1;
        float best = -1e9f;
        int candidates = cfg.integer("candidates", 40);
        for (int c = 0; c < candidates; ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            RoyaleSim test;
            test.init(s, n);
            while (!test.done) { test.step(); test.ev.clear(); }
            float sc = test.score();
            if (sc > best) { best = sc; best_seed = s; final_hit = test.final_hit; }
        }
        sim.init(best_seed, n);
        printf("[FOOT] Royale : %d équipes, meilleur scénario %.1f (fin à %.1f s de simulation)\n", n, best, final_hit);
        if (getenv("FB_DEBUG")) {
            RoyaleSim d;
            d.init(best_seed, n);
            while (!d.done) { d.step(); d.ev.clear(); }
            printf("[FOOT] Éliminations :");
            for (float e : d.elim_times) printf(" %.1f", e);
            printf("  | duel %.1f s, %d changements de meneur\n", d.t - d.duel_start, d.lead_changes);
        }
        ctx.metric("drama_score", best);
        ctx.metric("thumb_time", start_time + final_hit * 0.5f);
        flash.assign(n, 0);
        death_t.assign(n, -1);
        death_x.assign(n, 0);
        death_y.assign(n, 0);
        hp_shown.assign(n, 100);
        last_alive = n;
    }

    float desired_time_scale() override {
        if (final_hit < 0) return 1;
        float d = (float)sim_time - final_hit;
        if (d > -0.55f && d < 0.12f) return 0.22f;
        return 1;
    }

    void sim_step() override {
        sim.step();
        sim_done = sim.done;
    }

    void after_frame(AnimContext& ctx, float dt) override {
        float t = (float)ctx.time;
        // introduction : apparition des jetons, coup de sifflet
        int pops_target = std::min(8, (int)(t / 0.07f));
        while (intro_pops < pops_target && t < 0.8f) { sound(ctx, "pop", 4, 0.35f); intro_pops++; }
        if (!started && t >= start_time - 0.12f) {
            started = true;
            ctx.sound("whistle_long", 0.75f);
            ctx.sound("airhorn", 0.35f);
        }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        for (int i = 0; i < n; ++i) {
            flash[i] = std::max(0.0f, flash[i] - dt * 4.5f);
            hp_shown[i] = lerpf(hp_shown[i], sim.hp[i], clamp01(dt * 9));
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
            // pluie de confettis continue aux couleurs du vainqueur
            if (age > 0.3f && (int)(age * 10) != (int)((age - dt) * 10) && age < 3.0f) {
                const Team& w = teams[sim.winner];
                fx.confetti(frng.r(100, 980), -20, 10, {w.c1, w.c2, WHITE, FB_GOLD}, 300, 0.5f, PI_F / 2);
            }
        }
    }

    void handle(AnimContext& ctx, const Ev& e, float t) {
        switch (e.type) {
        case EV_HIT: {
            flash[e.b] = 1.0f;
            const Team& tv = teams[e.b];
            fx.sparks(e.x, e.y, e.crit ? 26 : 12, mixc(teams[e.a].glow, WHITE, 0.3f), e.crit ? 1300 : 900);
            char buf[32];
            snprintf(buf, sizeof(buf), "-%d", (int)std::round(e.amount));
            if (e.amount >= 9 && t - last_dmg_text > 0.18f) {   // seuls les gros coups s'affichent (lisibilité)
                last_dmg_text = t;
                fx.text(e.crit ? std::string("CRIT ") + buf : buf, sim.w.balls[e.b].x,
                        sim.w.balls[e.b].y - sim.radius - 30, e.crit ? FB_GOLD : rgba(255, 90, 90), e.crit ? 60 : 46);
            }
            if (e.crit) {
                sound(ctx, "hit_heavy", 2, 0.85f);
                ctx.sound("kick_hard_1", 0.6f);
                kick_shake(14);
                if (t - last_hitstop > 1.2f) { hitstop = 0.05f; last_hitstop = t; }
            } else {
                sound(ctx, "hit", 3, 0.32f + 0.4f * clamp01(e.amount / 30));
                sound(ctx, "kick", 3, 0.35f);
                kick_shake(4);
            }
            (void)tv;
            break;
        }
        case EV_ELIM: {
            const Team& tv = teams[e.a];
            death_t[e.a] = t; death_x[e.a] = e.x; death_y[e.a] = e.y;
            fx.shock(e.x, e.y, sim.radius * 0.5f, sim.radius * 4.5f, 0.7f, tv.glow, 1.0f);
            fx.confetti(e.x, e.y, 46, {tv.c1, tv.c2, WHITE}, 950, PI_F);
            fx.sparks(e.x, e.y, 34, tv.glow, 1400, 1.4f);
            bool last = e.amount <= 1;
            ctx.sound(last ? "explode_big" : (frng.chance(0.5f) ? "explode_1" : "explode_2"), last ? 1.0f : 0.8f);
            if (!last) ctx.sound(frng.chance(0.5f) ? "crowd_ooh" : "crowd_cheer_small", 0.55f);
            kick_shake(last ? 30 : 18);
            left_pop = 1;
            toast(upper(tv.name) + " OUT", e.a, t, rgba(255, 92, 92));
            if ((int)e.amount == 2) {
                ctx.sound("drumroll", 0.45f);
                toast("FINAL DUEL", -1, t + 0.05f, FB_GOLD);
                duel_t = t;
            }
            break;
        }
        case EV_HEART_SPAWN:
            heart_t0 = t;
            ctx.sound("pop_3", 0.4f);
            fx.shock(e.x, e.y, 10, 90, 0.5f, rgba(255, 80, 120), 1);
            break;
        case EV_HEART_TAKE: {
            char buf[32];
            snprintf(buf, sizeof(buf), "+%d", (int)std::round(e.amount));
            fx.text(buf, e.x, e.y - 60, HP_GREEN, 54);
            fx.sparks(e.x, e.y, 18, rgba(255, 90, 140), 700);
            ctx.sound("powerup", 0.5f);
            ctx.sound("coin_1", 0.4f);
            break;
        }
        case EV_WALL:
            sound(ctx, "bounce", 4, 0.12f + 0.18f * clamp01((e.amount - 250) / 600));
            break;
        case EV_WIN: {
            win_t = t + 0.45f;
            ctx.sound("impact_bass", 0.9f);
            ctx.sound("whistle_final", 0.8f);
            ctx.sound("crowd_roar", 1.0f);
            break;
        }
        }
        if (e.type == EV_WIN) {
            // fanfare et confettis au moment où la carte apparaît (programmés via win_t)
        }
    }

    // ------------------------------------------------------------------ dessin
    void draw_arena(float t) {
        const float cx = sim.cx, cy = sim.cy, R0 = sim.R0, R = sim.w.arena_R;
        // sol : disque sombre teinté + lignes de terrain
        g.disc_grad(cx, cy, R0 + 18, rgba(18, 24, 40), rgba(8, 10, 18), 1.4f);
        g.disc_grad(cx, cy, R0, mixc(rgba(16, 70, 44), accent, 0.10f), rgba(8, 34, 24), 1.8f);
        // bandes de tonte (anneaux concentriques alternés)
        for (int i = 1; i < 8; i += 2)
            g.ring_(cx, cy, R0 * (i + 0.5f) / 8.0f, R0 / 16.0f, withA(WHITE, 0.035f));
        g.ring_(cx, cy, 120, 2.5f, withA(WHITE, 0.32f));
        g.disc(cx, cy, 7, withA(WHITE, 0.45f));
        g.rect(Rectangle{cx - R0 + 10, cy - 2.5f, 2 * R0 - 20, 5}, withA(WHITE, 0.22f));
        // zone interdite (au-delà du rayon courant)
        if (R < R0 - 2) {
            float mid = (R + R0) / 2, half = (R0 - R) / 2;
            float pulse = 0.5f + 0.5f * std::sin(t * 5);
            g.ring_(cx, cy, mid, half, withA(rgba(40, 4, 12), 0.80f));
            for (int i = 0; i < 64; ++i) {   // hachures de zone interdite (traits radiaux)
                float a = i * 2 * PI_F / 64 + t * 0.25f;
                float ca = std::cos(a), sa = std::sin(a);
                DrawLineEx(Vector2{cx + ca * (R + 10), cy + sa * (R + 10)}, Vector2{cx + ca * (R0 - 2), cy + sa * (R0 - 2)},
                           3.0f, withA(rgba(255, 50, 70), 0.10f + 0.06f * pulse));
            }
            g.ring_(cx, cy, R + 16, 7, withA(rgba(255, 40, 60), 0.35f + 0.25f * pulse));
        }
        // mur lumineux
        g.ring_(cx, cy, R + 6, 6, withA(mixc(WHITE, accent, 0.25f), 0.95f));
        g.ring_(cx, cy, R + 6, 1.5f, withA(WHITE, 1));
    }

    void draw_world(AnimContext& ctx) override {
        if (n == 0) return;
        float t = (float)ctx.time;
        draw_arena(t);
        // cœur
        if (sim.heart_on) {
            float age = t - heart_t0;
            float s = ease_out_back(clamp01(age / 0.35f)) * (1 + 0.08f * std::sin(t * 7));
            if (const Texture2D* h = icon("heart")) g.tex_center(*h, sim.hx, sim.hy, 70 * s, 64 * s, WHITE);
        }
        // fantômes des éliminés (rétrécissement + rotation)
        for (int i = 0; i < n; ++i) {
            if (death_t[i] < 0) continue;
            float age = t - death_t[i];
            if (age > 0.45f) continue;
            TokenFx f; f.scale = 1 + 0.35f * age / 0.45f; f.alpha = 1 - age / 0.45f; f.flash = 1 - age / 0.3f;
            f.tilt = age * 400;
            draw_token(g, teams[i], death_x[i], death_y[i], sim.radius, f);
        }
        // jetons + jauges de vie
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            if (!b.on) continue;
            float pop = ease_out_back(clamp01((t - i * 0.012f) / 0.30f), 2.0f);
            float r = sim.radius;
            float h = hp_shown[i] / 100.0f;
            Color hc = h > 0.5f ? mixc(rgba(255, 210, 60), HP_GREEN, (h - 0.5f) * 2) : mixc(HP_RED, rgba(255, 210, 60), h * 2);
            g.ring_(b.x, b.y, r + 10, 5.5f, withA(rgba(0, 0, 0), 0.55f * pop));
            g.gauge(b.x, b.y, r + 10, 4.5f, h * pop, hc);
            TokenFx f; f.scale = pop; f.flash = flash[i] * 0.8f;
            f.red = sim.hp[i] < 30 ? 0.25f + 0.25f * std::sin(t * 12) : 0;
            draw_token(g, teams[i], b.x, b.y, r, f);
        }
    }

    void draw_world_glow(AnimContext&) override {
        if (n == 0) return;
        g.ring_(sim.cx, sim.cy, sim.w.arena_R + 6, 9, withA(mixc(WHITE, accent, 0.4f), 0.9f));
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            if (b.on) draw_token_glow(g, teams[i], b.x, b.y, sim.radius, 0.5f + flash[i]);
        }
        if (sim.heart_on) g.glow_at(sim.hx, sim.hy, 90, withA(rgba(255, 60, 120), 0.8f));
    }

    void draw_hud(AnimContext& ctx) override {
        if (n == 0) return;
        float t = (float)ctx.time;
        header(t);
        // compteur ou duel final
        if (sim.alive > 2 || win_t >= 0) {
            char buf[48];
            snprintf(buf, sizeof(buf), "%d %s LEFT", sim.alive, sim.alive > 3 ? upper(cfg.str("noun", "clubs")).c_str() : "");
            std::string s = sim.alive == 3 ? "FINAL 3" : buf;
            if (win_t < 0) {
                float k = 1 + 0.25f * ease_out_back(left_pop);
                chip(s, DW / 2, 470, withA(rgba(255, 255, 255), 0.95f), rgba(12, 14, 24), 1, 34 * k);
            }
        } else {
            draw_duel_bar(t);
        }
        if (win_t < 0 || t < win_t) draw_toasts(t, 1560);
        if (win_t >= 0 && t >= win_t) {
            float age = t - win_t;
            if (age < 0.02f) {}
            winner_card(age, sim.winner, cfg.str("win_line", "LAST CLUB STANDING"));
        }
    }

    void draw_duel_bar(float t) {
        int a = -1, b = -1;
        for (int i = 0; i < n; ++i) if (sim.w.balls[i].on) { if (a < 0) a = i; else b = i; }
        if (a < 0 || b < 0) return;
        float in = ease_out_back(clamp01((t - std::max(0.0f, duel_t)) / 0.4f));
        float y = 470;
        for (int side = 0; side < 2; ++side) {
            int i = side == 0 ? a : b;
            float h = clamp01(hp_shown[i] / 100);
            float bw = 270, bh = 34;
            float bx = side == 0 ? DW / 2 - 60 - bw : DW / 2 + 60;
            g.rrect(Rectangle{bx - 4, y - bh / 2 - 4, bw + 8, bh + 8}, bh / 2 + 4, withA(rgba(255, 255, 255), 0.18f * in));
            g.rrect(Rectangle{bx, y - bh / 2, bw, bh}, bh / 2, withA(rgba(8, 10, 18), 0.92f * in));
            Color hc = h > 0.5f ? mixc(rgba(255, 210, 60), HP_GREEN, (h - 0.5f) * 2) : mixc(HP_RED, rgba(255, 210, 60), h * 2);
            float fw = std::max(bh, bw * h);
            g.rrect(Rectangle{side == 0 ? bx + bw - fw : bx, y - bh / 2, fw, bh}, bh / 2, withA(hc, in));
            TokenFx f; f.alpha = in; f.shadow = 0.6f;
            draw_token(g, teams[i], side == 0 ? bx - 56 : bx + bw + 56, y, 50, f);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", (int)std::ceil(hp_shown[i]));
            TextStyle hs; hs.kind = F_BOLD; hs.size = 30; hs.shadow = 0.8f;
            draw_text(fonts, buf, side == 0 ? bx + bw - 30 : bx + 30, y + 1, hs, 0, 0.5f, in);
        }
        TextStyle vs; vs.size = 60; vs.color = FB_GOLD; vs.outline = 3;
        draw_text(fonts, "VS", DW / 2, y, vs, 0, 0.5f, in);
    }
};

}  // namespace

Scene* make_royale_scene() { return new RoyaleScene(); }
