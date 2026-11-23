// ============================================================================
//  colorwar_scene.cpp — « Color War » : une carte quadrillée partagée entre 4 à 6 équipes.
//  Les jetons repeignent les cases adverses qu'ils touchent et rebondissent dessus ; des bombes
//  de peinture font basculer des zones entières. À la fin du compte à rebours, la plus grande
//  surface gagne. Scénario choisi pour ses changements de meneur et sa fin serrée.
// ============================================================================
#include "fb_common.h"

namespace {

enum { CV_FLIP, CV_BOMB_SPAWN, CV_BOMB, CV_WALL, CV_END };
struct Ev { int type, team; float x, y; int count; };

const int GN = 30;                 // 30 x 30 cases
const float CELL = 30;
const float GX = 90, GY = 600;     // coin haut-gauche de la carte
const float GW = GN * CELL;

struct CwSim {
    World w;
    Rng rng{1};
    int n = 0;
    float t = 0, duration = 26;
    bool done = false;
    std::vector<int> cell;             // propriétaire de chaque case
    std::vector<int> owner_of_ball;
    std::vector<int> counts;
    std::vector<Ev> ev;
    std::vector<int> flips;            // cases repeintes à ce pas (indices)
    bool bomb_on = false;
    float bx = 0, by = 0, bomb_timer = 2.2f;
    // suivi du meneur
    int leader = -1, lead_changes = 0, late_change = 0;
    float sample_t = 0;
    int leader_at_75 = -1;

    void init(uint64_t seed, int count) {
        rng.reseed(seed);
        n = count;
        cell.assign(GN * GN, 0);
        counts.assign(n, 0);
        // territoires de départ : secteurs angulaires (Voronoï de points sur un cercle)
        float a0 = rng.r(0, 2 * PI_F);
        std::vector<Vector2> seeds;
        for (int i = 0; i < n; ++i) {
            float a = a0 + i * 2 * PI_F / n;
            seeds.push_back({GN / 2.0f + std::cos(a) * GN * 0.3f, GN / 2.0f + std::sin(a) * GN * 0.3f});
        }
        for (int y = 0; y < GN; ++y)
            for (int x = 0; x < GN; ++x) {
                int best = 0; float bd = 1e9f;
                for (int i = 0; i < n; ++i) {
                    float d = (x + 0.5f - seeds[i].x) * (x + 0.5f - seeds[i].x) + (y + 0.5f - seeds[i].y) * (y + 0.5f - seeds[i].y);
                    if (d < bd) { bd = d; best = i; }
                }
                cell[y * GN + x] = best;
                counts[best]++;
            }
        w = World();
        w.box = Rectangle{GX, GY, GW, GW};
        w.ball_rest = 1.0f;
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 2; ++k) {
                Ball b;
                b.r = n <= 4 ? 31 : 27;
                float off = (k == 0 ? -1.6f : 1.6f);
                b.x = GX + (seeds[i].x + off * 0.6f) * CELL;
                b.y = GY + (seeds[i].y + off * 0.4f) * CELL;
                float va = rng.r(0, 2 * PI_F);
                b.vx = std::cos(va) * 720; b.vy = std::sin(va) * 720;
                b.id = (int)w.balls.size();
                w.balls.push_back(b);
                owner_of_ball.push_back(i);
            }
        w.collide_filter = [this](int a, int b) { return owner_of_ball[a] != owner_of_ball[b]; };
    }

    int cell_at(float px, float py) const {
        int cx = (int)std::floor((px - GX) / CELL), cy = (int)std::floor((py - GY) / CELL);
        if (cx < 0 || cy < 0 || cx >= GN || cy >= GN) return -1;
        return cy * GN + cx;
    }

    void paint(int c, int team) {
        int old = cell[c];
        if (old == team) return;
        cell[c] = team;
        counts[old]--;
        counts[team]++;
        flips.push_back(c);
    }

