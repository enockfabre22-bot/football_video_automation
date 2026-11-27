// ============================================================================
//  bigmatch_scene.cpp — « Big Match » : le match du soir simulé en ~30 s, vu de dessus.
//  Règle comprise en une seconde : 2 équipes (gardien + 3 joueurs = leurs écussons), un ballon,
//  deux buts. Ballon dans le filet = but, score et chrono 0'->90' en haut comme à la télé.
//  Égalité à 90' -> séance de tirs au but. Le scénario le plus spectaculaire (action dès la
//  1re seconde, buts réguliers, égalisation, but tardif) est choisi parmi N candidats.
// ============================================================================
#include "fb_common.h"

namespace {

const float PX0 = 80, PX1 = 1000, PY0 = 470, PY1 = 1560;   // lignes du terrain
const float PW = PX1 - PX0, PH = PY1 - PY0;
const float CX = (PX0 + PX1) / 2, CY = (PY0 + PY1) / 2;
const float GOAL_HW = 125, GOAL_D = 46, POST_R = 9;
const float PR = 42, BR = 19, GK_R = 38;
const float MATCH_T = 27.0f;            // 90 minutes en 27 s de simulation
const float CELEB_T = 1.7f;             // célébration après un but (le chrono tourne)
const float PEN_SPOT = 175;
const int NP = 4;                        // par équipe : 0 = gardien, 1..3 joueurs de champ
const float FORM[NP][2] = {{0.5f, 0.03f}, {0.5f, 0.22f}, {0.27f, 0.50f}, {0.73f, 0.50f}};

enum { PH_PLAY, PH_CELEB, PH_PENS, PH_DONE };
enum { E_KICKOFF, E_PASS, E_SHOT, E_GOAL, E_SAVE, E_CATCH, E_POST, E_WALL, E_HALF, E_FULL,
       E_PENS, E_PEN_KICK, E_PEN_RES, E_END };
enum { PEN_GOAL, PEN_SAVE, PEN_MISS };

struct Ev { int type, team, player; float x, y, v; int res; };
struct Pl { float x = 0, y = 0, vx = 0, vy = 0, cd = 0; };

// y d'attaque : l'équipe 0 (domicile) défend le but du bas et attaque vers le haut
inline float own_line(int t) { return t == 0 ? PY1 : PY0; }
inline float opp_line(int t) { return t == 0 ? PY0 : PY1; }
inline float to_y(int t, float fy) { return t == 0 ? PY1 - fy * PH : PY0 + fy * PH; }
inline float prog(int t, float y) { return t == 0 ? (PY1 - y) / PH : (y - PY0) / PH; }

struct MatchSim {
    Rng rng{1};
    float t = 0, phase_t = 0, shot_fly = 0;
    int phase = PH_PLAY;
    Pl pl[2][NP];
    float bx = CX, by = CY, bvx = 0, bvy = 0, bspin = 0;
    int holder_team = -1;                // gardien qui tient le ballon
    float hold_t = 0;
    int last_team = -1, last_player = -1;
    int score[2] = {0, 0};
    int kickoff_team = 0;
    float gk_dive[2] = {560, 560};
    bool half_done = false;
    std::vector<Ev> ev;
    // bilan (choix du scénario)
    float first_shot = -1, first_goal = -1, last_shot = 0, max_gap = 0;
    std::vector<float> goal_times;
    std::vector<int> goal_team;
    int saves = 0, posts = 0, equalizers = 0, lead_changes = 0, shots = 0, kicks = 0;
    int leader = -1;
    bool done = false;
    int winner = -1;
    float end_t = -1;
    // tirs au but
    int pen_k = -1, pen_first = 0, pen_sub = 0, pen_res = 0, pen_side = 1;
    float pen_tx = CX, pen_gk_tx = CX, pen_t = 0;
    std::vector<int> pens[2];
    float decisive_pen_t = -1;

    void place_kickoff(int kicker) {
        kickoff_team = kicker;
        bx = CX; by = CY; bvx = bvy = 0;
        holder_team = -1;
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < NP; ++i) {
                Pl& p = pl[s][i];
                float fy = i == 0 ? 0.03f : FORM[i][1] * 0.78f;
                p.x = PX0 + FORM[i][0] * PW;
                p.y = to_y(s, fy);
                p.vx = p.vy = 0;
                p.cd = 0;
            }
        // le tireur du coup d'envoi se place au rond central, un partenaire à côté
        Pl& k = pl[kicker][2];
        k.x = CX - 30; k.y = CY + (kicker == 0 ? 1 : -1) * (PR + BR + 4);
        Pl& m = pl[kicker][3];
        m.x = CX + 150; m.y = CY + (kicker == 0 ? 1 : -1) * 60;
    }

    void init(uint64_t seed) {
        rng.reseed(seed);
        gk_dive[0] = rng.r(400, 540);
        gk_dive[1] = rng.r(400, 540);
        place_kickoff(rng.i(0, 1));
        ev.push_back({E_KICKOFF, kickoff_team, 2, CX, CY, 0, 0});
    }

    float minute() const { return std::min(90.0f, t / MATCH_T * 90.0f); }

    void set_ball_vel(float dx, float dy, float speed, float noise) {
        float a = std::atan2(dy, dx) + rng.r(-noise, noise);
        bvx = std::cos(a) * speed;
        bvy = std::sin(a) * speed;
    }

    // un joueur de champ touche le ballon : tir, passe ou conduite
    void kick(int s, int i) {
        Pl& p = pl[s][i];
        float gy = opp_line(s);
        float D = len2(CX - bx, gy - by);
        last_team = s; last_player = i;
        p.cd = 0.30f;
        kicks++;
        bool shoot = D < 470 ? rng.chance(0.65f) : D < 650 ? rng.chance(0.12f) : false;
        if (shoot) {
            int side = rng.chance(0.5f) ? -1 : 1;
            float tx = CX + side * rng.r(GOAL_HW * 0.2f, GOAL_HW * 0.98f);
            set_ball_vel(tx - bx, gy - by, rng.r(1350, 1800), 0.05f + 0.10f * D / 470);
            shots++;
            if (first_shot < 0) first_shot = t;
            max_gap = std::max(max_gap, t - last_shot);
            last_shot = t;
            shot_fly = rng.r(0.22f, 0.42f);
            ev.push_back({E_SHOT, s, i, bx, by, len2(bvx, bvy), 0});
            return;
        }
        // passe vers un partenaire plus avancé, sinon conduite vers le but
        int best = -1;
        float best_v = -1e9f;
        for (int j = 1; j < NP; ++j) {
            if (j == i) continue;
            const Pl& m = pl[s][j];
            float v = prog(s, m.y) - prog(s, p.y) + rng.r(0, 0.25f);
            if (v > best_v) { best_v = v; best = j; }
        }
        if (best >= 0 && best_v > -0.05f && rng.chance(0.6f)) {
            const Pl& m = pl[s][best];
            float tx = m.x + m.vx * 0.25f, ty = m.y + m.vy * 0.25f + (s == 0 ? -70 : 70);
            float d = len2(tx - bx, ty - by);
            set_ball_vel(tx - bx, ty - by, clampf(d * 1.9f, 650, 1250), 0.08f);
            ev.push_back({E_PASS, s, i, bx, by, 0, 0});
        } else {
            set_ball_vel(CX + rng.r(-200, 200) - bx, gy - by, rng.r(520, 720), 0.45f);
            ev.push_back({E_PASS, s, i, bx, by, 0, 1});
        }
    }

