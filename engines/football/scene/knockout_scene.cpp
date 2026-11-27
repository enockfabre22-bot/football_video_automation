// ============================================================================
//  knockout_scene.cpp — « 8-Club Knockout » : tableau à élimination directe (quarts, demies,
//  finale). Chaque duel se joue façon sumo sur la plateforme du bas : le premier poussé dehors
//  est éliminé, le vainqueur rejoint sa case suivante du tableau. Le tournoi entier est simulé
//  d'avance (scénario le plus dramatique).
// ============================================================================
#include "fb_common.h"

namespace {

enum { KV_HIT, KV_KO, KV_ENTER, KV_EXIT, KV_EDGE, KV_CHAMP };
struct Ev { int type, a, b; float x, y; int match; };

const float ENTER = 0.45f, EXIT = 0.6f;
const float AX = 540, AY = 1215, AR = 265, FR = 64;

// cases du tableau : 0-7 quarts, 8-11 demies, 12-13 finale, 14 champion
static Vector2 slot_pos(int s) {
    static const float qy[4] = {470, 580, 690, 800};
    if (s < 4) return {100, qy[s]};
    if (s < 8) return {980, qy[s - 4]};
    if (s == 8) return {258, 525};
    if (s == 9) return {258, 745};
    if (s == 10) return {822, 525};
    if (s == 11) return {822, 745};
    if (s == 12) return {412, 635};
    if (s == 13) return {668, 635};
    return {540, 548};
}
static float slot_r(int s) { return s < 8 ? 40 : s < 12 ? 43 : s < 14 ? 47 : 56; }
// match m : cases de départ et case d'arrivée du vainqueur
static void match_slots(int m, int& s1, int& s2, int& dst) {
    static const int A[7] = {0, 2, 4, 6, 8, 10, 12}, B[7] = {1, 3, 5, 7, 9, 11, 13}, D[7] = {8, 9, 10, 11, 12, 13, 14};
    s1 = A[m]; s2 = B[m]; dst = D[m];
}

// Duels façon sumo : sur la plateforme sans mur, les deux écussons reculent puis chargent ;
// le premier poussé hors du cercle est éliminé (aucun point de vie : la règle se voit).
struct KoSim {
    World w;
    Rng rng{1};
    float t = 0;
    int match = 0;          // 0..6
    int phase = 0;          // 0 entrée, 1 combat, 2 sortie, 3 terminé
    float phase_t = 0;
    int occ[15];
    int fighters[2] = {-1, -1};
    int winner_side = -1;
    float stun[2] = {0, 0}, edge_cd[2] = {0, 0}, hit_cd = -10;
    float R = AR, cycle_off = 0;
    float fall_x = 0, fall_y = 0;
    bool done = false;
    int champion = -1;
    std::vector<Ev> ev;
    float final_hit = -1;
    std::vector<float> fight_len;
    int edges_final = 0;

    void init(uint64_t seed, int n) {
        rng.reseed(seed);
        for (int i = 0; i < 15; ++i) occ[i] = i < 8 && i < n ? i : -1;
        w = World();
        w.arena_R = -1;
        w.ball_rest = 0.3f;
        Ball a, b;
        a.id = 0; b.id = 1;
        w.balls = {a, b};
        start_match();
    }

    void start_match() {
        int s1, s2, d;
        match_slots(match, s1, s2, d);
        fighters[0] = occ[s1]; fighters[1] = occ[s2];
        phase = 0; phase_t = 0;
        ev.push_back({KV_ENTER, fighters[0], fighters[1], 0, 0, match});
    }

    void begin_fight() {
        phase = 1; phase_t = 0;
        R = AR;
        cycle_off = rng.r(0.15f, 0.35f);      // première charge presque immédiate
        for (int i = 0; i < 2; ++i) {
            Ball& b = w.balls[i];
            b.on = true;
            b.r = FR;
            b.m = std::exp(rng.r(-0.35f, 0.35f));
            b.x = AX + (i == 0 ? -120 : 120);
            b.y = AY + rng.r(-30, 30);
            b.vx = b.vy = 0;
            stun[i] = 0; edge_cd[i] = 0;
        }
    }

