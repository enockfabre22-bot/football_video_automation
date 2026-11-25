// ============================================================================
//  quiz_scene.cpp — « Guess the Football Player » : vidéo longue 16:9 (1920 x 1080).
//  Chaque manche montre la nationalité, le poste et la carrière (écussons qui apparaissent
//  un par un), avec la silhouette du joueur ; compte à rebours, puis révélation (photo libre
//  détourée + nom). Niveaux EASY -> IMPOSSIBLE, intro et fin « comment your score ».
//  Tout le minutage est calculé par quiz_prepare.py (quiz.txt) : la scène le suit à la lettre.
// ============================================================================
#include "fb_common.h"

namespace {

struct Club { int team; std::string years; float t_show; };
struct Round {
    int idx = 0, level = 0;
    float t0 = 0, t_nation = 0, t_pos = 0, t_count = 0, t_reveal = 0, t1 = 0;
    std::string name, nation, flag, position, photo, stat, birth;
    std::vector<Club> clubs;
    Texture2D tex{}, mask{}, flag_tex{};
};
struct Level { int idx; float t0, t1; std::string label; Color col; int first, last; };

static std::vector<std::string> fields(const std::string& line) { return split(line, '|'); }

class QuizScene : public FootballScene {
    std::vector<Round> rounds;
    std::vector<Level> levels;
    float intro_t1 = 6, outro_t0 = 0, total = 0;
    float prev_t = -1;
    int total_rounds = 0;

    static float f(const std::string& s) { return s.empty() ? 0.0f : std::stof(s); }

public:
    void setup(AnimContext& ctx) override {
        start_time = 1e9f;   // pas de simulation physique : tout suit le minutage de quiz.txt
        std::ifstream in(dir + "/quiz.txt");
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            auto v = fields(line);
            if (v.empty()) continue;
            if (v[0] == "T" && v.size() >= 4) {
                if (v[1] == "intro") intro_t1 = f(v[3]);
                if (v[1] == "outro") { outro_t0 = f(v[2]); total = f(v[3]); }
            } else if (v[0] == "L" && v.size() >= 8) {
                levels.push_back({std::stoi(v[1]), f(v[2]), f(v[3]), v[4], parse_color(v[5]), std::stoi(v[6]), std::stoi(v[7])});
            } else if (v[0] == "R" && v.size() >= 16) {
                Round r;
                r.idx = std::stoi(v[1]); r.level = std::stoi(v[2]);
                r.t0 = f(v[3]); r.t_nation = f(v[4]); r.t_pos = f(v[5]); r.t_count = f(v[6]); r.t_reveal = f(v[7]); r.t1 = f(v[8]);
                r.name = v[9]; r.nation = v[10]; r.flag = v[11]; r.position = v[12]; r.photo = v[13]; r.stat = v[14]; r.birth = v[15];
                rounds.push_back(r);
            } else if (v[0] == "C" && v.size() >= 5 && !rounds.empty()) {
                rounds.back().clubs.push_back({std::stoi(v[2]), v[3], f(v[4])});
            }
        }
        total_rounds = (int)rounds.size();
        for (auto& r : rounds) {
            r.tex = load_tex(dir + "/" + r.photo);
            std::string mp = r.photo.substr(0, r.photo.size() - 4) + "_mask.png";
            r.mask = load_tex(dir + "/" + mp);
            r.flag_tex = load_tex(dir + "/" + r.flag);
        }
        printf("[QUIZ] %d manches, %zu niveaux, durée %.1f s\n", total_rounds, levels.size(), total);
        ctx.metric("rounds", total_rounds);
        ctx.metric("thumb_time", rounds.empty() ? 3.0f : rounds[std::min(2, total_rounds - 1)].t_reveal + 1.2f);
    }

    void sim_step() override {}