    void goal(int s) {
        score[s]++;
        goal_times.push_back(t);
        goal_team.push_back(s);
        if (first_goal < 0) first_goal = t;
        int l = score[0] > score[1] ? 0 : score[1] > score[0] ? 1 : -1;
        if (l == -1) equalizers++;
        if (l >= 0 && leader >= 0 && l != leader) lead_changes++;
        if (l >= 0) leader = l;
        phase = PH_CELEB;
        phase_t = 0;
        holder_team = -1;
        ev.push_back({E_GOAL, s, last_team == s ? last_player : -1, bx, by, 0, 0});
    }

    void step_ball(float dt) {
        if (holder_team >= 0) return;
        bx += bvx * dt; by += bvy * dt;
        float sp = len2(bvx, bvy);
        bspin += sp * dt / BR;
        float k = std::exp(-0.75f * dt);
        bvx *= k; bvy *= k;
        if (sp < 12) bvx = bvy = 0;
        // dans un but : filet du fond et filets latéraux
        bool in_mouth = std::fabs(bx - CX) < GOAL_HW - BR * 0.4f;
        bool in_goal = by < PY0 || by > PY1;
        if (in_goal) {
            bx = clampf(bx, CX - GOAL_HW + BR, CX + GOAL_HW - BR);
            float back = by < PY0 ? PY0 - GOAL_D + BR : PY1 + GOAL_D - BR;
            if ((by < PY0 && by < back) || (by > PY1 && by > back)) { by = back; bvy *= -0.15f; bvx *= 0.5f; }
            if (phase == PH_PLAY) {
                if (by < PY0 - BR) goal(0);
                else if (by > PY1 + BR) goal(1);
            }
            return;
        }
        if (bx < PX0 + BR) { bx = PX0 + BR; if (bvx < 0) { if (-bvx > 400) ev.push_back({E_WALL, -1, -1, bx, by, -bvx, 0}); bvx = -bvx * 0.75f; } }
        if (bx > PX1 - BR) { bx = PX1 - BR; if (bvx > 0) { if (bvx > 400) ev.push_back({E_WALL, -1, -1, bx, by, bvx, 0}); bvx = -bvx * 0.75f; } }
        if (!in_mouth) {
            if (by < PY0 + BR) { by = PY0 + BR; if (bvy < 0) { if (-bvy > 400) ev.push_back({E_WALL, -1, -1, bx, by, -bvy, 0}); bvy = -bvy * 0.7f; } }
            if (by > PY1 - BR) { by = PY1 - BR; if (bvy > 0) { if (bvy > 400) ev.push_back({E_WALL, -1, -1, bx, by, bvy, 0}); bvy = -bvy * 0.7f; } }
        }
        // poteaux
        for (int g = 0; g < 2; ++g)
            for (int side = -1; side <= 1; side += 2) {
                float px = CX + side * GOAL_HW, py = g == 0 ? PY0 : PY1;
                float dx = bx - px, dy = by - py, d = len2(dx, dy);
                if (d < BR + POST_R && d > 1e-4f) {
                    float nx = dx / d, ny = dy / d;
                    bx = px + nx * (BR + POST_R); by = py + ny * (BR + POST_R);
                    float vn = bvx * nx + bvy * ny;
                    if (vn < 0) {
                        if (-vn > 600 && phase == PH_PLAY) { posts++; ev.push_back({E_POST, last_team, -1, px, py, -vn, 0}); }
                        bvx -= 1.85f * vn * nx; bvy -= 1.85f * vn * ny;
                    }
                }
            }
    }

    void collide_ball_player(Pl& p, float r) {
        float dx = bx - p.x, dy = by - p.y, d = len2(dx, dy);
        float minD = r + BR;
        if (d >= minD || d < 1e-4f) return;
        float nx = dx / d, ny = dy / d;
        bx = p.x + nx * minD; by = p.y + ny * minD;
        float rvx = bvx - p.vx, rvy = bvy - p.vy, vn = rvx * nx + rvy * ny;
        if (vn < 0) { bvx -= 1.4f * vn * nx; bvy -= 1.4f * vn * ny; }
    }

    void move_player(Pl& p, float tx, float ty, float speed, float dt, float accel = 7.0f) {
        float dx = tx - p.x, dy = ty - p.y, d = len2(dx, dy);
        float want = d < 8 ? 0 : std::min(speed, d * 4.0f);
        float dvx = d > 1e-3f ? dx / d * want : 0, dvy = d > 1e-3f ? dy / d * want : 0;
        p.vx = lerpf(p.vx, dvx, clamp01(accel * dt));
        p.vy = lerpf(p.vy, dvy, clamp01(accel * dt));
        p.x += p.vx * dt; p.y += p.vy * dt;
        p.x = clampf(p.x, PX0 + PR * 0.7f, PX1 - PR * 0.7f);
        p.y = clampf(p.y, PY0 + PR * 0.5f, PY1 - PR * 0.5f);
    }