    float push_mult() const {
        float base = match < 4 ? 0.22f : match < 6 ? 0.17f : 0.10f;   // la finale dure plus longtemps
        return base + std::max(0.0f, phase_t - 1.2f) * 0.16f;
    }

    void ko(int loser) {
        winner_side = 1 - loser;
        fight_len.push_back(phase_t);
        if (match == 6) final_hit = t;
        fall_x = w.balls[loser].x; fall_y = w.balls[loser].y;
        ev.push_back({KV_KO, winner_side, loser, fall_x, fall_y, match});
        phase = 2; phase_t = 0;
    }

    void step() {
        float dt = SIM_DT;
        t += dt;
        phase_t += dt;
        if (phase == 0) { if (phase_t >= ENTER) begin_fight(); return; }
        if (phase == 2) {
            if (phase_t >= EXIT) {
                int s1, s2, d;
                match_slots(match, s1, s2, d);
                occ[d] = fighters[winner_side];
                w.balls[0].on = w.balls[1].on = false;
                if (match == 6) { phase = 3; done = true; champion = occ[14]; ev.push_back({KV_CHAMP, champion, -1, 0, 0, match}); return; }
                match++;
                start_match();
            }
            return;
        }
        if (phase != 1) return;
        if (phase_t > 3.0f) R = std::max(150.0f, R - 14 * dt);   // combat long : la plateforme rétrécit
        float ph = std::fmod(phase_t + 1.25f - cycle_off, 1.25f);
        for (int i = 0; i < 2; ++i) {
            Ball& b = w.balls[i];
            const Ball& o = w.balls[1 - i];
            edge_cd[i] = std::max(0.0f, edge_cd[i] - dt);
            if (stun[i] > 0) {
                stun[i] -= dt;
                float f = std::exp(-1.1f * dt);
                b.vx *= f; b.vy *= f;
                continue;
            }
            float ax = b.x - o.x, ay = b.y - o.y, al = std::max(1.0f, len2(ax, ay));
            float gx, gy, sp;
            if (ph < 0.5f) { gx = AX + ax / al * R * 0.42f; gy = AY + ay / al * R * 0.42f; sp = 380; }
            else { gx = o.x; gy = o.y; sp = 980 * (0.85f + 0.3f * (b.m - 0.7f)); }
            float dx = gx - b.x, dy = gy - b.y, d = std::max(1.0f, len2(dx, dy));
            b.vx = lerpf(b.vx, dx / d * sp, clamp01(6 * dt));
            b.vy = lerpf(b.vy, dy / d * sp, clamp01(6 * dt));
        }
        w.ball_hits.clear();
        w.wall_hits.clear();
        w.step(dt);
        for (const auto& h : w.ball_hits) {
            if (t - hit_cd < 0.2f || h.speed < 110) continue;
            hit_cd = t;
            Ball& A = w.balls[0];
            Ball& B = w.balls[1];
            float K = std::min(1000.0f, (60 + 0.38f * h.speed) * push_mult());
            float sa = 0.6f + 0.8f * clamp01(h.att_b / std::max(1.0f, h.att_a + h.att_b));
            float sb = 0.6f + 0.8f * clamp01(h.att_a / std::max(1.0f, h.att_a + h.att_b));
            A.vx -= h.nx * K * sa / A.m; A.vy -= h.ny * K * sa / A.m;
            B.vx += h.nx * K * sb / B.m; B.vy += h.ny * K * sb / B.m;
            stun[0] = stun[1] = 0.28f + 0.00025f * h.speed;
            int att = h.att_a >= h.att_b ? 0 : 1;
            ev.push_back({KV_HIT, att, 1 - att, h.x, h.y, match});
        }
        for (int i = 0; i < 2; ++i) {
            const Ball& b = w.balls[i];
            float d = len2(b.x - AX, b.y - AY);
            if (d > R + FR * 0.15f) { ko(i); return; }
            if (d > R - FR * 0.55f && edge_cd[i] <= 0) {
                edge_cd[i] = 0.8f;
                ev.push_back({KV_EDGE, i, -1, b.x, b.y, match});
                if (match == 6) edges_final++;
            }
        }
        if (phase_t > 14) {   // garde-fou : le plus excentré tombe
            float d0 = len2(w.balls[0].x - AX, w.balls[0].y - AY), d1 = len2(w.balls[1].x - AX, w.balls[1].y - AY);
            ko(d0 >= d1 ? 0 : 1);
        }
    }