    const Round* current(float t) const {
        for (const auto& r : rounds) if (t >= r.t0 && t < r.t1) return &r;
        return nullptr;
    }
    const Level* level_card(float t) const {
        for (const auto& l : levels) if (t >= l.t0 && t < l.t1) return &l;
        return nullptr;
    }
    const Level* level_of(int lv) const {
        for (const auto& l : levels) if (l.idx == lv) return &l;
        return levels.empty() ? nullptr : &levels[0];
    }
    bool crossed(float te, float t) const { return prev_t < te && t >= te; }

    // ------------------------------------------------------------------ sons / particules
    void after_frame(AnimContext& ctx, float) override {
        float t = (float)ctx.time;
        if (prev_t < 0) {
            ctx.sound("whoosh_long", 0.7f);
            ctx.sound("crowd_cheer_small", 0.45f);
        }
        if (crossed(0.55f, t)) { ctx.sound("impact_bass", 0.6f); kick_shake(8); }
        for (const auto& l : levels) {
            if (crossed(l.t0, t)) { ctx.sound("whoosh_heavy", 0.6f); }
            if (crossed(l.t0 + 0.35f, t)) {
                ctx.sound(l.idx >= 3 ? "airhorn_hype" : "impact_bass", l.idx >= 3 ? 0.5f : 0.6f);
                fx.confetti(DW / 2, DH / 2 + 40, 60, {l.col, WHITE, FB_GOLD}, 1300, PI_F);
                kick_shake(10);
            }
        }
        for (const auto& r : rounds) {
            if (crossed(r.t0, t)) sound(ctx, "whoosh", 2, 0.5f);
            if (crossed(r.t_nation, t)) sound(ctx, "pop", 4, 0.35f);
            if (crossed(r.t_pos, t)) sound(ctx, "pop", 4, 0.3f);
            for (size_t k = 0; k < r.clubs.size(); ++k)
                if (crossed(r.clubs[k].t_show, t)) { sound(ctx, "pop", 4, 0.45f); ctx.sound("kick_1", 0.25f); }
            if (crossed(r.t_count, t)) ctx.sound("clock_tick", 0.55f);
            for (int s = 1; s <= 3; ++s)
                if (crossed(r.t_reveal - s, t)) ctx.sound("tick_2", 0.6f);
            if (crossed(r.t_reveal, t)) {
                ctx.sound("correct", 0.75f);
                ctx.sound(r.level >= 3 ? "crowd_cheer" : "crowd_cheer_small", 0.4f);
                const Level* l = level_of(r.level);
                fx.confetti(card().x + card().width / 2, card().y + 60, 45, {l ? l->col : FB_GOLD, WHITE, FB_GOLD}, 900, PI_F * 0.6f, PI_F / 2);
                fx.shock(card().x + card().width / 2, card().y + card().height * 0.45f, 60, 420, 0.6f, WHITE, 1.0f);
            }
        }
        if (crossed(outro_t0, t)) { ctx.sound("fanfare", 0.6f); ctx.sound("crowd_cheer", 0.6f); }
        if (crossed(outro_t0 + 0.4f, t)) fx.confetti(DW / 2, -20, 120, {FB_GOLD, WHITE, accent}, 600, 0.6f, PI_F / 2);
        prev_t = t;
        if (total > 0 && t >= total) ctx.finish();
    }

    // ------------------------------------------------------------------ mise en page
    static Rectangle panel() { return Rectangle{60, 150, 1100, 870}; }
    static Rectangle card() { return Rectangle{1200, 150, 660, 870}; }

    void glass(Rectangle r, float radius, float a, Color tint) {
        g.rrect(Rectangle{r.x + 6, r.y + 10, r.width, r.height}, radius, withA(BLACK, 0.35f * a));
        g.rrect_grad(r, radius, withA(mixc(rgba(22, 28, 48), tint, 0.10f), 0.92f * a), withA(rgba(10, 13, 24), 0.92f * a));
        g.rrect_line(r, radius, 1.5f, withA(WHITE, 0.14f * a));
    }