    void step_players(float dt, bool celebrate) {
        float bpx = bx + bvx * 0.14f, bpy = by + bvy * 0.14f;
        for (int s = 0; s < 2; ++s) {
            // joueur le plus proche du ballon : il va le chercher
            int chaser = -1;
            float best = 1e9f;
            if (!celebrate && holder_team != s) {
                for (int i = 1; i < NP; ++i) {
                    float d = len2(pl[s][i].x - bpx, pl[s][i].y - bpy);
                    if (d < best) { best = d; chaser = i; }
                }
                if (holder_team == 1 - s) chaser = -1;
                // ballon lent devant notre but : c'est au gardien de sortir, les joueurs ne l'encombrent pas
                if (std::fabs(by - own_line(s)) < 230 && std::fabs(bx - CX) < 260 && len2(bvx, bvy) < 320) chaser = -1;
            }
            float bp = prog(s, by);
            bool attacking = last_team == s;
            for (int i = 1; i < NP; ++i) {
                Pl& p = pl[s][i];
                p.cd = std::max(0.0f, p.cd - dt);
                if (i == chaser) {
                    move_player(p, bpx, bpy, 560, dt, 9.0f);
                    continue;
                }
                float fx = FORM[i][0], fy = FORM[i][1];
                if (celebrate) fy *= 0.78f;
                else {
                    fy = clampf(fy + (bp - 0.5f) * 0.55f + (attacking ? 0.08f : -0.04f), 0.08f, 0.9f);
                    fx = clampf(fx + ((bx - PX0) / PW - 0.5f) * 0.35f, 0.12f, 0.88f);
                }
                move_player(p, PX0 + fx * PW, to_y(s, fy), celebrate ? 300 : 420, dt);
            }
            // gardien : sur sa ligne, suit le ballon, plonge sur les tirs
            Pl& g = pl[s][0];
            g.cd = std::max(0.0f, g.cd - dt);
            float line = own_line(s), sgn = s == 0 ? -1.0f : 1.0f;   // sgn : vers le terrain
            float gx = CX + (bx - CX) * 0.45f, gy = line + sgn * 40;
            float spd = 240;
            float vtoward = s == 0 ? bvy : -bvy;   // vitesse du ballon vers ce but
            if (!celebrate && holder_team < 0 && vtoward > 150) {
                float tc = (line - by) / (bvy != 0 ? bvy : 1);
                if (tc > 0 && tc < 1.2f) { gx = bx + bvx * tc; if (len2(bvx, bvy) > 900) spd = gk_dive[s]; }
            }
            float dist_line = std::fabs(by - line);
            if (!celebrate && holder_team < 0 && dist_line < 230 && len2(bvx, bvy) < 320 && std::fabs(bx - CX) < 260) {
                gx = bx; gy = by - sgn * (GK_R + BR);   // sort cueillir un ballon lent
                spd = 380;
            }
            gx = clampf(gx, CX - GOAL_HW - 30, CX + GOAL_HW + 30);
            gy = s == 0 ? clampf(gy, PY1 - 240, PY1 - 22) : clampf(gy, PY0 + 22, PY0 + 240);
            move_player(g, gx, gy, spd, dt, 12.0f);
        }
        // séparation des joueurs
        for (int a = 0; a < 2 * NP; ++a)
            for (int b = a + 1; b < 2 * NP; ++b) {
                Pl& A = pl[a / NP][a % NP];
                Pl& B = pl[b / NP][b % NP];
                float dx = B.x - A.x, dy = B.y - A.y, d = len2(dx, dy), minD = 2 * PR - 4;
                if (d < minD && d > 1e-4f) {
                    float push = (minD - d) / 2, nx = dx / d, ny = dy / d;
                    A.x -= nx * push; A.y -= ny * push;
                    B.x += nx * push; B.y += ny * push;
                }
            }
    }