    float score() const {
        if (!done) return -1e9f;
        float s = -std::fabs(t - 22) * 1.0f;
        if (t > 30) s -= 10;
        s += std::min(edges_final, 3) * 2.0f;
        for (size_t i = 0; i < fight_len.size(); ++i) {
            float L = fight_len[i];
            float lo = i < 4 ? 1.3f : i < 6 ? 1.8f : 3.5f, hi = i < 4 ? 3.0f : i < 6 ? 4.2f : 7.0f;
            if (L < lo) s -= (lo - L) * 3;
            if (L > hi) s -= (L - hi) * 1.5f;
        }
        return s;
    }
};

class KnockoutScene : public FootballScene {
    KoSim sim;
    int n = 0;
    float final_hit = -1, win_t = -1, last_hitstop = -10, fall_t = -10;
    float flash[2] = {0, 0}, edge_flash[2] = {0, 0};
    int fall_team = -1;
    float fall_x = 0, fall_y = 0;
    std::vector<float> lost_t;     // instant d'élimination de chaque équipe (bracket grisé)
    std::vector<int> lost_round;
    bool started = false, celebrated = false;

public:
    void setup(AnimContext& ctx) override {
        n = std::min(8, (int)teams.size());
        if (n < 8) printf("[FOOT] Knockout : %d équipes (8 attendues)\n", n);
        Rng pick(ctx.seed);
        uint64_t best_seed = 1;
        float best = -1e9f;
        for (int c = 0; c < cfg.integer("candidates", 40); ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            KoSim k;
            k.init(s, n);
            while (!k.done) { k.step(); k.ev.clear(); }
            float sc = k.score();
            if (sc > best) { best = sc; best_seed = s; final_hit = k.final_hit; }
        }
        sim.init(best_seed, n);
        printf("[FOOT] Knockout : meilleur scénario %.1f (finale décidée à %.1f s)\n", best, final_hit);
        if (getenv("FB_DEBUG")) {
            KoSim d; d.init(best_seed, n);
            while (!d.done) { d.step(); d.ev.clear(); }
            printf("[FOOT] Durée des duels :");
            for (float L : d.fight_len) printf(" %.1f", L);
            printf("  | %d frôlements en finale\n", d.edges_final);
        }
        ctx.metric("drama_score", best);
        ctx.metric("thumb_time", start_time + final_hit * 0.55f);
        lost_t.assign(teams.size(), -1);
        lost_round.assign(teams.size(), -1);
    }

    float desired_time_scale() override {
        float d = (float)sim_time - final_hit;
        return (final_hit > 0 && d > -0.6f && d < 0.12f) ? 0.22f : 1.0f;
    }
    void sim_step() override { sim.step(); sim_done = sim.done; }

    void after_frame(AnimContext& ctx, float dt) override {
        float t = (float)ctx.time;
        if (!started) { started = true; ctx.sound("whistle_long", 0.75f); ctx.sound("airhorn", 0.3f); }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        for (float& f : flash) f = std::max(0.0f, f - dt * 4.5f);
        for (float& f : edge_flash) f = std::max(0.0f, f - dt * 2.5f);
        if (win_t >= 0 && !celebrated && t >= win_t) {
            celebrated = true;
            const Team& w = teams[sim.champion];
            ctx.sound("fanfare", 0.75f);
            ctx.sound("confetti", 0.8f);
            ctx.sound("crowd_cheer", 0.9f);
            fx.confetti(DW / 2, 940, 100, {w.c1, w.c2, WHITE, FB_GOLD}, 1500, PI_F * 0.45f);
        }
        if (win_t >= 0) {
            float age = t - win_t;
            if (age > 0.3f && age < 3.0f && (int)(age * 10) != (int)((age - dt) * 10)) {
                const Team& w = teams[sim.champion];
                fx.confetti(frng.r(100, 980), -20, 10, {w.c1, w.c2, WHITE, FB_GOLD}, 300, 0.5f, PI_F / 2);
            }
        }
    }

