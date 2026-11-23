// ============================================================================
//  duel_scene.cpp — « Big Match Duel » : les deux équipes d'une affiche s'affrontent sur un
//  mini-terrain. Points de vie, bonus foot (crampons = vitesse + dégâts, gant = bouclier,
//  carton rouge = adversaire gelé, cœur = soin). Scénario le plus serré choisi avant le rendu.
// ============================================================================
#include "fb_common.h"

namespace {

enum { DV_HIT, DV_KO, DV_SPAWN, DV_TAKE, DV_BLOCK, DV_WALL };
enum { PU_BOOTS, PU_SHIELD, PU_CARD, PU_HEART, PU_COUNT };
struct Ev { int type, a, b; float x, y, amount; int kind; bool crit; };

struct DuelSim {
    World w;
    Rng rng{1};
    float t = 0;
    bool done = false;
    int winner = -1;
    float hp[2] = {100, 100};
    float boots[2] = {0, 0}, frozen[2] = {0, 0};
    bool shield[2] = {false, false};
    float pair_cd = -10;
    bool pu_on = false;
    int pu_kind = 0;
    float pu_x = 0, pu_y = 0, pu_timer = 2.4f;
    int last_kind = -1;
    Rectangle pitch{110, 640, 860, 860};
    float radius = 84;
    float target_end = 24;
    std::vector<Ev> ev;
    int lead = -1, lead_changes = 0;
    float final_hit = -1;
    float max_gap = 0;   // plus grand écart de vie observé

    void init(uint64_t seed) {
        rng.reseed(seed);
        w = World();
        w.box = Rectangle{pitch.x + 8, pitch.y + 8, pitch.width - 16, pitch.height - 16};
        w.ball_rest = 1.0f;
        target_end = rng.r(22.5f, 26.0f);
        for (int i = 0; i < 2; ++i) {
            Ball b;
            b.x = pitch.x + pitch.width / 2 + (i == 0 ? -1 : 1) * 210;
            b.y = pitch.y + pitch.height / 2 + rng.r(-120, 120);
            float a = rng.r(-0.9f, 0.9f) + (i == 0 ? 0 : PI_F);
            b.vx = std::cos(a) * 600; b.vy = std::sin(a) * 600;
            b.r = radius; b.id = i;
            w.balls.push_back(b);
        }
    }

    float base_speed() const { return 600 + t * 9; }

    // régulateur : la vie du plus faible suit une descente cible jusqu'à ~target_end
    float pace() const {
        float low = std::min(hp[0], hp[1]);
        float desired = 100 * (1 - t / target_end);
        return clampf(std::exp((low - desired) / 22.0f), 0.35f, 3.0f);
    }

    void spawn_pu() {
        int kind;
        do { kind = rng.i(0, PU_COUNT - 1); } while (kind == last_kind);
        if (kind == PU_HEART && std::max(hp[0], hp[1]) > 85) kind = PU_BOOTS;
        for (int tries = 0; tries < 40; ++tries) {
            float x = rng.r(pitch.x + 110, pitch.x + pitch.width - 110);
            float y = rng.r(pitch.y + 110, pitch.y + pitch.height - 110);
            bool ok = true;
            for (const auto& b : w.balls) if (len2(b.x - x, b.y - y) < b.r + 120) ok = false;
            if (!ok) continue;
            pu_on = true; pu_kind = kind; pu_x = x; pu_y = y; last_kind = kind;
            ev.push_back({DV_SPAWN, -1, -1, x, y, 0, kind, false});
            return;
        }
    }