    void step() {
        t += SIM_DT;
        flips.clear();
        w.ball_hits.clear();
        w.wall_hits.clear();
        w.step(SIM_DT);
        w.normalize_speed(720 + t * 10, 3, SIM_DT);
        // règle « pong wars » : une case adverse touchée est repeinte et renvoie le jeton
        for (size_t i = 0; i < w.balls.size(); ++i) {
            Ball& b = w.balls[i];
            int team = owner_of_ball[i];
            bool fx = false, fy = false;
            for (int k = 0; k < 8; ++k) {
                float a = k * PI_F / 4;
                float ca = std::cos(a), sa = std::sin(a);
                int c = cell_at(b.x + ca * b.r, b.y + sa * b.r);
                if (c < 0 || cell[c] == team) continue;
                paint(c, team);
                if (std::fabs(ca) > 0.3f && !fx && b.vx * ca > 0) { b.vx = -b.vx; fx = true; }
                if (std::fabs(sa) > 0.3f && !fy && b.vy * sa > 0) { b.vy = -b.vy; fy = true; }
            }
            if (fx || fy) {   // léger angle aléatoire : évite les boucles parfaites
                float r = rng.r(-0.08f, 0.08f), cs = std::cos(r), sn = std::sin(r);
                float vx = b.vx * cs - b.vy * sn, vy = b.vx * sn + b.vy * cs;
                b.vx = vx; b.vy = vy;
            }
        }
        if (!flips.empty()) ev.push_back({CV_FLIP, -1, 0, 0, (int)flips.size()});
        for (const auto& h : w.wall_hits) if (h.speed > 300) ev.push_back({CV_WALL, -1, h.x, h.y, 0});
        // bombes de peinture
        if (!bomb_on && t < duration - 3) {
            bomb_timer -= SIM_DT;
            if (bomb_timer <= 0) {
                bx = GX + rng.r(4, GN - 4) * CELL;
                by = GY + rng.r(4, GN - 4) * CELL;
                bomb_on = true;
                ev.push_back({CV_BOMB_SPAWN, -1, bx, by, 0});
            }
        }
        if (bomb_on) {
            for (size_t i = 0; i < w.balls.size(); ++i) {
                const Ball& b = w.balls[i];
                if (len2(b.x - bx, b.y - by) > b.r + 34) continue;
                int team = owner_of_ball[i];
                int painted = 0;
                for (int y = 0; y < GN; ++y)
                    for (int x = 0; x < GN; ++x) {
                        float cx = GX + (x + 0.5f) * CELL, cy = GY + (y + 0.5f) * CELL;
                        if (len2(cx - bx, cy - by) < 5.3f * CELL && cell[y * GN + x] != team) { paint(y * GN + x, team); painted++; }
                    }
                ev.push_back({CV_BOMB, team, bx, by, painted});
                bomb_on = false;
                bomb_timer = rng.r(2.2f, 3.2f);
                break;
            }
        }
        // meneur (échantillonné)
        if (t - sample_t >= 0.25f) {
            sample_t = t;
            int l = (int)(std::max_element(counts.begin(), counts.end()) - counts.begin());
            if (leader >= 0 && l != leader) { lead_changes++; if (t > duration - 6) late_change++; }
            leader = l;
            if (leader_at_75 < 0 && t > duration * 0.75f) leader_at_75 = l;
        }
        if (t >= duration) { done = true; ev.push_back({CV_END, winner(), 0, 0, 0}); }
    }

    int winner() const { return (int)(std::max_element(counts.begin(), counts.end()) - counts.begin()); }