    void handle(AnimContext& ctx, const Ev& e, float t) {
        static const char* ROUND[7] = {"QUARTER-FINAL 1", "QUARTER-FINAL 2", "QUARTER-FINAL 3", "QUARTER-FINAL 4",
                                       "SEMI-FINAL 1", "SEMI-FINAL 2", "THE FINAL"};
        switch (e.type) {
        case KV_ENTER:
            sound(ctx, "whoosh", 2, 0.55f);
            if (e.match == 6) { ctx.sound("drumroll", 0.5f); ctx.sound("crowd_cheer_small", 0.6f); }
            toast(ROUND[e.match], -1, t, e.match == 6 ? FB_GOLD : WHITE);
            break;
        case KV_HIT:
            flash[e.b] = 1;
            fx.sparks(e.x, e.y, 22, mixc(teams[sim.fighters[e.a]].glow, WHITE, 0.3f), 1200);
            sound(ctx, "hit_heavy", 2, 0.75f);
            kick_shake(9);
            if (t - last_hitstop > 0.8f) { hitstop = 0.04f; last_hitstop = t; }
            break;
        case KV_EDGE:
            edge_flash[e.a] = 1;
            ctx.sound("crowd_ooh", e.match == 6 ? 0.6f : 0.35f);
            break;
        case KV_KO: {
            int loser = sim.fighters[e.b];
            fall_t = t; fall_team = loser; fall_x = e.x; fall_y = e.y;
            lost_t[loser] = t;
            lost_round[loser] = e.match;
            const Team& tv = teams[loser];
            bool fin = e.match == 6;
            fx.shock(e.x, e.y, 40, fin ? 520 : 320, 0.7f, tv.glow, 1.2f);
            fx.confetti(e.x, e.y, fin ? 60 : 36, {tv.c1, tv.c2, WHITE}, 1000, PI_F);
            fx.sparks(e.x, e.y, 30, tv.glow, 1400, 1.3f);
            ctx.sound("whoosh_low", 0.7f);
            ctx.sound(fin ? "explode_big" : "splat_big", fin ? 1.0f : 0.55f);
            ctx.sound(fin ? "crowd_roar" : "crowd_cheer_small", fin ? 1.0f : 0.6f);
            kick_shake(fin ? 32 : 18);
            if (fin) { ctx.sound("impact_bass", 0.9f); ctx.sound("whistle_final", 0.8f); }
            else ctx.sound("whistle_short", 0.6f);
            toast(upper(teams[loser].name) + " FELL OUT", loser, t, rgba(255, 92, 92));
            toast(upper(teams[sim.fighters[e.a]].name) + " GOES THROUGH", sim.fighters[e.a], t + 0.05f, HP_GREEN);
            break;
        }
        case KV_CHAMP:
            win_t = t + 0.35f;
            ctx.sound("whoosh_long", 0.6f);
            break;
        }
    }

    // position et rayon d'affichage d'un combattant (vol depuis / vers le tableau)
    void fighter_pose(int side, Vector2& p, float& r) const {
        int s1, s2, d;
        match_slots(sim.match, s1, s2, d);
        int slot = side == 0 ? s1 : s2;
        Vector2 from = slot_pos(slot);
        Vector2 arena = {AX + (side == 0 ? -120.0f : 120.0f), AY};
        if (sim.phase == 0) {
            float k = ease_in_out(sim.phase_t / ENTER);
            p = {lerpf(from.x, arena.x, k), lerpf(from.y, arena.y, k)};
            r = lerpf(slot_r(slot), FR, k);
        } else if (sim.phase == 1) {
            p = {sim.w.balls[side].x, sim.w.balls[side].y};
            r = FR;
        } else {
            Vector2 last = {sim.w.balls[side].x, sim.w.balls[side].y};
            if (side == sim.winner_side) {
                float k = ease_in_out(sim.phase_t / EXIT);
                Vector2 to = slot_pos(d);
                p = {lerpf(last.x, to.x, k), lerpf(last.y, to.y, k)};
                r = lerpf(FR, slot_r(d), k);
            } else { p = last; r = 0; }
        }
    }