    void step() {
        t += SIM_DT;
        for (int i = 0; i < 2; ++i) {
            boots[i] = std::max(0.0f, boots[i] - SIM_DT);
            frozen[i] = std::max(0.0f, frozen[i] - SIM_DT);
        }
        // vitesse : gelé = presque immobile, crampons = +40 %
        Ball saved[2];
        for (int i = 0; i < 2; ++i) saved[i] = w.balls[i];
        w.ball_hits.clear();
        w.wall_hits.clear();
        w.step(SIM_DT);
        for (int i = 0; i < 2; ++i) {
            Ball& b = w.balls[i];
            float target = base_speed() * (boots[i] > 0 ? 1.4f : 1.0f);
            if (frozen[i] > 0) {
                b.x = saved[i].x; b.y = saved[i].y;   // immobile (mais garde sa direction)
                float v = len2(b.vx, b.vy);
                if (v > 1) { b.vx *= target / v; b.vy *= target / v; }
                continue;
            }
            float v = len2(b.vx, b.vy);
            if (v > 1) { float nv = lerpf(v, target, clamp01(3 * SIM_DT)); b.vx *= nv / v; b.vy *= nv / v; }
        }
        for (const auto& h : w.ball_hits) {
            if (t - pair_cd < 0.2f) continue;
            pair_cd = t;
            int att = h.att_a >= h.att_b ? 0 : 1;
            if (frozen[att] > 0) att = 1 - att;   // un joueur gelé ne peut pas attaquer
            int vic = 1 - att;
            bool crit = h.speed > 1.7f * base_speed() || boots[att] > 0;
            float dmg = (7 + 0.019f * h.speed) * pace() * (boots[att] > 0 ? 1.5f : 1.0f) * (frozen[vic] > 0 ? 1.3f : 1.0f);
            if (shield[vic]) {
                shield[vic] = false;
                ev.push_back({DV_BLOCK, att, vic, h.x, h.y, 0, PU_SHIELD, false});
                continue;
            }
            hp[vic] -= dmg;
            ev.push_back({DV_HIT, att, vic, h.x, h.y, dmg, 0, crit});
            if (hp[vic] <= 0) {
                hp[vic] = 0;
                winner = att;
                done = true;
                final_hit = t;
                ev.push_back({DV_KO, att, vic, w.balls[vic].x, w.balls[vic].y, 0, 0, true});
                return;
            }
            max_gap = std::max(max_gap, std::fabs(hp[0] - hp[1]));
        }
        for (const auto& h : w.wall_hits)
            if (h.speed > 300) ev.push_back({DV_WALL, h.ball, -1, h.x, h.y, h.speed, 0, false});

        if (!pu_on) { pu_timer -= SIM_DT; if (pu_timer <= 0) spawn_pu(); }
        if (pu_on) {
            for (int i = 0; i < 2; ++i) {
                const Ball& b = w.balls[i];
                if (frozen[i] > 0 || len2(b.x - pu_x, b.y - pu_y) > b.r + 38) continue;
                pu_on = false;
                pu_timer = rng.r(2.6f, 4.0f);
                float amount = 0;
                if (pu_kind == PU_BOOTS) boots[i] = 4.0f;
                else if (pu_kind == PU_SHIELD) shield[i] = true;
                else if (pu_kind == PU_CARD) frozen[1 - i] = 1.7f;
                else { amount = std::min(22.0f, 100 - hp[i]); hp[i] += amount; }
                ev.push_back({DV_TAKE, i, 1 - i, pu_x, pu_y, amount, pu_kind, false});
                break;
            }
        }
        int l = hp[0] > hp[1] + 0.5f ? 0 : hp[1] > hp[0] + 0.5f ? 1 : lead;
        if (lead >= 0 && l != lead) lead_changes++;
        lead = l;
        if (t > 50) { winner = hp[0] >= hp[1] ? 0 : 1; done = true; final_hit = t; }
    }

    float score() const {
        if (!done) return -1e9f;
        float s = -std::fabs(t - 24) * 1.5f;
        if (t < 18 || t > 31) s -= 10;
        s += std::min(lead_changes, 5) * 2.0f;
        if (lead_changes == 0) s -= 6;
        float wh = hp[winner];
        if (wh < 30) s += 4; else if (wh > 60) s -= 4;
        if (max_gap > 55) s -= 3;
        return s;
    }
};

class DuelScene : public FootballScene {
    DuelSim sim;
    float final_hit = -1;
    float flash[2] = {0, 0}, hp_shown[2] = {100, 100};
    float pu_t0 = -1, win_t = -1, last_text = -10, last_hitstop = -10;
    int intro = 0;
    bool started = false, celebrated = false;

public:
    void setup(AnimContext& ctx) override {
        if (teams.size() < 2) { printf("[FOOT] Duel : il faut 2 équipes\n"); return; }
        Rng pick(ctx.seed);
        uint64_t best_seed = 1;
        float best = -1e9f;
        int candidates = cfg.integer("candidates", 40) + 20;
        for (int c = 0; c < candidates; ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            DuelSim d;
            d.init(s);
            while (!d.done) { d.step(); d.ev.clear(); }
            float sc = d.score();
            if (sc > best) { best = sc; best_seed = s; final_hit = d.final_hit; }
        }
        sim.init(best_seed);
        printf("[FOOT] Duel : meilleur scénario %.1f (KO à %.1f s)\n", best, final_hit);
        ctx.metric("drama_score", best);
        ctx.metric("thumb_time", start_time + final_hit * 0.45f);
    }