    void touches() {
        if (holder_team >= 0) return;
        // gardiens
        for (int s = 0; s < 2; ++s) {
            Pl& g = pl[s][0];
            float d = len2(bx - g.x, by - g.y);
            if (d > GK_R + BR + 1 || g.cd > 0) continue;
            float vtoward = s == 0 ? bvy : -bvy;
            float sp = len2(bvx, bvy);
            last_team = s; last_player = 0;
            if (vtoward > 300 && sp > 700) {
                saves++;
                if (rng.chance(0.4f)) {           // parade : le ballon repart
                    float sgn = s == 0 ? -1.0f : 1.0f;
                    bvy = sgn * std::fabs(bvy) * rng.r(0.35f, 0.6f);
                    bvx = bvx * 0.3f + rng.r(-650, 650);
                    ev.push_back({E_SAVE, s, 0, bx, by, sp, 0});
                    g.cd = 0.4f;
                    return;
                }
                ev.push_back({E_SAVE, s, 0, bx, by, sp, 1});
            } else {
                ev.push_back({E_CATCH, s, 0, bx, by, sp, 0});
            }
            holder_team = s;                       // arrêt / prise en main : le gardien relance
            hold_t = rng.r(0.45f, 0.75f);
            bvx = bvy = 0;
            return;
        }
        // un tir part « au-dessus » des joueurs de champ un court instant (sinon il est contré à bout portant)
        if (shot_fly > 0) return;
        // joueurs de champ : celui qui touche le ballon le frappe (cooldown sinon simple contact)
        int bs = -1, bi = -1;
        float bd = 1e9f;
        for (int s = 0; s < 2; ++s)
            for (int i = 1; i < NP; ++i) {
                float d = len2(bx - pl[s][i].x, by - pl[s][i].y);
                if (d < PR + BR + 1 && pl[s][i].cd <= 0 && d < bd) { bd = d; bs = s; bi = i; }
            }
        if (bs >= 0) { kick(bs, bi); return; }
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < NP; ++i)
                if (pl[s][i].cd < 0.12f)       // le tireur / le gardien qui relance ne bloque pas son propre ballon
                    collide_ball_player(pl[s][i], i == 0 ? GK_R : PR);
    }

    void step_hold(float dt) {
        int s = holder_team;
        Pl& g = pl[s][0];
        float sgn = s == 0 ? -1.0f : 1.0f;
        bx = g.x; by = g.y + sgn * (GK_R + BR - 6);
        hold_t -= dt;
        if (hold_t > 0) return;
        // relance vers le partenaire le plus avancé et démarqué (au moins 220 px du gardien)
        int j = 1;
        float best = -1e9f;
        for (int k = 1; k < NP; ++k) {
            const Pl& c = pl[s][k];
            float v = prog(s, c.y) + rng.r(0, 0.2f) - (len2(c.x - g.x, c.y - g.y) < 220 ? 5 : 0);
            if (v > best) { best = v; j = k; }
        }
        const Pl& m = pl[s][j];
        holder_team = -1;
        g.cd = 0.6f;
        last_team = s; last_player = 0;
        float d = len2(m.x - bx, m.y - by);
        set_ball_vel(m.x - bx, m.y + sgn * 80 - by, clampf(d * 1.6f, 700, 1300), 0.12f);
        by += sgn * 6;
        ev.push_back({E_PASS, s, 0, bx, by, 0, 2});
    }

    // ---------------- tirs au but ----------------
    bool pens_decided() const {
        int n0 = (int)pens[0].size(), n1 = (int)pens[1].size();
        int g0 = 0, g1 = 0;
        for (int r : pens[0]) g0 += r == PEN_GOAL;
        for (int r : pens[1]) g1 += r == PEN_GOAL;
        if (n0 <= 5 && n1 <= 5) {
            int left0 = 5 - n0, left1 = 5 - n1;
            if (g0 + left0 < g1 || g1 + left1 < g0) return true;
            return n0 == 5 && n1 == 5 && g0 != g1;
        }
        return n0 == n1 && g0 != g1;
    }
    int pens_goals(int s) const { int g = 0; for (int r : pens[s]) g += r == PEN_GOAL; return g; }

    void start_pen() {
        pen_k++;
        int shooter = (pen_first + pen_k) % 2;
        pen_sub = 0; pen_t = 0;
        float r = rng.f();
        pen_res = r < 0.74f ? PEN_GOAL : r < 0.93f ? PEN_SAVE : PEN_MISS;
        pen_side = rng.chance(0.5f) ? -1 : 1;
        if (pen_res == PEN_MISS) pen_tx = CX + pen_side * (GOAL_HW + rng.r(18, 50));
        else pen_tx = CX + pen_side * rng.r(45, GOAL_HW - 24);
        if (pen_res == PEN_SAVE) pen_gk_tx = pen_tx;
        else if (rng.chance(0.6f)) pen_gk_tx = CX - pen_side * rng.r(40, 90);     // mauvais côté
        else pen_gk_tx = CX + pen_side * rng.r(20, 55);                           // bon côté, trop court
        // le tireur au point de penalty, le gardien adverse sur sa ligne (but du haut)
        Pl& sh = pl[shooter][2];
        sh.x = CX + pen_side * -60; sh.y = PY0 + PEN_SPOT + 150; sh.vx = sh.vy = 0;
        Pl& gk = pl[1 - shooter][0];
        gk.x = CX; gk.y = PY0 + 14; gk.vx = gk.vy = 0;
        bx = CX; by = PY0 + PEN_SPOT; bvx = bvy = 0;
        ev.push_back({E_PEN_KICK, shooter, 2, bx, by, 0, pen_k});
    }

    void step_pens(float dt) {
        if (pen_k < 0) { if (phase_t > 1.8f) start_pen(); return; }
        int shooter = (pen_first + pen_k) % 2;
        Pl& sh = pl[shooter][2];
        Pl& gk = pl[1 - shooter][0];
        pen_t += dt;
        const float RUN = 0.32f, FLY = 0.28f, AFTER = 0.46f;
        if (pen_sub == 0) {                       // course d'élan
            float k = clamp01(pen_t / RUN);
            sh.x = lerpf(CX + pen_side * -60, CX + pen_side * -10, k);
            sh.y = lerpf(PY0 + PEN_SPOT + 150, PY0 + PEN_SPOT + PR + BR - 4, k);
            if (pen_t >= RUN) { pen_sub = 1; pen_t = 0; ev.push_back({E_SHOT, shooter, 2, bx, by, 1500, 1}); }
        } else if (pen_sub == 1) {                // vol du ballon + plongeon
            float k = clamp01(pen_t / FLY);
            float ty = pen_res == PEN_MISS ? PY0 - 30 : pen_res == PEN_SAVE ? PY0 + 16 : PY0 - 26;
            bx = lerpf(CX, pen_tx, k); by = lerpf(PY0 + PEN_SPOT, ty, k);
            bspin += 40 * dt;
            float gk_k = clamp01((pen_t - 0.04f) / (FLY * 0.9f));
            gk.x = lerpf(CX, pen_gk_tx, ease_out_cubic(gk_k));
            if (pen_t >= FLY) {
                pen_sub = 2; pen_t = 0;
                pens[shooter].push_back(pen_res);
                if (pen_res == PEN_SAVE) { bvx = pen_side * 220; bvy = 520; saves++; }
                else { bvx = 0; bvy = pen_res == PEN_GOAL ? -60 : -200; }
                ev.push_back({E_PEN_RES, shooter, 2, bx, by, 0, pen_res});
                if (pens_decided()) {
                    winner = pens_goals(0) > pens_goals(1) ? 0 : 1;
                    decisive_pen_t = t;
                }
            }
        } else {                                  // ballon qui finit sa course
            bx += bvx * dt; by += bvy * dt;
            bvx *= std::exp(-3 * dt); bvy *= std::exp(-3 * dt);
            if (pen_res == PEN_GOAL) by = std::max(by, PY0 - GOAL_D + BR);
            if (pen_t >= AFTER) {
                if (winner >= 0) { phase = PH_DONE; done = true; end_t = t; ev.push_back({E_END, winner, -1, CX, CY, 0, 1}); }
                else start_pen();
            }
        }
    }

    void step() {
        if (done) return;
        float dt = SIM_DT;
        t += dt;
        phase_t += dt;
        shot_fly = std::max(0.0f, shot_fly - dt);
        if (phase == PH_PENS) { step_pens(dt); return; }
        if (!half_done && t >= MATCH_T / 2 && phase == PH_PLAY) {
            half_done = true;
            ev.push_back({E_HALF, -1, -1, CX, CY, 0, 0});
        }
        if (t >= MATCH_T && phase == PH_PLAY) {
            ev.push_back({E_FULL, -1, -1, CX, CY, 0, 0});
            max_gap = std::max(max_gap, t - last_shot);
            if (score[0] != score[1]) {
                winner = score[0] > score[1] ? 0 : 1;
                phase = PH_DONE; done = true; end_t = t;
                ev.push_back({E_END, winner, -1, CX, CY, 0, 0});
            } else {
                phase = PH_PENS; phase_t = 0; pen_first = rng.i(0, 1);
                ev.push_back({E_PENS, -1, -1, CX, CY, 0, 0});
            }
            return;
        }
        if (phase == PH_CELEB) {
            step_players(dt, true);
            step_ball(dt);
            if (phase_t >= CELEB_T) {
                if (t >= MATCH_T) { phase = PH_PLAY; return; }   // but à la dernière minute : coup de sifflet final
                int conceded = goal_team.back() == 0 ? 1 : 0;
                place_kickoff(conceded);
                phase = PH_PLAY; phase_t = 0;
                last_shot = t;
                ev.push_back({E_KICKOFF, conceded, 2, CX, CY, 0, 1});
            }
            return;
        }
        step_players(dt, false);
        if (holder_team >= 0) step_hold(dt);
        else { step_ball(dt); if (phase == PH_PLAY) touches(); }
    }

    // ---- qualité du scénario ----
    float score_fn(bool want_pens) const {
        if (!done) return -1e9f;
        int G = score[0] + score[1];
        float s = 0;
        if (G < 2) s -= 12;
        s -= std::fabs(G - 4.0f) * 1.6f;
        if (G > 6) s -= 4.0f * (G - 6);
        s += first_shot >= 0 && first_shot < 2.0f ? 5 : first_shot >= 0 && first_shot < 3.5f ? 2 : -4;
        s += first_goal >= 0 && first_goal < 7 ? 3 : first_goal >= 0 && first_goal < 12 ? 1 : -2;
        s += std::min(equalizers, 3) * 2.5f + std::min(lead_changes, 2) * 3.0f;
        if (max_gap > 6) s -= (max_gap - 6) * 1.5f;
        s += std::min(saves, 4) * 0.7f + std::min(posts, 2) * 1.0f;
        bool draw = score[0] == score[1];
        if (!goal_times.empty() && goal_times.back() / MATCH_T * 90 >= 78) {
            int a = 0, b = 0;   // le dernier but a-t-il fait basculer le résultat ?
            for (size_t k = 0; k + 1 < goal_team.size(); ++k) (goal_team[k] == 0 ? a : b)++;
            int before = a > b ? 0 : b > a ? 1 : -1;
            int after = score[0] > score[1] ? 0 : score[1] > score[0] ? 1 : -1;
            if (before != after) s += 6;
        }
        int margin = std::abs(score[0] - score[1]);
        if (!draw) {
            if (margin == 1) s += 4;
            if (margin >= 3) s -= 6;
            if (want_pens) s -= 6;
        } else {
            if (!want_pens) s -= 12;
            else {
                s += 8;
                int n = (int)(pens[0].size() + pens[1].size());
                if (n >= 9) s += 4;           // décidé au dernier tir ou en mort subite
                if (n > 14) s -= 3;
            }
        }
        return s;
    }
};