    void draw_bracket(float t) {
        Color line = withA(WHITE, 0.22f);
        Color gold = withA(FB_GOLD, 0.9f);
        auto seg = [&](Vector2 a, Vector2 b, Color c) { DrawLineEx(a, b, 4, c); };
        // liaisons quart -> demie -> finale -> trophée
        for (int m = 0; m < 7; ++m) {
            int s1, s2, d;
            match_slots(m, s1, s2, d);
            Vector2 a = slot_pos(s1), b = slot_pos(s2), c = slot_pos(d);
            bool decided = sim.occ[d] >= 0;
            if (m < 6) {
                float mx = (a.x + c.x) / 2;
                for (int k = 0; k < 2; ++k) {
                    Vector2 p = k == 0 ? a : b;
                    bool win = decided && sim.occ[k == 0 ? s1 : s2] == sim.occ[d];
                    Color col = win ? gold : line;
                    seg(p, {mx, p.y}, col);
                    seg({mx, p.y}, {mx, c.y}, col);
                }
                seg({mx, c.y}, c, decided ? gold : line);
            } else {
                for (int k = 0; k < 2; ++k) {
                    Vector2 p = k == 0 ? a : b;
                    bool win = decided && sim.occ[k == 0 ? s1 : s2] == sim.occ[d];
                    seg(p, {p.x, c.y + 40}, win ? gold : line);
                    seg({p.x, c.y + 40}, {c.x, c.y + 40}, win ? gold : line);
                }
            }
        }
        // cases
        for (int s = 0; s < 15; ++s) {
            Vector2 p = slot_pos(s);
            float r = slot_r(s);
            g.disc(p.x, p.y, r + 4, withA(rgba(8, 10, 18), 0.92f));
            g.ring_(p.x, p.y, r + 4, 2, withA(s == 14 ? FB_GOLD : WHITE, s == 14 ? 0.8f : 0.25f));
            int team = sim.occ[s];
            bool fighting = false;
            if (sim.phase < 3) {
                int s1, s2, d;
                match_slots(sim.match, s1, s2, d);
                fighting = (s == s1 || s == s2) && sim.phase <= 2;
                if (s == d && sim.phase == 2) team = -1;   // le vainqueur est encore en vol
            }
            if (s == 14 && team < 0) {
                if (const Texture2D* tr = icon("trophy"))
                    g.tex_center(*tr, p.x, p.y, 74, 74, withA(WHITE, 0.95f));
                continue;
            }
            if (team < 0) continue;
            TokenFx f;
            f.shadow = 0.5f;
            if (fighting) f.alpha = 0.25f;
            if (lost_t[team] >= 0 && s < 14) {
                bool here = (lost_round[team] < 4 && s < 8) || (lost_round[team] >= 4 && lost_round[team] < 6 && s >= 8 && s < 12) ||
                            (lost_round[team] == 6 && s >= 12 && s < 14);
                if (here) { f.gray = 1; f.alpha = 0.55f; }
            }
            if (s == 14) f.scale = ease_out_back(clamp01((t - win_t + 0.35f) / 0.4f));
            draw_token(g, teams[team], p.x, p.y, r, f);
        }
    }

