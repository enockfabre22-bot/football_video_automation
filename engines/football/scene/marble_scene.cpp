// ============================================================================
//  marble_scene.cpp — « Marble Race » : course de billes verticale sur un parcours généré
//  (plots, rampes en zigzag, barres tournantes, entonnoir, bumpers). La caméra suit le
//  meneur, le top 5 s'affiche en direct ; arrivée au ralenti, puis le reste du peloton en
//  accéléré et le classement complet. Course la plus disputée choisie avant le rendu.
// ============================================================================
#include "fb_common.h"

namespace {

enum { MV_PEG, MV_BUMPER, MV_SPIN, MV_WALL, MV_FINISH, MV_LEAD, MV_DONE };
struct Ev { int type, a, b; float x, y, amount; };
enum { TAG_WALL = 0, TAG_RAMP = 1, TAG_SPIN = 2, TAG_PEG = 10, TAG_BUMPER = 11 };

const float WL = 60, WR = 1020;

struct MarbleSim {
    World w;
    Rng rng{1};
    int n = 0;
    float t = 0, radius = 30, y_end = 5000;
    bool done = false;
    std::vector<int> order;            // ordre d'arrivée
    std::vector<float> finish_t, best_y, best_t;
    std::vector<bool> finished;
    std::vector<Ev> ev;
    std::vector<int> section_kind;
    std::vector<float> section_y;
    int leader = -1, lead_changes = 0, late_changes = 0, nudges = 0;
    float sample_t = 0;

    void add_seg(float ax, float ay, float bx, float by, float th, float rest, float fr, int tag) {
        Seg s; s.ax = ax; s.ay = ay; s.bx = bx; s.by = by; s.t = th; s.rest = rest; s.friction = fr; s.tag = tag;
        w.segs.push_back(s);
    }
    void add_spinner(float px, float py, float half, float angv, float ang0) {
        Seg s; s.rotating = true; s.px = px; s.py = py; s.len_a = s.len_b = half; s.ang = ang0; s.angv = angv;
        s.t = 9; s.rest = 0.6f; s.friction = 0.02f; s.tag = TAG_SPIN;
        s.update_rot();
        w.segs.push_back(s);
    }
    void add_peg(float x, float y, float r, float rest, int tag) {
        Peg p; p.x = x; p.y = y; p.r = r; p.rest = rest; p.tag = tag;
        w.pegs.push_back(p);
    }