class BigMatchScene : public FootballScene {
    MatchSim sim;
    std::vector<float> goal_t;      // instants (simulation) des buts du scénario retenu
    float decisive_t = -1, end_t = -1, pen_decisive = -1;
    bool want_pens = false;
    // présentation
    float score_pop[2] = {0, 0};
    float goal_banner_t = -10;
    int goal_banner_team = -1;
    struct Banner { std::string text; Color c; float t0; };
    std::vector<Banner> banners;
    bool whistle = false;
    float win_t = -1;
    bool celebrated = false, thumb_set = false;
    float cam_zoom = 1, cam_fx = CX, cam_fy = CY, cam_sy = CY;
    std::vector<Vector2> trail;
    float net_shake[2] = {0, 0};

public:
    void setup(AnimContext& ctx) override {
        if (teams.size() < 2) { printf("[FOOT] Big match : il faut 2 équipes\n"); return; }
        start_time = cfg.num("start_time", 0.0f);
        outro_len = 4.6f;
        ctx.metric("thumb_time", 6.0);
        Rng pick(ctx.seed);
        want_pens = cfg.num("pens_chance", 0.22f) > pick.f();
        uint64_t best_seed = 1;
        float best = -1e9f;
        int candidates = cfg.integer("candidates", 80);
        int goals_hist[12] = {0};
        double sum_shots = 0, sum_saves = 0, sum_posts = 0, sum_kicks = 0;
        for (int c = 0; c < candidates; ++c) {
            uint64_t s = pick.next() + ((uint64_t)pick.next() << 32);
            MatchSim m;
            m.init(s);
            while (!m.done && m.t < 90) { m.step(); m.ev.clear(); }
            goals_hist[std::min(11, m.score[0] + m.score[1])]++;
            sum_shots += m.shots; sum_saves += m.saves; sum_posts += m.posts; sum_kicks += m.kicks;
            float sc = m.score_fn(want_pens);
            if (sc > best) { best = sc; best_seed = s; }
        }
        sim.init(best_seed);
        {   // pré-calcul des instants clés du scénario retenu (ralentis, miniature)
            MatchSim m;
            m.init(best_seed);
            while (!m.done && m.t < 90) { m.step(); m.ev.clear(); }
            goal_t = m.goal_times;
            end_t = m.end_t;
            pen_decisive = m.decisive_pen_t;
            if (!goal_t.empty()) decisive_t = goal_t.back();
            printf("[FOOT] Big match : %d-%d%s, buts à", m.score[0], m.score[1],
                   m.pens[0].empty() ? "" : " (tirs au but)");
            for (float g : goal_t) printf(" %.0f'", g / MATCH_T * 90);
            printf(", 1er tir %.1f s, score %.1f\n", m.first_shot, best);
            if (getenv("FB_DEBUG")) {
                printf("[FOOT] buts par match sur %d candidats :", candidates);
                for (int k = 0; k < 12; ++k) if (goals_hist[k]) printf(" %d:%d", k, goals_hist[k]);
                printf("  | moy. tirs %.1f arrets %.1f poteaux %.1f touches %.1f\n", sum_shots / candidates,
                       sum_saves / candidates, sum_posts / candidates, sum_kicks / candidates);
            }
        }
        ctx.metric("drama_score", best);
    }

    float desired_time_scale() override {
        float st = (float)sim_time;
        for (size_t k = 0; k < goal_t.size(); ++k) {
            bool decisive = k + 1 == goal_t.size() && goal_t[k] / MATCH_T * 90 >= 70;
            float pre = decisive ? 0.55f : 0.32f;
            if (st > goal_t[k] - pre && st < goal_t[k] + 0.05f) return decisive ? 0.3f : 0.5f;
        }
        if (pen_decisive > 0 && st > pen_decisive - 0.32f && st < pen_decisive + 0.05f) return 0.28f;
        return 1.0f;
    }

    void sim_step() override { sim.step(); sim_done = sim.done; }

    void show_banner(const std::string& s, Color c, float t) { banners.push_back({s, c, t}); }