    void draw_world(AnimContext& ctx) override {
        float t = (float)ctx.time;
        draw_bracket(t);
        // plateforme de duel (sans mur) : bande de danger blanche et rouge au bord
        float R = sim.phase == 1 ? sim.R : AR;
        bool final_round = sim.match == 6;
        g.disc_grad(AX, AY, AR + 110, rgba(4, 6, 12), rgba(2, 3, 8, 0), 1.2f);
        g.ring_(AX, AY, R + 30, 26, withA(rgba(255, 40, 60), 0.10f + 0.05f * std::sin(t * 4)));
        g.disc(AX, AY + 22, R, rgba(12, 40, 26));
        g.disc_grad(AX, AY, R, mixc(rgba(36, 140, 74), accent, 0.08f), rgba(18, 82, 44), 1.6f);
        g.ring_(AX, AY, std::min(80.0f, R * 0.4f), 2.5f, withA(WHITE, 0.3f));
        int seg = 40;
        for (int i = 0; i < seg; ++i) {
            float a = (i + 0.5f) * 2 * PI_F / seg + t * 0.1f;
            Color c2 = final_round ? FB_GOLD : rgba(250, 250, 255);
            g.arc(AX, AY, R - 8, 6.5f, a, PI_F / seg * 0.82f, i % 2 ? rgba(255, 60, 70) : c2);
        }
        // le perdant tombe dans le vide
        float fa = t - fall_t;
        if (fall_team >= 0 && fa < 0.75f) {
            float k = fa / 0.75f;
            float dx = fall_x - AX, dy = fall_y - AY, dd = std::max(1.0f, len2(dx, dy));
            TokenFx f; f.scale = 1 - 0.65f * k; f.alpha = 1 - k * k; f.gray = k; f.tilt = fa * 520;
            draw_token(g, teams[fall_team], fall_x + dx / dd * 140 * k, fall_y + dy / dd * 140 * k + 200 * k * k, FR, f);
        }
        if (sim.phase >= 3) return;
        for (int side = 0; side < 2; ++side) {
            int team = sim.fighters[side];
            if (team < 0) continue;
            Vector2 p; float r;
            fighter_pose(side, p, r);
            if (r <= 1) continue;
            if (sim.phase == 2 && side != sim.winner_side) continue;   // il est en train de tomber
            if (edge_flash[side] > 0 && sim.phase == 1) g.ring_(p.x, p.y, r + 9, 5, withA(rgba(255, 60, 70), edge_flash[side]));
            TokenFx f; f.flash = flash[side] * 0.85f;
            draw_token(g, teams[team], p.x, p.y, r, f);
        }
    }

    void draw_world_glow(AnimContext&) override {
        bool final_round = sim.match == 6;
        g.ring_(AX, AY, (sim.phase == 1 ? sim.R : AR), 8, withA(final_round ? FB_GOLD : rgba(255, 70, 80), 0.55f));
        if (sim.phase >= 3) return;
        for (int side = 0; side < 2; ++side) {
            int team = sim.fighters[side];
            if (team < 0) continue;
            Vector2 p; float r;
            fighter_pose(side, p, r);
            if (r > 1 && !(sim.phase == 2 && side != sim.winner_side))
                draw_token_glow(g, teams[team], p.x, p.y, r, 0.6f + flash[side] + edge_flash[side]);
        }
    }

    void draw_hud(AnimContext& ctx) override {
        float t = (float)ctx.time;
        header(t);
        // les deux combattants du duel en cours, au-dessus de la plateforme
        if (sim.phase == 1 || sim.phase == 2) {
            for (int side = 0; side < 2; ++side) {
                int team = sim.fighters[side];
                if (team < 0) continue;
                TextStyle st; st.size = 60; st.outline = 3;
                bool lost = sim.phase == 2 && side != sim.winner_side;
                st.color = lost ? rgba(255, 92, 92) : WHITE;
                draw_text(fonts, upper(teams[team].name), side == 0 ? DW / 2 - 70 : DW / 2 + 70, 898, st, 380,
                          side == 0 ? 1.0f : 0.0f, lost ? 0.6f : 1.0f);
            }
            TextStyle vs; vs.size = 58; vs.color = FB_GOLD; vs.outline = 3;
            draw_text(fonts, "VS", DW / 2, 898, vs);
        }
        if (win_t < 0 || t < win_t) draw_toasts(t, 1568, 1.4f);
        if (win_t >= 0 && t >= win_t) winner_card(t - win_t, sim.champion, cfg.str("win_line", "Champions!"));
    }
};

}  // namespace

Scene* make_knockout_scene() { return new KnockoutScene(); }