    float build_section(int kind, float y) {
        section_kind.push_back(kind);
        section_y.push_back(y);
        float gapc = 2 * radius + 34;           // passage minimal entre obstacles
        switch (kind) {
        case 0: {   // champ de plots en quinconce
            int rows = 7;
            float step = 2 * radius + 62;
            for (int r = 0; r < rows; ++r) {
                float off = (r % 2) ? step / 2 : 0;
                for (float x = WL + 40 + off; x < WR - 30; x += step) add_peg(x, y + 60 + r * 96, 11, 0.5f, TAG_PEG);
            }
            return y + 60 + rows * 96 + 40;
        }
        case 1: {   // rampes en zigzag
            int ramps = 4;
            for (int i = 0; i < ramps; ++i) {
                float yy = y + 40 + i * 250;
                if (i % 2 == 0) add_seg(WL, yy, WR - 250, yy + 230, 9, 0.3f, 0.0f, TAG_RAMP);
                else add_seg(WR, yy, WL + 250, yy + 230, 9, 0.3f, 0.0f, TAG_RAMP);
            }
            return y + 40 + ramps * 250 + 80;
        }
        case 2: {   // barres tournantes
            float dir = rng.chance(0.5f) ? 1.0f : -1.0f;
            add_spinner(300, y + 170, 150, 2.4f * dir, rng.r(0, PI_F));
            add_spinner(780, y + 170, 150, -2.4f * dir, rng.r(0, PI_F));
            add_spinner(540, y + 470, 210, -2.0f * dir, rng.r(0, PI_F));
            for (float yy : {y + 330.0f, y + 600.0f}) { add_peg(WL + 50, yy, 12, 0.5f, TAG_PEG); add_peg(WR - 50, yy, 12, 0.5f, TAG_PEG); }
            return y + 720;
        }
        case 3: {   // entonnoir + bumper qui sépare le peloton
            float open = gapc * 1.9f;
            add_seg(WL, y + 40, 540 - open / 2, y + 400, 9, 0.35f, 0.02f, TAG_RAMP);
            add_seg(WR, y + 40, 540 + open / 2, y + 400, 9, 0.35f, 0.02f, TAG_RAMP);
            add_peg(540, y + 560, 42, 1.1f, TAG_BUMPER);
            return y + 700;
        }
        case 4: {   // bumpers
            float xs1[3] = {270, 540, 810}, xs2[2] = {405, 675};
            for (float x : xs1) add_peg(x, y + 150, 46, 1.15f, TAG_BUMPER);
            for (float x : xs2) add_peg(x, y + 400, 46, 1.15f, TAG_BUMPER);
            add_peg(WL + 60, y + 400, 14, 0.5f, TAG_PEG);
            add_peg(WR - 60, y + 400, 14, 0.5f, TAG_PEG);
            return y + 600;
        }
        default: {  // chicanes : déflecteurs alternés + plots centraux
            for (int i = 0; i < 4; ++i) {
                float yy = y + 60 + i * 190;
                if (i % 2 == 0) add_seg(WL, yy, WL + 300, yy + 170, 9, 0.35f, 0.0f, TAG_RAMP);
                else add_seg(WR, yy, WR - 300, yy + 170, 9, 0.35f, 0.0f, TAG_RAMP);
                add_peg(540 + (i % 2 ? -110 : 110), yy + 90, 14, 0.5f, TAG_PEG);
            }
            return y + 60 + 4 * 190 + 40;
        }
        }
    }

    void init(uint64_t seed, int count) {
        rng.reseed(seed);
        n = count;
        radius = n <= 12 ? 38 : n <= 16 ? 36 : 34;
        w = World();
        w.gx = 0; w.gy = 2400;
        w.ball_rest = 0.55f;
        // parcours : plots d'abord (étale le peloton), entonnoir en dernier
        std::vector<int> mid = {1, 2, 4, 5};
        for (int i = (int)mid.size() - 1; i > 0; --i) std::swap(mid[i], mid[rng.i(0, i)]);
        mid.resize(4);                                   // 4 sections (ordre tiré au sort) : ~18 s de course
        float y = 420;
        y = build_section(0, y);
        for (int k : mid) y = build_section(k, y);
        y = build_section(3, y);
        y_end = y + 120;
        add_seg(WL, -400, WL, y_end + 1400, 10, 0.4f, 0.0f, TAG_WALL);
        add_seg(WR, -400, WR, y_end + 1400, 10, 0.4f, 0.0f, TAG_WALL);
        w.build_buckets(200, y_end + 1600);
        // grille de départ
        int per_row = 6;
        float sp = (WR - WL - 60) / per_row;
        for (int i = 0; i < n; ++i) {
            Ball b;
            b.r = radius; b.id = i;
            b.x = WL + 30 + sp * (i % per_row + 0.5f) + rng.r(-6, 6) + ((i / per_row) % 2) * sp * 0.25f;
            b.y = 260 - (i / per_row) * (2 * radius + 10);
            b.vx = rng.r(-30, 30);
            w.balls.push_back(b);
        }
        finished.assign(n, false);
        finish_t.assign(n, -1);
        best_y.assign(n, -1e9f);
        best_t.assign(n, 0);
    }

    // classement courant (arrivés d'abord, puis par avancée)
    std::vector<int> ranking() const {
        std::vector<int> r(n);
        for (int i = 0; i < n; ++i) r[i] = i;
        std::sort(r.begin(), r.end(), [&](int a, int b) {
            if (finished[a] != finished[b]) return (bool)finished[a];
            if (finished[a]) return finish_t[a] < finish_t[b];
            return w.balls[a].y > w.balls[b].y;
        });
        return r;
    }