    void top_bar(float t, const Round* r) {
        TextStyle brand; brand.size = 46; brand.spacing = 2; brand.shadow = 0.5f;
        draw_text(fonts, "GUESS THE PLAYER", 60, 70, brand, 0, 0.0f);
        float bw = text_width(fonts, "GUESS THE PLAYER", brand);
        g.rrect(Rectangle{60 + bw + 16, 56, 12, 28}, 4, FB_GOLD);
        if (!r) return;
        char buf[48];
        snprintf(buf, sizeof(buf), "PLAYER %d / %d", r->idx, total_rounds);
        chip(buf, DW / 2, 70, withA(WHITE, 0.95f), rgba(12, 14, 24), 1, 32);
        const Level* l = level_of(r->level);
        if (l) {
            TextStyle ls; ls.size = 40; ls.spacing = 3; ls.shadow = 0;
            float w = text_width(fonts, upper(l->label), ls) + 60;
            Rectangle rr{DW - 60 - w, 44, w, 56};
            g.rrect(rr, 28, l->col);
            ls.color = rgba(12, 14, 24);
            draw_text(fonts, upper(l->label), rr.x + w / 2, rr.y + 29, ls);
        }
        // progression : un segment par joueur, couleur du niveau
        float x0 = 60, x1 = DW - 60, y = 122, gap = 6;
        float sw = (x1 - x0 - gap * (total_rounds - 1)) / std::max(1, total_rounds);
        for (const auto& q : rounds) {
            const Level* ql = level_of(q.level);
            Color c = ql ? ql->col : WHITE;
            float x = x0 + (q.idx - 1) * (sw + gap);
            bool done = t >= q.t_reveal, cur = &q == r;
            g.rrect(Rectangle{x, y - 5, sw, 10}, 5, withA(c, done ? 0.95f : cur ? 0.55f + 0.3f * std::sin(t * 6) : 0.16f));
        }
        (void)t;
    }