    float score() const {
        std::vector<int> c = counts;
        std::sort(c.begin(), c.end(), std::greater<int>());
        float margin = (c[0] - c[1]) / (float)(GN * GN);
        float s = std::min(lead_changes, 6) * 1.5f;
        s += std::min(late_change, 2) * 3;
        if (margin < 0.015f) s -= 8;               // trop serré pour être lisible (mêmes %)
        else if (margin < 0.06f) s += 4;
        else if (margin > 0.15f) s -= 4;
        if (leader_at_75 >= 0 && leader_at_75 != winner()) s += 3;   // retournement final
        if (lead_changes == 0) s -= 8;
        return s;
    }
};

class ColorWarScene : public FootballScene {
    CwSim sim;
    int n = 0;
    std::vector<Color> col;           // couleur de peinture (distincte) de chaque équipe
    std::vector<float> flip_t;        // dernier instant vidéo où chaque case a changé
    std::vector<float> share_shown;
    float bomb_t0 = -1, end_t = -1, win_t = -1, last_flip_snd = -10;
    int last_tick = -1;
    bool started = false, celebrated = false;

public:
    void setup(AnimContext& ctx) override {
        n = (int)teams.size();
        assign_colors();
        Rng pick(ctx.seed);
        uint64_t best_seed = 1;
        float best = -1e9f;
        for (int c = 0; c < cfg.integer("candidates", 40); ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            CwSim d;
            d.init(s, n);
            while (!d.done) { d.step(); d.ev.clear(); }
            float sc = d.score();
            if (sc > best) { best = sc; best_seed = s; }
        }
        sim.init(best_seed, n);
        printf("[FOOT] Color war : %d équipes, meilleur scénario %.1f\n", n, best);
        ctx.metric("drama_score", best);
        ctx.metric("thumb_time", start_time + sim.duration * 0.6f);
        flip_t.assign(GN * GN, -10);
        share_shown.assign(n, 1.0f / n);
    }

    // couleurs bien distinctes : si deux équipes se ressemblent, la seconde prend sa couleur
    // secondaire ou une teinte décalée
    void assign_colors() {
        auto dist = [](Color a, Color b) {
            return std::sqrt((float)((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b)));
        };
        for (int i = 0; i < n; ++i) {
            std::vector<Color> options = {teams[i].glow, teams[i].c2, teams[i].c1};
            Vector3 h = ColorToHSV(teams[i].glow);
            for (int k = 1; k <= 6; ++k) options.push_back(ColorFromHSV(std::fmod(h.x + k * 50.0f, 360.0f), 0.75f, 0.95f));
            Color pickc = options[0];
            for (const Color& o : options) {
                Vector3 oh = ColorToHSV(o);
                if (oh.y < 0.25f && oh.z < 0.6f) continue;      // ni noir ni gris foncé
                bool ok = true;
                for (int j = 0; j < i; ++j) if (dist(o, col[j]) < 130) ok = false;
                if (ok) { pickc = o; break; }
            }
            col.push_back(pickc);
        }
    }

    void sim_step() override {
        sim.step();
        sim_done = sim.done;
        for (int c : sim.flips) flip_t[c] = (float)sim_time + start_time;   // approximation suffisante
    }

    void after_frame(AnimContext& ctx, float dt) override {
        float t = (float)ctx.time;
        if (!started && t >= start_time - 0.12f) { started = true; ctx.sound("whistle_long", 0.7f); ctx.sound("airhorn", 0.3f); }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        for (int i = 0; i < n; ++i)
            share_shown[i] = lerpf(share_shown[i], sim.counts[i] / (float)(GN * GN), clamp01(dt * 8));
        // compte à rebours sonore
        float remaining = sim.duration - (float)sim_time;
        int sec = (int)std::ceil(remaining);
        if (!sim_done && remaining <= 5.0f && sec != last_tick && sec > 0) {
            last_tick = sec;
            ctx.sound("tick_1", 0.8f);
            if (sec == 5) ctx.sound("drumroll", 0.5f);
        }
        if (win_t >= 0 && !celebrated && t >= win_t) {
            celebrated = true;
            const Team& w = teams[sim.winner()];
            ctx.sound("fanfare", 0.7f);
            ctx.sound("confetti", 0.8f);
            ctx.sound("crowd_cheer", 0.9f);
            fx.confetti(DW / 2, 940, 100, {col[sim.winner()], w.c2, WHITE, FB_GOLD}, 1500, PI_F * 0.45f);
        }
        if (win_t >= 0) {
            float age = t - win_t;
            if (age > 0.3f && age < 3.0f && (int)(age * 10) != (int)((age - dt) * 10))
                fx.confetti(frng.r(100, 980), -20, 10, {col[sim.winner()], WHITE, FB_GOLD}, 300, 0.5f, PI_F / 2);
        }
    }