    void step() {
        t += SIM_DT;
        w.ball_hits.clear();   // (World::step fait tourner les barres)
        w.wall_hits.clear();
        w.step(SIM_DT);
        for (auto& b : w.balls) {
            float v = len2(b.vx, b.vy);
            if (v > 1750) { b.vx *= 1750 / v; b.vy *= 1750 / v; }
        }
        for (const auto& h : w.wall_hits) {
            if (h.speed < 140) continue;
            int type = h.kind == 3 ? (h.tag == TAG_BUMPER ? MV_BUMPER : MV_PEG) : (h.tag == TAG_SPIN ? MV_SPIN : MV_WALL);
            ev.push_back({type, h.ball, -1, h.x, h.y, h.speed});
        }
        for (int i = 0; i < n; ++i) {
            Ball& b = w.balls[i];
            if (!b.on) continue;
            if (!finished[i] && b.y > y_end) {
                finished[i] = true;
                finish_t[i] = t;
                order.push_back(i);
                ev.push_back({MV_FINISH, i, (int)order.size(), b.x, y_end, 0});
            }
            if (b.y > y_end + 1300) b.on = false;
            // anti-blocage : une bille qui n'avance plus reçoit une pichenette
            if (b.y > best_y[i] + 25) { best_y[i] = b.y; best_t[i] = t; }
            else if (!finished[i] && t - best_t[i] > 1.8f) {
                b.vx += rng.r(-350, 350); b.vy -= rng.r(250, 450);
                best_t[i] = t;
                nudges++;
            }
        }
        if (t - sample_t >= 0.2f) {
            sample_t = t;
            int l = -1; float by = -1e9f;
            for (int i = 0; i < n; ++i) {
                float y = finished[i] ? 1e9f - finish_t[i] : w.balls[i].y;
                if (y > by) { by = y; l = i; }
            }
            if (order.empty() && leader >= 0 && l != leader) {
                lead_changes++;
                if (w.balls[l].y > y_end * 0.6f) late_changes++;
                ev.push_back({MV_LEAD, l, leader, w.balls[l].x, w.balls[l].y, 0});
            }
            leader = l;
        }
        if ((int)order.size() == n || (!order.empty() && t - finish_t[order[0]] > 14) || t > 70) {
            // les retardataires sont classés selon leur avancée
            for (int i : ranking()) if (!finished[i]) order.push_back(i);
            done = true;
            ev.push_back({MV_DONE, -1, -1, 0, 0, 0});
        }
    }