    void draw_clues(const Round& r, float t) {
        Rectangle p = panel();
        float in = ease_out_cubic(clamp01((t - r.t0) / 0.45f));
        float out = 1 - smooth01((t - (r.t1 - 0.35f)) / 0.35f);
        float a = in * out;
        rlPushMatrix();
        rlTranslatef((1 - in) * -60, 0, 0);
        glass(p, 34, a, accent);
        // nationalité + poste
        float ka = ease_out_back(clamp01((t - r.t_nation) / 0.35f)) * out;
        if (ka > 0.01f) {
            float fx0 = p.x + 50, fy = p.y + 80;
            if (r.flag_tex.id) {
                float fw = 120, fh = fw * r.flag_tex.height / std::max(1, r.flag_tex.width);
                g.rrect(Rectangle{fx0 - 3, fy - fh / 2 - 3, fw + 6, fh + 6}, 8, withA(WHITE, 0.85f * ka));
                g.tex(r.flag_tex, Rectangle{fx0, fy - fh / 2, fw, fh}, withA(WHITE, ka));
            }
            TextStyle ns; ns.size = 64; ns.spacing = 2;
            draw_text(fonts, upper(r.nation), fx0 + 150, fy, ns, 520, 0.0f, ka);
        }
        float kp = ease_out_back(clamp01((t - r.t_pos) / 0.35f)) * out;
        if (kp > 0.01f && !r.position.empty()) {
            TextStyle ps; ps.kind = F_BOLD; ps.size = 30; ps.spacing = 2; ps.shadow = 0;
            std::string pos = upper(r.position);
            float w = text_width(fonts, pos, ps) + 56;
            Rectangle pr{p.x + p.width - 50 - w, p.y + 80 - 30, w, 60};
            g.rrect(pr, 30, withA(accent, 0.95f * kp));
            ps.color = WHITE;
            draw_text(fonts, pos, pr.x + w / 2, pr.y + 31, ps, 0, 0.5f, kp);
        }
        // carrière
        TextStyle cl; cl.kind = F_BOLD; cl.size = 26; cl.spacing = 5; cl.shadow = 0; cl.color = withA(WHITE, 0.6f);
        draw_text(fonts, "CAREER PATH", p.x + 50, p.y + 170, cl, 0, 0.0f, a);
        g.rect(Rectangle{p.x + 50, p.y + 196, p.width - 100, 2}, withA(WHITE, 0.10f * a));
        int n = (int)r.clubs.size();
        int cols = n <= 4 ? n : n <= 10 ? (n + 1) / 2 : (n + 2) / 3;   // 2 lignes jusqu'à 10 clubs
        cols = std::max(1, std::min(cols, 5));
        int rows = (n + cols - 1) / cols;
        float rad = rows <= 2 ? 70.0f : 54.0f;
        float area_y0 = p.y + 230, area_y1 = r.stat.empty() ? p.y + p.height - 40 : p.y + p.height - 120;
        float row_h = (area_y1 - area_y0) / std::max(1, rows);
        float col_w = (p.width - 100) / cols;
        for (int k = 0; k < n; ++k) {
            const Club& c = r.clubs[k];
            float age = t - c.t_show;
            if (age < 0) continue;
            int row = k / cols, col = k % cols;
            int in_row = std::min(cols, n - row * cols);
            float row_x0 = p.x + 50 + (cols - in_row) * col_w / 2;
            float cx = row_x0 + col_w * (col + 0.5f), cy = area_y0 + row_h * (row + 0.42f);
            float s = ease_out_back(clamp01(age / 0.32f), 2.2f);
            // flèche depuis le club précédent (même ligne)
            if (col > 0) {
                float ax = cx - col_w / 2, ay = cy;
                float k2 = clamp01(age / 0.25f);
                DrawTriangle(Vector2{ax - 10, ay - 14}, Vector2{ax - 10, ay + 14}, Vector2{ax + 12, ay}, withA(WHITE, 0.55f * k2 * out));
            }
            if (c.team >= 0 && c.team < (int)teams.size()) {
                const Team& tm = teams[c.team];
                TokenFx fx_; fx_.scale = s; fx_.alpha = out;
                draw_token(g, tm, cx, cy, rad, fx_);
                if (!tm.has_logo) {
                    TextStyle in_; in_.size = rad * 0.62f * s; in_.color = rgba(20, 24, 36); in_.shadow = 0;
                    draw_text(fonts, tm.short_name, cx, cy, in_, rad * 1.4f, 0.5f, out);
                }
                // numéro d'ordre
                g.disc(cx - rad * 0.78f, cy - rad * 0.78f, 17 * s, withA(FB_GOLD, out));
                char nb[8];
                snprintf(nb, sizeof(nb), "%d", k + 1);
                TextStyle nbs; nbs.kind = F_BOLD; nbs.size = 22 * s; nbs.color = rgba(12, 14, 24); nbs.shadow = 0;
                draw_text(fonts, nb, cx - rad * 0.78f, cy - rad * 0.78f + 1, nbs, 0, 0.5f, out);
                TextStyle ys; ys.kind = F_BOLD; ys.size = rows <= 2 ? 30 : 25; ys.shadow = 0.6f;
                draw_text(fonts, c.years, cx, cy + rad + 30, ys, col_w - 10, 0.5f, clamp01(age / 0.3f) * out);
                bool reveal = t >= r.t_reveal;
                if (reveal) {
                    TextStyle cn; cn.kind = F_SEMI; cn.size = 22; cn.shadow = 0; cn.color = withA(WHITE, 0.75f);
                    draw_text(fonts, tm.name, cx, cy + rad + 62, cn, col_w - 10, 0.5f, clamp01((t - r.t_reveal) / 0.4f) * out);
                }
            }
        }
        // ligne de statistiques à la révélation
        if (!r.stat.empty() && t >= r.t_reveal) {
            float k = ease_out_back(clamp01((t - r.t_reveal - 0.25f) / 0.4f)) * out;
            TextStyle ss; ss.size = 44; ss.color = FB_GOLD; ss.spacing = 2; ss.outline = 2;
            draw_text(fonts, r.stat, p.x + p.width / 2, p.y + p.height - 62, ss, p.width - 100, 0.5f, k);
        }
        rlPopMatrix();
    }