    float desired_time_scale() override {
        float d = (float)sim_time - final_hit;
        return (final_hit > 0 && d > -0.6f && d < 0.12f) ? 0.22f : 1.0f;
    }

    void sim_step() override { sim.step(); sim_done = sim.done; }

    void after_frame(AnimContext& ctx, float dt) override {
        if (teams.size() < 2) return;
        float t = (float)ctx.time;
        if (intro == 0 && t >= 0.0f) { ctx.sound("whoosh_long", 0.6f); intro = 1; }
        if (intro == 1 && t >= 0.35f) { ctx.sound("impact_bass", 0.55f); ctx.sound("crowd_cheer_small", 0.5f); kick_shake(10); intro = 2; }
        if (!started && t >= start_time - 0.12f) { started = true; ctx.sound("whistle_long", 0.8f); }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        for (int i = 0; i < 2; ++i) {
            flash[i] = std::max(0.0f, flash[i] - dt * 4.5f);
            hp_shown[i] = lerpf(hp_shown[i], sim.hp[i], clamp01(dt * 9));
        }
        // traînée des crampons
        for (int i = 0; i < 2; ++i)
            if (sim.boots[i] > 0 && !sim_done) {
                const Ball& b = sim.w.balls[i];
                fx.splash(b.x + frng.r(-20, 20), b.y + frng.r(-20, 20), 1, teams[i].glow, 60);
            }
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

    void handle(AnimContext& ctx, const Ev& e, float t) {
        static const char* PU_NAMES[] = {"SPEED BOOTS", "GOALKEEPER GLOVE", "RED CARD", "+HEALTH"};
        switch (e.type) {
        case DV_HIT: {
            flash[e.b] = 1;
            fx.sparks(e.x, e.y, e.crit ? 28 : 14, mixc(teams[e.a].glow, WHITE, 0.3f), e.crit ? 1300 : 950);
            if (t - last_text > 0.15f) {
                last_text = t;
                char buf[32];
                snprintf(buf, sizeof(buf), "-%d", (int)std::round(e.amount));
                const Ball& v = sim.w.balls[e.b];
                fx.text(buf, v.x, v.y - sim.radius - 34, e.crit ? FB_GOLD : rgba(255, 90, 90), e.crit ? 66 : 52);
            }
            if (e.crit) {
                sound(ctx, "hit_heavy", 2, 0.9f);
                ctx.sound("kick_hard_2", 0.6f);
                kick_shake(16);
                if (t - last_hitstop > 1.0f) { hitstop = 0.05f; last_hitstop = t; }
            } else {
                sound(ctx, "hit", 3, 0.55f);
                sound(ctx, "kick", 3, 0.5f);
                kick_shake(6);
            }
            if (frng.chance(0.25f)) ctx.sound("crowd_ooh", 0.3f);
            break;
        }
        case DV_BLOCK:
            ctx.sound("clang", 0.8f);
            fx.shock(sim.w.balls[e.b].x, sim.w.balls[e.b].y, sim.radius, sim.radius * 2.4f, 0.45f, rgba(120, 200, 255));
            fx.text("BLOCKED!", sim.w.balls[e.b].x, sim.w.balls[e.b].y - sim.radius - 40, rgba(140, 210, 255), 56);
            break;
        case DV_SPAWN:
            pu_t0 = t;
            ctx.sound("pop_2", 0.45f);
            fx.shock(e.x, e.y, 10, 110, 0.5f, WHITE);
            break;
        case DV_TAKE: {
            const char* snd[] = {"powerup", "powerup_space", "whistle_short", "coin_2"};
            ctx.sound(snd[e.kind], 0.7f);
            if (e.kind == PU_CARD) { ctx.sound("freeze", 0.7f); ctx.sound("crowd_ooh", 0.5f); }
            if (e.kind == PU_HEART) ctx.sound("pickup", 0.5f);
            Color c = e.kind == PU_BOOTS ? rgba(255, 214, 40) : e.kind == PU_SHIELD ? rgba(110, 190, 255)
                    : e.kind == PU_CARD ? rgba(255, 50, 60) : HP_GREEN;
            fx.sparks(e.x, e.y, 22, c, 800);
            fx.shock(e.x, e.y, 20, 160, 0.5f, c);
            std::string label = PU_NAMES[e.kind];
            if (e.kind == PU_CARD) label = "RED CARD! " + upper(teams[e.b].short_name) + " FROZEN";
            toast(label, e.a, t, c);
            break;
        }
        case DV_WALL:
            sound(ctx, "bounce", 4, 0.16f + 0.2f * clamp01((e.amount - 300) / 600));
            break;
        case DV_KO: {
            const Team& tv = teams[e.b];
            win_t = t + 0.45f;
            fx.shock(e.x, e.y, 40, 520, 0.8f, tv.glow, 1.5f);
            fx.confetti(e.x, e.y, 60, {tv.c1, tv.c2, WHITE}, 1100, PI_F);
            fx.sparks(e.x, e.y, 46, tv.glow, 1600, 1.5f);
            ctx.sound("explode_big", 1.0f);
            ctx.sound("impact_bass", 0.9f);
            ctx.sound("whistle_final", 0.8f);
            ctx.sound("crowd_roar", 1.0f);
            kick_shake(32);
            break;
        }
        }
    }

    void draw_pitch() {
        const Rectangle p = sim.pitch;
        g.rrect(Rectangle{p.x - 14, p.y - 14, p.width + 28, p.height + 28}, 40, rgba(10, 14, 22));
        g.rrect_grad(p, 30, rgba(30, 120, 62), rgba(18, 84, 44));
        // bandes de tonte
        int bands = 10;
        for (int i = 0; i < bands; i += 2)
            g.rect(Rectangle{p.x + 14, p.y + 14 + i * (p.height - 28) / bands, p.width - 28, (p.height - 28) / bands},
                   withA(BLACK, 0.07f));
        Color line = withA(WHITE, 0.55f);
        float in = 26;
        g.rrect_line(Rectangle{p.x + in, p.y + in, p.width - 2 * in, p.height - 2 * in}, 6, 2.2f, line);
        g.rect(Rectangle{p.x + in, p.y + p.height / 2 - 2, p.width - 2 * in, 4}, line);
        g.ring_(p.x + p.width / 2, p.y + p.height / 2, 110, 2.2f, line);
        g.disc(p.x + p.width / 2, p.y + p.height / 2, 7, line);
        for (int s = 0; s < 2; ++s) {   // surfaces de réparation + buts
            float y = s == 0 ? p.y + in : p.y + p.height - in - 150;
            g.rrect_line(Rectangle{p.x + p.width / 2 - 200, y, 400, 150}, 2, 2.2f, line);
            float y2 = s == 0 ? p.y + in : p.y + p.height - in - 60;
            g.rrect_line(Rectangle{p.x + p.width / 2 - 95, y2, 190, 60}, 2, 2.2f, line);
            float gy = s == 0 ? p.y + 4 : p.y + p.height - 26;
            g.rrect(Rectangle{p.x + p.width / 2 - 80, gy, 160, 22}, 6, withA(WHITE, 0.85f));
        }
    }

    void draw_world(AnimContext& ctx) override {
        if (teams.size() < 2) return;
        float t = (float)ctx.time;
        draw_pitch();
        if (sim.pu_on) {
            float age = t - pu_t0;
            float s = ease_out_back(clamp01(age / 0.35f), 2) * (1 + 0.07f * std::sin(t * 7));
            const char* names[] = {"bolt", "shield", "card", "heart"};
            g.disc(sim.pu_x, sim.pu_y, 52 * s, withA(rgba(10, 14, 24), 0.85f));
            g.ring_(sim.pu_x, sim.pu_y, 52 * s, 3, withA(WHITE, 0.8f));
            if (const Texture2D* ic = icon(names[sim.pu_kind])) g.tex_center(*ic, sim.pu_x, sim.pu_y, 70 * s, 70 * s, WHITE);
        }
        for (int i = 0; i < 2; ++i) {
            const Ball& b = sim.w.balls[i];
            if (sim_done && i != sim.winner && win_t >= 0 && t > win_t - 0.3f) continue;
            float pop = ease_out_back(clamp01((t - 0.15f - i * 0.12f) / 0.4f), 2.2f);
            TokenFx f; f.scale = pop; f.flash = flash[i] * 0.85f;
            f.gray = sim.frozen[i] > 0 ? 0.85f : 0;
            float h = hp_shown[i] / 100;
            Color hc = h > 0.5f ? mixc(rgba(255, 210, 60), HP_GREEN, (h - 0.5f) * 2) : mixc(HP_RED, rgba(255, 210, 60), h * 2);
            g.ring_(b.x, b.y, sim.radius + 12, 6.5f, withA(BLACK, 0.55f * pop));
            g.gauge(b.x, b.y, sim.radius + 12, 5.5f, h * pop, hc);
            draw_token(g, teams[i], b.x, b.y, sim.radius, f);
            if (sim.frozen[i] > 0) {
                g.disc(b.x, b.y, sim.radius, withA(rgba(150, 210, 255), 0.30f));
                g.ring_(b.x, b.y, sim.radius + 3, 4, withA(rgba(200, 235, 255), 0.9f));
            }
            if (sim.shield[i]) {
                float p = 0.5f + 0.5f * std::sin(t * 6);
                g.ring_(b.x, b.y, sim.radius + 26, 5, withA(rgba(110, 190, 255), 0.6f + 0.3f * p));
            }
            if (sim.boots[i] > 0)
                if (const Texture2D* ic = icon("bolt"))
                    g.tex_center(*ic, b.x + std::cos(t * 5) * (sim.radius + 30), b.y + std::sin(t * 5) * (sim.radius + 30), 46, 46, WHITE);
        }
    }

    void draw_world_glow(AnimContext&) override {
        if (teams.size() < 2) return;
        for (int i = 0; i < 2; ++i) {
            const Ball& b = sim.w.balls[i];
            draw_token_glow(g, teams[i], b.x, b.y, sim.radius, (sim.boots[i] > 0 ? 1.4f : 0.6f) + flash[i]);
            if (sim.shield[i]) g.ring_(b.x, b.y, sim.radius + 26, 10, withA(rgba(110, 190, 255), 0.8f));
        }
        if (sim.pu_on) g.glow_at(sim.pu_x, sim.pu_y, 110, withA(WHITE, 0.5f));
    }

    void draw_hud(AnimContext& ctx) override {
        if (teams.size() < 2) return;
        float t = (float)ctx.time;
        header(t);
        std::string date = cfg.str("date");
        // barres de vie
        float in = ease_out_back(clamp01((t - 0.2f) / 0.45f));
        float y = 488;
        for (int side = 0; side < 2; ++side) {
            float h = clamp01(hp_shown[side] / 100);
            float bw = 300, bh = 38;
            float bx = side == 0 ? DW / 2 - 64 - bw : DW / 2 + 64;
            g.rrect(Rectangle{bx - 4, y - bh / 2 - 4, bw + 8, bh + 8}, bh / 2 + 4, withA(WHITE, 0.2f * in));
            g.rrect(Rectangle{bx, y - bh / 2, bw, bh}, bh / 2, withA(rgba(8, 10, 18), 0.92f * in));
            Color hc = mixc(teams[side].glow, WHITE, flash[side] * 0.6f);
            float fw = std::max(bh, bw * h);
            g.rrect(Rectangle{side == 0 ? bx + bw - fw : bx, y - bh / 2, fw, bh}, bh / 2, withA(hc, in));
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", (int)std::ceil(hp_shown[side]));
            TextStyle hs; hs.kind = F_BOLD; hs.size = 32; hs.shadow = 0.9f;
            draw_text(fonts, buf, side == 0 ? bx + bw - 34 : bx + 34, y + 1, hs, 0, 0.5f, in);
            TokenFx f; f.alpha = in;
            draw_token(g, teams[side], side == 0 ? bx - 60 : bx + bw + 60, y, 54, f);
        }
        TextStyle vs; vs.size = 64; vs.color = FB_GOLD; vs.outline = 3;
        draw_text(fonts, "VS", DW / 2, y, vs, 0, 0.5f, in);
        if (!date.empty()) {
            TextStyle ds; ds.kind = F_BOLD; ds.size = 30; ds.color = withA(WHITE, 0.85f); ds.spacing = 3;
            draw_text(fonts, date, DW / 2, 1572, ds, 0, 0.5f, clamp01(t / 0.4f) * (win_t >= 0 ? 0 : 1));
        }
        if (win_t < 0 || t < win_t) draw_toasts(t, 1572 + (date.empty() ? 0 : 70), 1.8f);
        if (win_t >= 0 && t >= win_t) winner_card(t - win_t, sim.winner, cfg.str("win_line", "wins the battle"));
    }
};

}  // namespace

Scene* make_duel_scene() { return new DuelScene(); }