    float score() const {
        if (!done || order.empty() || finish_t[order[0]] < 0) return -1e9f;
        float W = finish_t[order[0]];
        float s = -std::fabs(W - 18.0f) * 1.2f;
        if (W < 14 || W > 27) s -= 10;
        s += std::min(lead_changes, 8) * 1.0f + std::min(late_changes, 2) * 2.5f;
        if (order.size() > 1 && finish_t[order[1]] > 0) {
            float m = finish_t[order[1]] - W;
            if (m < 0.35f) s += 4; else if (m < 0.8f) s += 2;
        }
        float last = 0;
        for (float f : finish_t) last = std::max(last, f);
        if (last - W > 8) s -= (last - W - 8);
        if (std::count(finish_t.begin(), finish_t.end(), -1.0f) > 0) s -= 6;
        if (nudges > 12) s -= 2;
        return s;
    }
};

class MarbleScene : public FootballScene {
    MarbleSim sim;
    int n = 0;
    float win_finish = -1, cam_y = 0, first_t = -1, table_t = -1;
    std::vector<float> top_x, top_a, peg_hit;
    std::vector<int> rank_now;
    float last_peg = -10, last_wall = -10;
    bool started = false, zones = false;

public:
    void setup(AnimContext& ctx) override {
        n = (int)teams.size();
        zones = cfg.integer("zones", 0) == 1;
        Rng pick(ctx.seed);
        uint64_t best_seed = 1;
        float best = -1e9f;
        for (int c = 0; c < cfg.integer("candidates_marble", 24); ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            MarbleSim m;
            m.init(s, n);
            while (!m.done) { m.step(); m.ev.clear(); }
            float sc = m.score();
            if (sc > best) { best = sc; best_seed = s; win_finish = m.finish_t[m.order[0]]; }
        }
        sim.init(best_seed, n);
        printf("[FOOT] Marble : %d équipes, meilleur scénario %.1f (vainqueur à %.1f s, parcours %.0f px)\n", n, best,
               win_finish, sim.y_end);
        if (getenv("FB_DEBUG")) {
            MarbleSim d; d.init(best_seed, n);
            while (!d.done) { d.step(); d.ev.clear(); }
            printf("[FOOT] Arrivées :");
            for (int i : d.order) printf(" %.1f", d.finish_t[i]);
            printf(" | %d changements de meneur (%d tardifs), %d pichenettes\n", d.lead_changes, d.late_changes, d.nudges);
        }
        ctx.metric("drama_score", best);
        ctx.metric("thumb_time", start_time + win_finish * 0.55f);
        outro_len = 6.2f;
        top_x.assign(n, DW / 2);
        top_a.assign(n, 0);
        cam_y = 600;
        world_dy = 960 - cam_y;
        rank_now = sim.ranking();          // la 1re image est dessinée avant le 1er update : billes visibles d'emblée
        fx.cull_max_y = 1e9f;
    }

    float desired_time_scale() override {
        if (win_finish > 0) {
            float d = (float)sim_time - win_finish;
            if (d > -0.55f && d < 0.15f) return 0.25f;
            int arrived = (int)sim.order.size();
            if (arrived >= 3 && !sim.done) return 3.5f;     // le reste du peloton en accéléré
        }
        return 1.0f;
    }
    void sim_step() override { sim.step(); sim_done = sim.done; }

    void after_frame(AnimContext& ctx, float dt) override {
        float t = (float)ctx.time;
        if (!started && t >= start_time - 0.15f) { started = true; ctx.sound("whistle_long", 0.75f); ctx.sound("airhorn", 0.35f); }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        // caméra : suit la première bille encore en course
        rank_now = sim.ranking();
        int follow = -1;
        for (int i : rank_now) if (!sim.finished[i] && sim.w.balls[i].on) { follow = i; break; }
        float target = follow >= 0 ? sim.w.balls[follow].y : sim.y_end;
        if (!sim.order.empty() && follow >= 0) target = std::min(target, sim.y_end + 150);
        target = std::max(target, 600.0f);
        cam_y = lerpf(cam_y, target, clamp01(dt * 3.2f));
        world_dy = 960 - cam_y;
        fx.cull_max_y = cam_y + 1400;
        // top 5 : positions horizontales animées
        for (int k = 0; k < n; ++k) {
            int i = rank_now[k];
            bool in = k < 5;
            float tx = 150 + k * 195;
            top_x[i] = top_a[i] <= 0.01f ? tx : lerpf(top_x[i], tx, clamp01(dt * 10));
            top_a[i] = lerpf(top_a[i], in ? 1.0f : 0.0f, clamp01(dt * 10));
        }
        if (sim_done && table_t < 0) { table_t = t + 0.3f; ctx.sound("whoosh_long", 0.6f); }
    }