    // position de la photo dans la carte
    static Rectangle photo_rect(const Texture2D& tex) {
        Rectangle c = card();
        float maxw = c.width - 60, maxh = c.height - 190;
        float sc = std::min(maxw / tex.width, maxh / tex.height);
        float w = tex.width * sc, h = tex.height * sc;
        return Rectangle{c.x + (c.width - w) / 2, c.y + c.height - 150 - h, w, h};
    }
    float card_in(const Round& r, float t) const { return ease_out_cubic(clamp01((t - r.t0) / 0.45f)); }
    float card_out(const Round& r, float t) const { return 1 - smooth01((t - (r.t1 - 0.35f)) / 0.35f); }

    // fond de carte + photo en couleur (dessinés SOUS le halo lumineux)
    void draw_card_back(const Round& r, float t) {
        Rectangle c = card();
        float in = card_in(r, t), out = card_out(r, t), a = in * out;
        float rk = t >= r.t_reveal ? clamp01((t - r.t_reveal) / 0.35f) : 0.0f;
        rlPushMatrix();
        rlTranslatef((1 - in) * 60, 0, 0);
        const Level* l = level_of(r.level);
        Color lc = l ? l->col : accent;
        g.rrect(Rectangle{c.x + 6, c.y + 10, c.width, c.height}, 40, withA(BLACK, 0.35f * a));
        g.rrect_grad(c, 40, withA(mixc(rgba(30, 38, 70), lc, 0.18f + 0.25f * rk), a), withA(rgba(8, 10, 20), a));
        if (r.tex.id && rk > 0) {
            Rectangle d = photo_rect(r.tex);
            float s = 1.0f + 0.02f * ease_out_back(rk);
            g.tex(r.tex, Rectangle{d.x - d.width * (s - 1) / 2, d.y - d.height * (s - 1), d.width * s, d.height * s},
                  withA(WHITE, a * rk));
        }
        rlPopMatrix();
    }

    // silhouette noire, bas de carte, « ? », compte à rebours / nom (dessinés AU-DESSUS du halo)
    void draw_card_front(const Round& r, float t) {
        Rectangle c = card();
        float in = card_in(r, t), out = card_out(r, t), a = in * out;
        bool revealed = t >= r.t_reveal;
        float rk = revealed ? clamp01((t - r.t_reveal) / 0.35f) : 0.0f;
        rlPushMatrix();
        rlTranslatef((1 - in) * 60, 0, 0);
        const Level* l = level_of(r.level);
        Color lc = l ? l->col : accent;
        if (r.mask.id && rk < 1) g.tex(r.mask, photo_rect(r.mask), withA(rgba(5, 6, 12), a * (1 - rk)));
        // base de la photo fondue dans la carte
        g.vgrad_rect(Rectangle{c.x, c.y + c.height - 260, c.width, 120}, withA(rgba(8, 10, 20), 0.95f * a));
        g.rrect(Rectangle{c.x, c.y + c.height - 150, c.width, 150}, 40, withA(rgba(8, 10, 20), a));
        g.rrect_line(c, 40, 2.5f, withA(mixc(WHITE, lc, 0.5f), (0.5f + 0.5f * rk) * a));
        if (!revealed && r.tex.id) {   // gros point d'interrogation qui respire
            float pulse = 1 + 0.06f * std::sin(t * 5);
            TextStyle q; q.size = 260 * pulse; q.color = withA(WHITE, 0.95f); q.outline = 6; q.outline_color = lc;
            draw_text(fonts, "?", c.x + c.width / 2, c.y + (c.height - 150) * 0.5f, q, 0, 0.5f, a);
        }
        if (!revealed) {
            float cnt = t - r.t_count;
            float dur = r.t_reveal - r.t_count;
            float cx = c.x + c.width / 2, cy = c.y + c.height - 78;
            if (cnt >= 0) {
                float left = std::max(0.0f, dur - cnt);
                Color tc = left <= 3 ? mixc(HP_RED, WHITE, 0.25f * std::sin(t * 12)) : FB_GOLD;
                float pop = ease_out_back(clamp01(cnt / 0.3f));
                g.disc(cx, cy, 56 * pop, withA(rgba(8, 10, 20), a));
                g.ring_(cx, cy, 56 * pop, 6, withA(WHITE, 0.15f * a));
                g.gauge(cx, cy, 56 * pop, 6, left / dur, withA(tc, a));
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", (int)std::ceil(left));
                TextStyle ns; ns.size = 64 * pop; ns.color = tc; ns.shadow = 0;
                draw_text(fonts, buf, cx, cy + 2, ns, 0, 0.5f, a);
            } else {
                TextStyle wq; wq.kind = F_BOLD; wq.size = 34; wq.spacing = 3; wq.shadow = 0; wq.color = withA(WHITE, 0.7f);
                draw_text(fonts, "WHO AM I?", cx, cy, wq, 0, 0.5f, a);
            }
        } else {
            float k = ease_out_back(clamp01((t - r.t_reveal) / 0.4f), 1.6f);
            TextStyle nm; nm.size = 74; nm.spacing = 2; nm.outline = 3;
            draw_text(fonts, upper(r.name), c.x + c.width / 2, c.y + c.height - 82, nm, c.width - 50, 0.5f, clamp01(k) * out);
            g.rrect(Rectangle{c.x + c.width / 2 - 90 * k, c.y + c.height - 34, 180 * k, 6}, 3, withA(FB_GOLD, out));
            if (rk < 1) g.rrect(c, 40, withA(WHITE, 0.55f * (1 - rk) * a));   // éclair de révélation
        }
        rlPopMatrix();
    }