    void after_frame(AnimContext& ctx, float dt) override {
        if (teams.size() < 2) return;
        float t = (float)ctx.time;
        if (!whistle) { whistle = true; ctx.sound("whistle_long", 0.85f); ctx.sound("crowd_cheer_small", 0.6f); }
        for (const Ev& e : sim.ev) handle(ctx, e, t);
        sim.ev.clear();
        for (int i = 0; i < 2; ++i) {
            score_pop[i] = std::max(0.0f, score_pop[i] - dt * 2.2f);
            net_shake[i] = std::max(0.0f, net_shake[i] - dt * 2.0f);
        }
        // traînée du ballon
        trail.push_back(Vector2{sim.bx, sim.by});
        if (trail.size() > 9) trail.erase(trail.begin());
        // caméra : plan large ; léger zoom sur les ralentis ; gros plan sur le but pendant les tirs au but.
        // (fx, fy) = point du terrain visé, sy = hauteur à l'écran où il apparaît
        float tz = 1, fxw = CX, fyw = CY, sy = CY;
        if (sim.phase == PH_PENS || (sim.phase == PH_DONE && !sim.pens[0].empty())) {
            tz = 2.2f; fyw = PY0; sy = 640;
        } else if (time_scale < 0.8f) {
            tz = 1.14f; fxw = lerpf(CX, sim.bx, 0.6f); fyw = lerpf(CY, sim.by, 0.6f); sy = fyw;
        }
        float k = clamp01(dt * 5);
        cam_zoom = lerpf(cam_zoom, tz, k);
        cam_fx = lerpf(cam_fx, fxw, k);
        cam_fy = lerpf(cam_fy, fyw, k);
        cam_sy = lerpf(cam_sy, sy, k);
        zoom = cam_zoom;
        zoom_x = cam_fx;
        zoom_y = cam_sy;
        world_dy = cam_sy - cam_fy;
        if (win_t >= 0 && !celebrated && t >= win_t) {
            celebrated = true;
            const Team& w = teams[sim.winner];
            ctx.sound("fanfare", 0.7f);
            ctx.sound("confetti", 0.8f);
            ctx.sound("crowd_roar", 0.9f);
            fx.confetti(DW / 2, 940, 110, {w.c1, w.c2, WHITE, FB_GOLD}, 1500, PI_F * 0.45f);
        }
        if (win_t >= 0) {
            float age = t - win_t;
            if (age > 0.3f && age < 3.2f && (int)(age * 10) != (int)((age - dt) * 10)) {
                const Team& w = teams[sim.winner];
                fx.confetti(frng.r(100, 980), -20, 10, {w.c1, w.c2, WHITE, FB_GOLD}, 300, 0.5f, PI_F / 2);
            }
        }
    }

    void handle(AnimContext& ctx, const Ev& e, float t) {
        switch (e.type) {
        case E_KICKOFF:
            if (e.res == 1) ctx.sound("whistle_short", 0.6f);
            break;
        case E_PASS:
            sound(ctx, "kick", 3, e.res == 2 ? 0.55f : 0.32f);
            break;
        case E_SHOT:
            ctx.sound(frng.chance(0.5f) ? "kick_hard_1" : "kick_hard_2", 0.85f);
            ctx.sound("whoosh_1", 0.35f);
            fx.sparks(e.x, e.y, 10, WHITE, 700, 0.8f);
            break;
        case E_GOAL: {
            const Team& tm = teams[e.team];
            int net = e.team == 0 ? 0 : 1;            // 0 = but du haut
            net_shake[net] = 1;
            score_pop[e.team] = 1;
            goal_banner_t = t; goal_banner_team = e.team;
            float gy = net == 0 ? PY0 - GOAL_D / 2 : PY1 + GOAL_D / 2;
            fx.confetti(CX, gy, 70, {tm.c1, tm.c2, WHITE, FB_GOLD}, 1200, PI_F * 0.5f, net == 0 ? PI_F / 2 : -PI_F / 2);
            fx.shock(sim.bx, sim.by, 20, 420, 0.7f, tm.glow, 1.4f);
            ctx.sound("crowd_goal", 1.0f);
            ctx.sound("impact_bass", 0.8f);
            ctx.sound("airhorn", 0.45f);
            kick_shake(22);
            hitstop = 0.06f;
            if (!thumb_set) { thumb_set = true; ctx.metric("thumb_time", t + 0.35f); }
            break;
        }
        case E_SAVE:
            ctx.sound("hit_impact", 0.75f);
            ctx.sound("crowd_ooh", 0.8f);
            fx.text(e.res == 1 ? "WHAT A SAVE!" : "SAVED!", e.x, e.y + (e.team == 0 ? -90 : 90), rgba(120, 210, 255), 60, 1.1f);
            fx.shock(e.x, e.y, 10, 160, 0.4f, rgba(120, 210, 255));
            kick_shake(8);
            break;
        case E_CATCH:
            sound(ctx, "hit_soft", 2, 0.35f);
            break;
        case E_POST:
            ctx.sound("clang", 0.95f);
            ctx.sound("crowd_ooh", 0.85f);
            fx.text("OFF THE POST!", e.x, e.y + (e.y < CY ? 110 : -110), FB_GOLD, 58, 1.2f);
            fx.sparks(e.x, e.y, 18, WHITE, 900);
            kick_shake(10);
            break;
        case E_WALL:
            sound(ctx, "bounce_soft", 2, 0.12f + 0.15f * clamp01((e.v - 400) / 900));
            break;
        case E_HALF:
            ctx.sound("whistle_short", 0.7f);
            show_banner("HALF-TIME", WHITE, t);
            break;
        case E_FULL:
            ctx.sound("whistle_final", 0.9f);
            show_banner("FULL-TIME", WHITE, t);
            break;
        case E_PENS:
            ctx.sound("drumroll", 0.7f);
            ctx.sound("crowd_cheer", 0.6f);
            show_banner("PENALTIES!", FB_GOLD, t + 0.95f);
            break;
        case E_PEN_KICK:
            ctx.sound("whistle_short", 0.4f);
            break;
        case E_PEN_RES:
            if (e.res == PEN_GOAL) {
                ctx.sound("crowd_cheer", 0.75f); ctx.sound("pop_2", 0.4f);
                net_shake[0] = 0.8f;
                fx.confetti(e.x, PY0 - 20, 30, {teams[e.team].c1, teams[e.team].c2, WHITE}, 700, PI_F * 0.5f, PI_F / 2);
            } else if (e.res == PEN_SAVE) {
                ctx.sound("hit_impact", 0.8f); ctx.sound("crowd_ooh", 0.9f);
                fx.text("SAVED!", e.x, e.y + 120, rgba(120, 210, 255), 64, 1.0f);
            } else {
                ctx.sound("crowd_ooh", 0.9f);
                fx.text("MISSED!", CX, PY0 + 120, HP_RED, 64, 1.0f);
            }
            break;
        case E_END:
            win_t = t + (e.res == 1 ? 0.7f : 1.2f);
            ctx.sound("crowd_roar", 1.0f);
            if (e.res == 1) ctx.sound("whistle_final", 0.8f);
            break;
        }
    }