    void handle(AnimContext& ctx, const Ev& e, float t) {
        switch (e.type) {
        case MV_PEG:
            if (t - last_peg > 0.035f) { last_peg = t; sound(ctx, "marble", 4, 0.10f + 0.20f * clamp01(e.amount / 900)); }
            break;
        case MV_BUMPER:
            ctx.sound("bounce_soft_1", 0.4f);
            fx.shock(e.x, e.y, 30, 110, 0.35f, accent, 1);
            break;
        case MV_SPIN:
            sound(ctx, "hit_soft", 2, 0.25f);
            break;
        case MV_WALL:
            if (t - last_wall > 0.05f) { last_wall = t; ctx.sound("marble_roll", 0.10f + 0.12f * clamp01(e.amount / 900)); }
            break;
        case MV_LEAD:
            if (t - first_t > 0.6f && sim.order.empty()) {
                toast("NEW LEADER: " + upper(teams[e.a].name), e.a, t, FB_GOLD);
                sound(ctx, "whoosh", 2, 0.45f);
                if (frng.chance(0.4f)) ctx.sound("crowd_cheer_small", 0.4f);
            }
            break;
        case MV_FINISH: {
            const Team& tm = teams[e.a];
            if (e.b == 1) {
                first_t = t;
                fx.confetti(e.x, e.y, 90, {tm.c1, tm.c2, WHITE, FB_GOLD}, 1300, PI_F * 0.5f);
                fx.shock(e.x, e.y, 30, 400, 0.7f, tm.glow, 1.5f);
                ctx.sound("airhorn_hype", 0.7f);
                ctx.sound("crowd_roar", 1.0f);
                ctx.sound("impact_bass", 0.6f);
                kick_shake(20);
                toast(upper(tm.name) + " WINS!", e.a, t, FB_GOLD);
            } else if (e.b <= 3) {
                ctx.sound("coin_1", 0.6f);
                fx.sparks(e.x, e.y, 20, tm.glow, 900);
                char buf[64];
                snprintf(buf, sizeof(buf), "%s %s", e.b == 2 ? "2ND" : "3RD", upper(tm.name).c_str());
                toast(buf, e.a, t, WHITE);
            } else {
                sound(ctx, "pop", 4, 0.25f);
            }
            break;
        }
        case MV_DONE:
            ctx.sound("whistle_final", 0.7f);
            break;
        }
    }

    // capsule tournée (rampe, barre) : rectangle arrondi dans le repère du segment
    void capsule(float ax, float ay, float bx, float by, float th, Color c) {
        float dx = bx - ax, dy = by - ay;
        float L = len2(dx, dy);
        rlPushMatrix();
        rlTranslatef((ax + bx) / 2, (ay + by) / 2, 0);
        rlRotatef(std::atan2(dy, dx) * RAD2DEG, 0, 0, 1);
        g.rrect(Rectangle{-L / 2 - th, -th, L + 2 * th, 2 * th}, th, c);
        rlPopMatrix();
    }