    void draw_level_card(const Level& l, float t) {
        float age = t - l.t0, left = l.t1 - t;
        float a = smooth01(age / 0.25f) * smooth01(left / 0.3f);
        g.rect(Rectangle{0, 0, DW, DH}, withA(rgba(5, 7, 14), 0.88f * a));
        for (int i = 0; i < 18; ++i) {   // rayons
            float ang = i * 2 * PI_F / 18 + age * 0.4f;
            Vector2 c0 = {DW / 2, DH / 2};
            DrawTriangle(c0, Vector2{c0.x + std::cos(ang + 0.07f) * 1500, c0.y + std::sin(ang + 0.07f) * 1500},
                         Vector2{c0.x + std::cos(ang - 0.07f) * 1500, c0.y + std::sin(ang - 0.07f) * 1500}, withA(l.col, 0.09f * a));
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "LEVEL %d", l.idx + 1);
        TextStyle s1; s1.kind = F_BOLD; s1.size = 44; s1.spacing = 10; s1.color = withA(WHITE, 0.8f); s1.shadow = 0;
        draw_text(fonts, buf, DW / 2, DH / 2 - 150, s1, 0, 0.5f, a);
        float k = ease_out_back(clamp01((age - 0.2f) / 0.45f), 1.8f);
        TextStyle s2; s2.size = 240 * k; s2.color = l.col; s2.outline = 8; s2.spacing = 6;
        draw_text(fonts, upper(l.label), DW / 2, DH / 2, s2, 1700, 0.5f, a);
        snprintf(buf, sizeof(buf), "PLAYERS %d - %d", l.first, l.last);
        TextStyle s3; s3.kind = F_BOLD; s3.size = 40; s3.spacing = 4; s3.shadow = 0;
        draw_text(fonts, buf, DW / 2, DH / 2 + 160, s3, 0, 0.5f, a * clamp01((age - 0.4f) / 0.3f));
    }

    Rectangle intro_rect(const Round& r, int shown, float t) const {
        float k = ease_out_cubic(clamp01((t - 0.3f - shown * 0.12f) / 0.6f));
        float h = 520, w = h * r.tex.width / std::max(1, r.tex.height);
        return Rectangle{DW / 2 + (shown - 2) * 330 - w / 2, DH - h + (1 - k) * 200, w, h};
    }