    void handle(AnimContext& ctx, const Ev& e, float t) {
        switch (e.type) {
        case CV_FLIP:
            if (t - last_flip_snd > 0.06f) { last_flip_snd = t; sound(ctx, "splat_small", 3, 0.16f + 0.04f * std::min(e.count, 4)); }
            break;
        case CV_WALL:
            sound(ctx, "bounce", 4, 0.12f);
            break;
        case CV_BOMB_SPAWN:
            bomb_t0 = t;
            ctx.sound("pop_2", 0.5f);
            fx.shock(e.x, e.y, 10, 100, 0.5f, WHITE);
            break;
        case CV_BOMB: {
            ctx.sound("splat_big", 0.9f);
            ctx.sound("explode_fire", 0.55f);
            ctx.sound("crowd_ooh", 0.45f);
            fx.splash(e.x, e.y, 60, col[e.team], 900);
            fx.shock(e.x, e.y, 30, 260, 0.6f, col[e.team], 1.2f);
            kick_shake(14);
            char buf[64];
            snprintf(buf, sizeof(buf), "PAINT BOMB! +%d", e.count);
            toast(buf, e.team, t, col[e.team]);
            break;
        }
        case CV_END:
            end_t = t;
            win_t = t + 1.3f;
            ctx.sound("whistle_final", 0.85f);
            ctx.sound("crowd_roar", 1.0f);
            ctx.sound("impact_bass", 0.7f);
            break;
        }
    }

    void draw_world(AnimContext& ctx) override {
        float t = (float)ctx.time;
        g.rrect(Rectangle{GX - 16, GY - 16, GW + 32, GW + 32}, 22, rgba(8, 10, 18));
        // cases
        for (int y = 0; y < GN; ++y)
            for (int x = 0; x < GN; ++x) {
                int c = y * GN + x;
                Color base = col[sim.cell[c]];
                float age = t - flip_t[c];
                float pop = age < 0.35f ? 1 - age / 0.35f : 0;
                Color cc = mixc(scalec(base, 0.78f + 0.08f * ((x + y) % 2)), WHITE, pop * 0.55f);
                float inset = 1.0f - pop * 1.0f;
                g.rect(Rectangle{GX + x * CELL + inset, GY + y * CELL + inset, CELL - 2 * inset, CELL - 2 * inset}, cc);
            }
        g.rrect_line(Rectangle{GX - 4, GY - 4, GW + 8, GW + 8}, 12, 3, withA(WHITE, 0.8f));
        if (sim.bomb_on) {
            float s = ease_out_back(clamp01((t - bomb_t0) / 0.35f), 2) * (1 + 0.08f * std::sin(t * 8));
            g.disc(sim.bx, sim.by, 46 * s, withA(rgba(10, 12, 20), 0.85f));
            g.ring_(sim.bx, sim.by, 46 * s, 3, withA(WHITE, 0.9f));
            if (const Texture2D* b = icon("bomb")) g.tex_center(*b, sim.bx, sim.by, 64 * s, 64 * s, WHITE);
        }
        for (size_t i = 0; i < sim.w.balls.size(); ++i) {
            const Ball& b = sim.w.balls[i];
            int team = sim.owner_of_ball[i];
            float pop = ease_out_back(clamp01((t - i * 0.04f) / 0.3f), 2);
            g.disc(b.x, b.y, b.r * pop + 5, withA(col[team], 1));
            TokenFx f; f.scale = pop;
            draw_token(g, teams[team], b.x, b.y, b.r, f);
        }
    }