    void draw_world(AnimContext& ctx) override {
        float t = (float)ctx.time;
        float top = cam_y - 1000, bot = cam_y + 1100;
        // couloir
        g.rect(Rectangle{WL, std::max(-400.0f, top), WR - WL, bot - std::max(-400.0f, top)}, withA(rgba(6, 8, 16), 0.55f));
        // repères de distance
        for (float y = 1000; y < sim.y_end; y += 1000) {
            if (y < top || y > bot) continue;
            g.rect(Rectangle{WL + 12, y - 1.5f, 40, 3}, withA(WHITE, 0.4f));
            g.rect(Rectangle{WR - 52, y - 1.5f, 40, 3}, withA(WHITE, 0.4f));
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", (int)std::round(100 * y / sim.y_end));
            TextStyle st; st.kind = F_BOLD; st.size = 26; st.color = withA(WHITE, 0.5f); st.shadow = 0;
            draw_text(fonts, buf, WL + 70, y, st, 0, 0.0f);
        }
        // ligne d'arrivée en damier
        if (sim.y_end > top && sim.y_end < bot) {
            float sq = 24;
            for (int i = 0; WL + i * sq < WR; ++i)
                for (int r = 0; r < 2; ++r)
                    g.rect(Rectangle{WL + i * sq, sim.y_end - sq + r * sq, sq, sq}, ((i + r) % 2) ? WHITE : rgba(20, 20, 26));
            TextStyle st; st.size = 70; st.outline = 4; st.color = FB_GOLD;
            draw_text(fonts, "FINISH", DW / 2, sim.y_end + 70, st);
        }
        Color ramp = mixc(rgba(220, 225, 240), accent, 0.25f);
        for (const auto& s : sim.w.segs) {
            float y0 = std::min(s.ay, s.by) - 240, y1 = std::max(s.ay, s.by) + 240;
            if (y1 < top || y0 > bot) continue;
            if (s.tag == TAG_WALL) {
                g.rect(Rectangle{s.ax - 5, std::max(top, -400.0f), 10, bot - std::max(top, -400.0f)}, withA(mixc(WHITE, accent, 0.4f), 0.9f));
                continue;
            }
            Color c = s.tag == TAG_SPIN ? FB_GOLD : ramp;
            capsule(s.ax, s.ay, s.bx, s.by, s.t, c);
            if (s.rotating) { g.disc(s.px, s.py, 14, withA(rgba(20, 20, 30), 1)); g.ring_(s.px, s.py, 14, 3, FB_GOLD); }
        }
        for (const auto& p : sim.w.pegs) {
            if (p.y < top || p.y > bot) continue;
            if (p.tag == TAG_BUMPER) {
                g.disc_grad(p.x, p.y, p.r, mixc(accent, WHITE, 0.35f), scalec(accent, 0.6f), 1.5f);
                g.ring_(p.x, p.y, p.r - 6, 3, withA(WHITE, 0.8f));
            } else {
                g.disc(p.x, p.y, p.r, rgba(230, 234, 245));
            }
        }
        // billes (la première porte une couronne)
        int lead = rank_now.empty() ? -1 : rank_now[0];
        for (int k = n - 1; k >= 0 && k < (int)rank_now.size(); --k) {
            int i = rank_now[k];
            const Ball& b = sim.w.balls[i];
            if (!b.on || b.y < top || b.y > bot) continue;
            TokenFx f; f.tilt = b.ang * RAD2DEG;
            draw_token(g, teams[i], b.x, b.y, sim.radius, f);
            if (i == lead && !sim.done)
                if (const Texture2D* cr = icon("crown"))
                    g.tex_center(*cr, b.x, b.y - sim.radius - 22, 46, 36, WHITE);
        }
        (void)t;
    }

    void draw_world_glow(AnimContext&) override {
        float top = cam_y - 1000, bot = cam_y + 1100;
        for (const auto& p : sim.w.pegs)
            if (p.tag == TAG_BUMPER && p.y > top && p.y < bot) g.glow_at(p.x, p.y, p.r * 2.4f, withA(accent, 0.8f));
        for (const auto& s : sim.w.segs)
            if (s.tag == TAG_WALL) g.rect(Rectangle{s.ax - 10, top, 20, bot - top}, withA(accent, 0.7f));
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            if (b.on && b.y > top && b.y < bot) draw_token_glow(g, teams[i], b.x, b.y, sim.radius, 0.45f);
        }
    }