    // ------------------------------------------------------------------ dessin
    void draw_goal(int net, float t) {
        float y0 = net == 0 ? PY0 - GOAL_D : PY1;
        const Team& owner = teams[net == 0 ? 1 : 0];   // le but du haut est défendu par l'extérieur
        float sh = net_shake[net];
        float bulge = sh * 10 * std::sin(t * 40);
        Rectangle r{CX - GOAL_HW, y0 + (net == 0 ? -bulge : 0), GOAL_HW * 2, GOAL_D + std::fabs(bulge)};
        g.rect(r, withA(mixc(owner.c1, rgba(10, 12, 20), 0.6f), 0.85f));
        Color netc = withA(WHITE, 0.30f + 0.3f * sh);
        for (float x = r.x + 14; x < r.x + r.width; x += 16) g.rect(Rectangle{x, r.y, 1.6f, r.height}, netc);
        for (float y = r.y + 10; y < r.y + r.height; y += 12) g.rect(Rectangle{r.x, y, r.width, 1.6f}, netc);
        float ly = net == 0 ? PY0 : PY1;
        g.rect(Rectangle{CX - GOAL_HW - 4, net == 0 ? r.y - 4 : ly + GOAL_D, GOAL_HW * 2 + 8, 6}, withA(WHITE, 0.9f));
        g.rect(Rectangle{CX - GOAL_HW - 4, std::min(ly, r.y) - 2, 7, GOAL_D + 6}, withA(WHITE, 0.95f));
        g.rect(Rectangle{CX + GOAL_HW - 3, std::min(ly, r.y) - 2, 7, GOAL_D + 6}, withA(WHITE, 0.95f));
        g.disc(CX - GOAL_HW, ly, POST_R + 1, WHITE);
        g.disc(CX + GOAL_HW, ly, POST_R + 1, WHITE);
    }

    void draw_pitch(float t) {
        g.rrect(Rectangle{PX0 - 30, PY0 - 30 - GOAL_D, PW + 60, PH + 60 + 2 * GOAL_D}, 36, rgba(8, 12, 20));
        g.rrect_grad(Rectangle{PX0 - 18, PY0 - 18, PW + 36, PH + 36}, 22, rgba(34, 128, 66), rgba(20, 90, 46));
        int bands = 12;
        for (int i = 0; i < bands; i += 2)
            g.rect(Rectangle{PX0 - 18, PY0 - 18 + i * (PH + 36) / bands, PW + 36, (PH + 36) / bands}, withA(BLACK, 0.07f));
        // écussons en filigrane dans chaque moitié (qui défend quoi)
        for (int s = 0; s < 2; ++s) {
            const Team& tm = teams[s];
            if (!tm.has_logo) continue;
            float y = s == 0 ? CY + PH * 0.25f : CY - PH * 0.25f;
            float h = 300, w = h * tm.logo_aspect;
            g.tex_center(tm.logo, CX, y, w, h, withA(WHITE, 0.07f));
        }
        Color line = withA(WHITE, 0.6f);
        float lt = 2.4f;
        g.rrect_line(Rectangle{PX0, PY0, PW, PH}, 3, lt, line);
        g.rect(Rectangle{PX0, CY - 2, PW, 4}, line);
        g.ring_(CX, CY, 120, lt, line);
        g.disc(CX, CY, 7, line);
        for (int s = 0; s < 2; ++s) {
            float ly = s == 0 ? PY0 : PY1, sg = s == 0 ? 1.0f : -1.0f;
            float bh = 200, gh = 72;
            g.rrect_line(Rectangle{CX - 260, s == 0 ? ly : ly - bh, 520, bh}, 2, lt, line);
            g.rrect_line(Rectangle{CX - 150, s == 0 ? ly : ly - gh, 300, gh}, 2, lt, line);
            g.disc(CX, ly + sg * PEN_SPOT, 6, line);
            g.arc(CX, ly + sg * PEN_SPOT, 92, lt, s == 0 ? PI_F / 2 : -PI_F / 2, 0.62f, line);
        }
        draw_goal(0, t);
        draw_goal(1, t);
    }