    void draw_world_glow(AnimContext&) override {
        for (size_t i = 0; i < sim.w.balls.size(); ++i) {
            const Ball& b = sim.w.balls[i];
            g.glow_at(b.x, b.y, b.r * 2.2f, withA(col[sim.owner_of_ball[i]], 0.6f));
        }
        if (sim.bomb_on) g.glow_at(sim.bx, sim.by, 100, withA(WHITE, 0.5f));
    }

    void draw_hud(AnimContext& ctx) override {
        float t = (float)ctx.time;
        header(t);
        // barre de parts + écussons avec pourcentages
        float bx = 90, bw = 900, by = 534, bh = 24;
        float x = bx;
        g.rrect(Rectangle{bx - 3, by - 3, bw + 6, bh + 6}, (bh + 6) / 2, withA(rgba(8, 10, 18), 0.9f));
        float total = 0;
        for (float s : share_shown) total += s;
        for (int i = 0; i < n; ++i) {
            float w = bw * share_shown[i] / std::max(0.001f, total);
            g.rect(Rectangle{x, by, w, bh}, col[i]);
            x += w;
        }
        int lead = sim.winner();
        float slot = DW / n;
        for (int i = 0; i < n; ++i) {
            float cx = slot * (i + 0.5f), cy = 462;
            TokenFx f; f.shadow = 0.5f;
            draw_token(g, teams[i], cx - 44, cy, 30, f);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", (int)std::round(share_shown[i] * 100));
            TextStyle st; st.size = 46; st.color = col[i]; st.outline = 3;
            draw_text(fonts, buf, cx - 6, cy, st, 0, 0.0f);
            if (i == lead && end_t < 0)
                if (const Texture2D* cr = icon("crown")) g.tex_center(*cr, cx - 44, cy - 42, 40, 32, WHITE);
        }
        // minuteur / compte à rebours
        float remaining = std::max(0.0f, sim.duration - (float)sim_time);
        if (end_t < 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "0:%02d", (int)std::ceil(remaining));
            TextStyle ts; ts.size = 64; ts.outline = 3; ts.color = remaining <= 5 ? rgba(255, 80, 80) : WHITE;
            draw_text(fonts, buf, DW / 2, 1572, ts);
            if (remaining <= 5 && remaining > 0 && sim_time > 0) {
                float frac = remaining - std::floor(remaining);
                TextStyle big; big.size = 300 * (0.8f + 0.4f * frac); big.outline = 8;
                big.color = withA(WHITE, 0.85f);
                snprintf(buf, sizeof(buf), "%d", (int)std::ceil(remaining));
                draw_text(fonts, buf, GX + GW / 2, GY + GW / 2, big, 0, 0.5f, 0.35f + 0.6f * frac);
            }
        } else if (win_t < 0 || t < win_t) {
            TextStyle big; big.size = 220; big.outline = 8; big.color = FB_GOLD;
            float k = ease_out_back(clamp01((t - end_t) / 0.35f), 2);
            big.size *= k;
            draw_text(fonts, "TIME!", GX + GW / 2, GY + GW / 2, big);
        }
        if (win_t < 0 || t < win_t) draw_toasts(t, 1660, 1.6f);
        if (win_t >= 0 && t >= win_t) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%d%%)", upper(cfg.str("win_line", "conquers the map")).c_str(),
                     (int)std::round(sim.counts[sim.winner()] * 100.0f / (GN * GN)));
            winner_card(t - win_t, sim.winner(), buf);
        }
    }
};

}  // namespace

Scene* make_colorwar_scene() { return new ColorWarScene(); }