    void draw_table(float age) {
        float fade = clamp01(age / 0.3f);
        g.rect(Rectangle{0, 0, DW, DH}, withA(rgba(5, 7, 14), 0.97f * fade));
        TextStyle ts; ts.size = 92; ts.outline = 4;
        // peu d'équipes : lignes plus grandes et tableau centré dans l'écran
        float row_h = std::min(n <= 8 ? 120.0f : 64.0f, 1240.0f / n);
        float y0 = std::max(270.0f, (DH - n * row_h) / 2 - 80);
        draw_text(fonts, upper(cfg.str("table_title", "Final standings")), DW / 2, std::max(170.0f, y0 - 100), ts, 980, 0.5f, fade);
        for (int k = 0; k < n && k < (int)sim.order.size(); ++k) {
            int i = sim.order[k];
            float a = clamp01((age - 0.15f - k * 0.045f) / 0.25f);
            if (a <= 0) continue;
            float y = y0 + k * row_h + row_h / 2 + (1 - ease_out_cubic(a)) * 30;
            Color band = withA(WHITE, 0.06f);
            if (k == 0) band = withA(FB_GOLD, 0.30f);
            else if (zones && k < 4) band = withA(rgba(60, 130, 255), 0.22f);
            else if (zones && k >= n - 3) band = withA(rgba(255, 60, 70), 0.24f);
            g.rrect(Rectangle{70, y - row_h / 2 + 3, 940, row_h - 6}, 12, mulA(band, a));
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", k + 1);
            TextStyle rs; rs.size = row_h * 0.62f; rs.color = k == 0 ? FB_GOLD : WHITE;
            draw_text(fonts, buf, 125, y, rs, 0, 0.5f, a);
            float cx = 160 + row_h * 0.5f;
            TokenFx f; f.alpha = a; f.shadow = 0.4f;
            draw_token(g, teams[i], cx, y, row_h * 0.40f, f);
            TextStyle ns; ns.size = row_h * 0.60f; ns.spacing = 1;
            draw_text(fonts, upper(teams[i].name), cx + row_h * 0.5f + 14, y, ns, 860 - row_h - (zones ? 260 : 0), 0.0f, a);
            if (zones && (k < 4 || k >= n - 3)) {
                TextStyle zs; zs.kind = F_BOLD; zs.size = row_h * 0.34f; zs.shadow = 0;
                zs.color = k < 4 ? rgba(130, 180, 255) : rgba(255, 120, 120);
                draw_text(fonts, k < 4 ? "CHAMPIONS LEAGUE" : "RELEGATED", 990, y, zs, 0, 1.0f, a);
            }
        }
        float kc = clamp01((age - 1.4f) / 0.3f);
        if (kc > 0) {
            std::string cta = upper(cfg.str("cta", "Where did yours finish?"));
            TextStyle c; c.kind = F_BOLD; c.size = 44; c.shadow = 0; c.color = rgba(12, 14, 24);
            float tw = text_width(fonts, cta, c);
            float by = std::min(1640.0f, y0 + n * row_h + 70);
            g.rrect(Rectangle{DW / 2 - tw / 2 - 60, by - 44, tw + 120, 88}, 44, withA(WHITE, 0.96f * kc));
            draw_text(fonts, cta, DW / 2, by + 2, c, 0, 0.5f, kc);
        }
    }

    void draw_hud(AnimContext& ctx) override {
        float t = (float)ctx.time;
        if (table_t >= 0 && t >= table_t) { draw_table(t - table_t); return; }
        g.vgrad_rect(Rectangle{0, 0, DW, 560}, withA(rgba(4, 6, 14), 0.92f), true);
        header(t);
        // top 5 en direct
        for (int k = 0; k < n && k < (int)rank_now.size(); ++k) {
            int i = rank_now[k];
            if (top_a[i] < 0.02f) continue;
            float x = top_x[i], y = 482;
            TokenFx f; f.alpha = top_a[i]; f.scale = k == 0 ? 1.12f : 1.0f;
            draw_token(g, teams[i], x, y, 38, f);
            g.disc(x - 30, y - 30, 17, withA(k == 0 ? FB_GOLD : WHITE, top_a[i]));
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", k + 1);
            TextStyle st; st.kind = F_BOLD; st.size = 24; st.color = rgba(12, 14, 24); st.shadow = 0;
            draw_text(fonts, buf, x - 30, y - 29, st, 0, 0.5f, top_a[i]);
        }
        // mini-carte de progression (bord gauche)
        float mx = 30, my0 = 600, my1 = 1500;
        g.rrect(Rectangle{mx - 4, my0, 8, my1 - my0}, 4, withA(WHITE, 0.25f));
        for (int i = 0; i < n; ++i) {
            const Ball& b = sim.w.balls[i];
            float p = clamp01(b.y / sim.y_end);
            g.disc(mx, my0 + p * (my1 - my0), 7, withA(teams[i].glow, 0.9f));
        }
        if (time_scale > 1.5f && !sim.done) chip("FAST FORWARD >>", DW / 2, 600, withA(WHITE, 0.92f), rgba(12, 14, 24), 1, 30);
        draw_toasts(t, 1600, 1.6f);
    }
};

}  // namespace

Scene* make_marble_scene() { return new MarbleScene(); }