    void draw_world(AnimContext& ctx) override {
        if (teams.size() < 2) return;
        float t = (float)ctx.time;
        draw_pitch(t);
        bool pens = sim.phase == PH_PENS || (sim.phase == PH_DONE && !sim.pens[0].empty());
        int shooter = pens && sim.pen_k >= 0 ? (sim.pen_first + sim.pen_k) % 2 : -1;
        // joueurs (pendant les tirs au but : seulement le tireur et le gardien)
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < NP; ++i) {
                if (pens && !((s == shooter && i == 2) || (s == 1 - shooter && i == 0))) continue;
                const Pl& p = sim.pl[s][i];
                float r = i == 0 ? GK_R : PR;
                g.disc(p.x + 5, p.y + 9, r * 1.02f, withA(BLACK, 0.28f));
                if (i == 0) g.ring_(p.x, p.y, r + 7, 4.5f, withA(rgba(210, 255, 90), 0.95f));   // gardien
                TokenFx f; f.shadow = 0;
                draw_token(g, teams[s], p.x, p.y, r, f);
            }
        // ballon : traînée, ombre, ballon qui tourne
        float sp = len2(sim.bvx, sim.bvy);
        for (size_t k = 0; k + 1 < trail.size(); ++k) {
            float a = (float)(k + 1) / trail.size();
            g.disc(trail[k].x, trail[k].y, BR * (0.4f + 0.5f * a), withA(WHITE, 0.22f * a * clamp01((sp - 300) / 700)));
        }
        g.disc(sim.bx + 4, sim.by + 7, BR, withA(BLACK, 0.35f));
        if (const Texture2D* ic = icon("ball")) g.tex_center(*ic, sim.bx, sim.by, BR * 2.3f, BR * 2.3f, WHITE, sim.bspin * 57.3f);
        else g.disc(sim.bx, sim.by, BR, WHITE);
    }

    void draw_world_glow(AnimContext&) override {
        if (teams.size() < 2) return;
        float sp = len2(sim.bvx, sim.bvy);
        g.glow_at(sim.bx, sim.by, 60, withA(WHITE, 0.25f + 0.35f * clamp01((sp - 600) / 900)));
        for (int n = 0; n < 2; ++n)
            if (net_shake[n] > 0) g.glow_at(CX, n == 0 ? PY0 - GOAL_D / 2 : PY1 + GOAL_D / 2, 260, withA(WHITE, 0.6f * net_shake[n]));
    }

    void draw_scoreboard(float t) {
        float y = 255, h = 170, w = 960;
        Rectangle r{DW / 2 - w / 2, y - h / 2, w, h};
        g.rrect(Rectangle{r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 40, withA(WHITE, 0.16f));
        g.rrect(r, 38, withA(rgba(8, 10, 20), 0.93f));
        for (int s = 0; s < 2; ++s) {
            const Team& tm = teams[s];
            float side = s == 0 ? -1.0f : 1.0f;
            g.rrect(Rectangle{s == 0 ? r.x + 18 : r.x + r.width - 26, r.y + 26, 8, h - 52}, 4, tm.glow);
            TokenFx f; f.shadow = 0;
            draw_token(g, tm, DW / 2 + side * 385, y - 8, 54, f);
            TextStyle n; n.kind = F_BOLD; n.size = 34; n.shadow = 0; n.spacing = 1;
            draw_text(fonts, upper(tm.short_name.empty() ? tm.name : tm.short_name), DW / 2 + side * 385, y + 62, n, 200, 0.5f);
            TextStyle nm; nm.size = 50; nm.shadow = 0;
            draw_text(fonts, upper(tm.name), DW / 2 + side * 205, y - 10, nm, 230, 0.5f);
        }
        // score
        for (int s = 0; s < 2; ++s) {
            float pop = score_pop[s];
            TextStyle sc; sc.size = 118 * (1 + 0.45f * ease_out_back(pop)); sc.shadow = 0;
            sc.color = mixc(WHITE, teams[s].glow, pop);
            char buf[8]; snprintf(buf, sizeof(buf), "%d", sim.score[s]);
            draw_text(fonts, buf, DW / 2 + (s == 0 ? -52 : 52), y - 14, sc, 0, 0.5f);
        }
        TextStyle dash; dash.size = 80; dash.shadow = 0; dash.color = withA(WHITE, 0.6f);
        draw_text(fonts, "-", DW / 2, y - 16, dash, 0, 0.5f);
        // chrono
        bool pens = sim.phase == PH_PENS || (sim.phase == PH_DONE && !sim.pens[0].empty());
        std::string clock;
        if (pens) clock = "PENALTIES";
        else if (sim.phase == PH_DONE || sim.t >= MATCH_T) clock = "FULL-TIME";
        else { char b[16]; snprintf(b, sizeof(b), "%d'", std::max(1, (int)std::ceil(sim.minute()))); clock = b; }
        TextStyle c; c.kind = F_BOLD; c.size = 32; c.shadow = 0; c.spacing = 2; c.color = rgba(12, 14, 24);
        float cw = text_width(fonts, clock, c) + 56;
        g.rrect(Rectangle{DW / 2 - cw / 2, y + 44, cw, 48}, 24, pens ? FB_GOLD : rgba(120, 255, 150));
        draw_text(fonts, clock, DW / 2, y + 69, c, 0, 0.5f);
        // barre de progression du match
        float prog = clamp01(sim.t / MATCH_T);
        g.rrect(Rectangle{r.x + 40, r.y + h - 10, (r.width - 80), 5}, 2.5f, withA(WHITE, 0.12f));
        g.rrect(Rectangle{r.x + 40, r.y + h - 10, (r.width - 80) * prog, 5}, 2.5f, withA(rgba(120, 255, 150), 0.9f));
        // tirs au but : pastilles
        if (pens) {
            for (int s = 0; s < 2; ++s) {
                int n = std::max(5, (int)std::max(sim.pens[0].size(), sim.pens[1].size()));
                float yy = y + h / 2 + 46 + s * 50;
                TokenFx f; f.shadow = 0;
                draw_token(g, teams[s], DW / 2 - 250, yy, 20, f);
                for (int k = 0; k < n; ++k) {
                    float x = DW / 2 - 190 + k * 58;
                    if (k < (int)sim.pens[s].size()) {
                        bool ok = sim.pens[s][k] == PEN_GOAL;
                        g.disc(x, yy, 19, ok ? HP_GREEN : HP_RED);
                        TextStyle m; m.kind = F_BOLD; m.size = 24; m.shadow = 0;
                        draw_text(fonts, ok ? "O" : "X", x, yy + 1, m, 0, 0.5f);
                    } else g.ring_(x, yy, 18, 2.5f, withA(WHITE, 0.45f));
                }
            }
        }
    }

    void draw_hud(AnimContext& ctx) override {
        if (teams.size() < 2) return;
        float t = (float)ctx.time;
        std::string chip_txt = cfg.str("chip");
        if (!chip_txt.empty()) chip(upper(chip_txt), DW / 2, 128, withA(accent, 0.95f), WHITE, 1, 28);
        draw_scoreboard(t);
        // GOAL !
        float ga = t - goal_banner_t;
        if (ga >= 0 && ga < 1.5f && goal_banner_team >= 0) {
            const Team& tm = teams[goal_banner_team];
            float k = ease_out_back(clamp01(ga / 0.3f), 2.0f);
            float out = 1 - smooth01((ga - 1.2f) / 0.3f);
            float cy = CY;
            g.rect(Rectangle{0, cy - 150 * k, DW, 300 * k}, withA(mixc(tm.c1, rgba(6, 8, 16), 0.35f), 0.82f * out));
            TextStyle gs; gs.size = 210 * (0.7f + 0.3f * k); gs.outline = 8; gs.color = WHITE; gs.spacing = 4;
            draw_text(fonts, "GOAL!", DW / 2 + 70, cy - 6, gs, 760, 0.5f, out);
            TokenFx f; f.scale = k; f.alpha = out;
            draw_token(g, tm, DW / 2 - 300, cy, 92, f);
            TextStyle ns; ns.kind = F_BOLD; ns.size = 40; ns.shadow = 0.6f; ns.color = withA(WHITE, 0.95f); ns.spacing = 3;
            char b[64]; snprintf(b, sizeof(b), "%s  %d - %d  %s", upper(teams[0].short_name).c_str(), sim.score[0], sim.score[1],
                                 upper(teams[1].short_name).c_str());
            draw_text(fonts, b, DW / 2, cy + 118, ns, 900, 0.5f, out * clamp01((ga - 0.2f) / 0.2f));
        }
        // bandeaux (mi-temps, fin, tirs au but) : le plus récent déjà commencé
        const Banner* bn = nullptr;
        for (const auto& x : banners) if (t >= x.t0) bn = &x;
        float ba = bn ? t - bn->t0 : 99;
        if (bn && ba < 1.3f && win_t < 0) {
            float k = ease_out_back(clamp01(ba / 0.25f), 1.6f);
            float out = 1 - smooth01((ba - 1.0f) / 0.3f);
            TextStyle bs; bs.size = 120 * (0.8f + 0.2f * k); bs.outline = 6; bs.color = bn->c; bs.spacing = 3;
            g.rect(Rectangle{0, CY - 95, DW, 190}, withA(rgba(6, 8, 16), 0.7f * out * clamp01(k)));
            draw_text(fonts, bn->text, DW / 2, CY, bs, 960, 0.5f, out * clamp01(k));
        }
        if (win_t >= 0 && t >= win_t) {
            char line[64];
            int w = sim.winner, l = 1 - w;
            if (!sim.pens[0].empty())
                snprintf(line, sizeof(line), "wins on penalties %d-%d", sim.pens_goals(w), sim.pens_goals(l));
            else snprintf(line, sizeof(line), "wins %d-%d", sim.score[w], sim.score[l]);
            winner_card(t - win_t, w, line);
        }
    }
};

}  // namespace

Scene* make_bigmatch_scene() { return new BigMatchScene(); }