    void draw_intro(float t) {
        float out = 1 - smooth01((t - (intro_t1 - 0.4f)) / 0.4f);
        // silhouettes des joueurs à venir (niveaux difficiles : rien n'est dévoilé)
        int shown = 0;
        for (int i = total_rounds - 1; i >= 0 && shown < 5; --i) {
            const Round& r = rounds[i];
            if (!r.tex.id) continue;
            float d = (float)shown;
            float k = ease_out_cubic(clamp01((t - 0.3f - d * 0.12f) / 0.6f));
            Rectangle d2 = intro_rect(r, shown, t);
            float x = d2.x, y = d2.y, w = d2.width, h = d2.height;
            g.tex(r.mask.id ? r.mask : r.tex, d2, withA(rgba(5, 6, 12), out * k));
            TextStyle q; q.size = 140; q.color = withA(WHITE, 0.9f); q.outline = 5; q.outline_color = accent;
            draw_text(fonts, "?", x + w / 2, y + h * 0.35f, q, 0, 0.5f, out * k);
            shown++;
        }
        g.vgrad_rect(Rectangle{0, DH - 260, DW, 260}, withA(rgba(5, 7, 14), 0.9f * out));
        float k1 = ease_out_back(clamp01((t - 0.05f) / 0.5f), 1.6f);
        TextStyle t1; t1.size = 170 * k1; t1.outline = 7; t1.spacing = 3;
        draw_text(fonts, upper(cfg.str("title1", "Guess the player")), DW / 2, 210, t1, 1750, 0.5f, out);
        float k2 = ease_out_back(clamp01((t - 0.35f) / 0.45f));
        TextStyle t2; t2.size = 84; t2.color = FB_GOLD; t2.outline = 5; t2.spacing = 3;
        draw_text(fonts, upper(cfg.str("title2", "from their career path")), DW / 2, 360 + (1 - k2) * 30, t2, 1700, 0.5f, out * clamp01(k2));
        char buf[64];
        snprintf(buf, sizeof(buf), "%d PLAYERS  ·  EASY  >  IMPOSSIBLE", total_rounds);
        float k3 = clamp01((t - 0.8f) / 0.3f);
        chip(buf, DW / 2, 470, withA(WHITE, 0.95f), rgba(12, 14, 24), out * k3, 34);
    }

    void draw_outro(float t) {
        float age = t - outro_t0;
        float a = smooth01(age / 0.4f);
        g.rect(Rectangle{0, 0, DW, DH}, withA(rgba(5, 7, 14), 0.75f * a));
        float k = ease_out_back(clamp01(age / 0.5f), 1.6f);
        TextStyle h; h.size = 150 * k; h.outline = 6; h.spacing = 3;
        draw_text(fonts, "HOW MANY DID YOU GET?", DW / 2, 170, h, 1750, 0.5f, a);
        struct Tier { const char* range; const char* label; Color c; };
        int n = total_rounds;
        char r1[16], r2[16], r3[16], r4[16], r5[16];
        snprintf(r1, sizeof(r1), "0 - %d", n / 4);
        snprintf(r2, sizeof(r2), "%d - %d", n / 4 + 1, n / 2);
        snprintf(r3, sizeof(r3), "%d - %d", n / 2 + 1, 3 * n / 4);
        snprintf(r4, sizeof(r4), "%d - %d", 3 * n / 4 + 1, n - 1);
        snprintf(r5, sizeof(r5), "%d / %d", n, n);
        Tier tiers[5] = {{r1, "CASUAL FAN", rgba(160, 170, 190)}, {r2, "REAL FAN", rgba(46, 204, 113)},
                         {r3, "EXPERT", rgba(241, 196, 15)}, {r4, "GENIUS", rgba(230, 126, 34)},
                         {r5, "FOOTBALL LEGEND", rgba(255, 45, 85)}};
        for (int i = 0; i < 5; ++i) {
            float ka = ease_out_back(clamp01((age - 0.4f - i * 0.15f) / 0.35f));
            float y = 330 + i * 112;
            Rectangle row{DW / 2 - 520, y - 46, 1040, 92};
            g.rrect(row, 46, withA(rgba(14, 18, 32), 0.92f * clamp01(ka)));
            g.rrect_line(row, 46, 2, withA(tiers[i].c, 0.9f * clamp01(ka)));
            TextStyle rs; rs.size = 56; rs.color = tiers[i].c;
            draw_text(fonts, tiers[i].range, row.x + 60, y + 2, rs, 0, 0.0f, clamp01(ka));
            TextStyle ls; ls.size = 56; ls.spacing = 3;
            draw_text(fonts, tiers[i].label, row.x + row.width - 60, y + 2, ls, 0, 1.0f, clamp01(ka));
        }
        float kc = clamp01((age - 1.3f) / 0.35f);
        if (kc > 0) {
            float pulse = 1 + 0.04f * std::sin(age * 6);
            TextStyle c; c.kind = F_BOLD; c.size = 50 * pulse; c.shadow = 0; c.color = rgba(12, 14, 24);
            const char* cta = "COMMENT YOUR SCORE";
            float tw = text_width(fonts, cta, c);
            Rectangle b{DW / 2 - tw / 2 - 70, 930, tw + 140, 100};
            g.rrect(b, 50, withA(WHITE, 0.97f * kc));
            draw_text(fonts, cta, DW / 2 - 18, 982, c, 0, 0.5f, kc);
            float ax = DW / 2 + tw / 2 + 22, ay = 982 + 6 * std::sin(age * 8);
            DrawTriangle(Vector2{ax - 18, ay - 11}, Vector2{ax, ay + 13}, Vector2{ax + 18, ay - 11}, withA(rgba(12, 14, 24), kc));
        }
    }

    void draw_world(AnimContext& ctx) override {
        float t = (float)ctx.time;
        if (t < intro_t1) return;
        if (const Round* r = current(t)) {
            draw_clues(*r, t);
            draw_card_back(*r, t);
        }
    }

    void draw_world_glow(AnimContext& ctx) override {
        float t = (float)ctx.time;
        if (t < intro_t1) {   // contours lumineux des silhouettes de l'intro
            int shown = 0;
            float out = 1 - smooth01((t - (intro_t1 - 0.4f)) / 0.4f);
            for (int i = total_rounds - 1; i >= 0 && shown < 5; --i) {
                const Round& r = rounds[i];
                if (!r.mask.id) continue;
                Rectangle d = intro_rect(r, shown, t);
                g.tex(r.mask, Rectangle{d.x - 8, d.y - 8, d.width + 16, d.height + 16}, withA(accent, out));
                shown++;
            }
            return;
        }
        const Round* r = current(t);
        if (!r || !r->mask.id) return;
        const Level* l = level_of(r->level);
        float a = card_in(*r, t) * card_out(*r, t);
        float fade = t >= r->t_reveal ? 1 - clamp01((t - r->t_reveal) / 0.5f) : 1.0f;
        Rectangle d = photo_rect(r->mask);
        float sx = (1 - card_in(*r, t)) * 60;
        g.tex(r->mask, Rectangle{d.x + sx - 8, d.y - 8, d.width + 16, d.height + 16}, withA(l ? l->col : accent, a * fade));
    }

    void draw_hud(AnimContext& ctx) override {
        float t = (float)ctx.time;
        if (t < intro_t1) { draw_intro(t); return; }
        if (t < outro_t0) top_bar(t, current(t));
        if (const Round* r = current(t)) draw_card_front(*r, t);
        if (const Level* l = level_card(t)) draw_level_card(*l, t);
        if (t >= outro_t0 && outro_t0 > 0) draw_outro(t);
    }

    void shutdown(AnimContext& ctx) override {
        for (auto& r : rounds) {
            if (r.tex.id) UnloadTexture(r.tex);
            if (r.mask.id) UnloadTexture(r.mask);
            if (r.flag_tex.id) UnloadTexture(r.flag_tex);
        }
        FootballScene::shutdown(ctx);
    }
};

}  // namespace

Scene* make_quiz_scene() { return new QuizScene(); }
