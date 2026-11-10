// ============================================================================
//  anim_runtime.cpp — Boucle de rendu hors temps réel, capture, encodage, son.
//  Usage : anim <sandbox_job.txt>   (fichier clé=valeur écrit par l'application)
// ============================================================================
#include "anim_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

// ---------------------------------------------------------------------------
// AnimContext
// ---------------------------------------------------------------------------
static const double MIN_SAME_SOUND_INTERVAL = 0.03;  // anti-cacophonie : 30 ms entre deux sons identiques
static const int MAX_SOUNDS_PER_FRAME = 8;

float AnimContext::randf(float min, float max) {
    std::uniform_real_distribution<float> d(min, max);
    return d(rng);
}

int AnimContext::randi(int min, int max) {
    std::uniform_int_distribution<int> d(min, max);
    return d(rng);
}

void AnimContext::sound(const std::string& name, float volume) {
    if (volume <= 0.0f || name.empty()) return;
    auto it = last_sound_time.find(name);
    if (sounds_this_frame >= MAX_SOUNDS_PER_FRAME ||
        (it != last_sound_time.end() && time - it->second < MIN_SAME_SOUND_INTERVAL)) {
        dropped_sounds++;
        return;
    }
    last_sound_time[name] = time;
    sounds_this_frame++;
    events.push_back({time, name, std::min(volume, 1.0f)});
}

void AnimContext::metric(const std::string& key, double value) {
    metrics[key] = value;
}

// ---------------------------------------------------------------------------
// Configuration (fichier clé=valeur)
// ---------------------------------------------------------------------------
struct JobConfig {
    std::string mode = "final";        // final | preview
    int render_width = 1080, render_height = 1920;
    int logical_width = 1080, logical_height = 1920;
    int fps = 60;
    double duration = 30.0;
    uint32_t seed = 1;
    std::string out_dir = "/work/sandbox_out";
    std::string sounds_dir;
    std::set<int> capture_frames;
    int crf = 18;
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static bool load_config(const char* path, JobConfig& cfg) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "mode") cfg.mode = v;
        else if (k == "render_width") cfg.render_width = std::stoi(v);
        else if (k == "render_height") cfg.render_height = std::stoi(v);
        else if (k == "logical_width") cfg.logical_width = std::stoi(v);
        else if (k == "logical_height") cfg.logical_height = std::stoi(v);
        else if (k == "fps") cfg.fps = std::stoi(v);
        else if (k == "duration") cfg.duration = std::stod(v);
        else if (k == "seed") cfg.seed = (uint32_t)std::stoul(v);
        else if (k == "out_dir") cfg.out_dir = v;
        else if (k == "sounds_dir") cfg.sounds_dir = v;
        else if (k == "crf") cfg.crf = std::stoi(v);
        else if (k == "capture_frames") {
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ','))
                if (!trim(item).empty()) cfg.capture_frames.insert(std::stoi(trim(item)));
        }
    }
    return true;
}

static std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

// ---------------------------------------------------------------------------
// Son : mixage des événements dans un WAV stéréo 44.1 kHz
// ---------------------------------------------------------------------------
static bool find_sound_file(const std::string& dir, const std::string& name, std::string& out) {
    const char* exts[] = {".wav", ".mp3", ".ogg", ".flac"};
    for (const char* e : exts) {
        std::string p = dir + "/" + name + e;
        if (FileExists(p.c_str())) { out = p; return true; }
    }
    return false;
}

static int mix_audio(const AnimContext& ctx, const JobConfig& cfg, const std::string& wav_path) {
    const int rate = 44100, ch = 2;
    size_t total = (size_t)(cfg.duration * rate);
    std::vector<float> mix(total * ch, 0.0f);

    std::map<std::string, std::vector<float>> cache;   // nom -> échantillons float entrelacés
    std::set<std::string> missing;
    int mixed = 0;

    for (const auto& ev : ctx.events) {
        if (!cache.count(ev.sound)) {
            std::string file;
            if (missing.count(ev.sound) || !find_sound_file(cfg.sounds_dir, ev.sound, file)) {
                if (!missing.count(ev.sound))
                    printf("[RUNTIME] Son introuvable : %s (dans %s)\n", ev.sound.c_str(), cfg.sounds_dir.c_str());
                missing.insert(ev.sound);
                continue;
            }
            Wave w = LoadWave(file.c_str());
            if (w.frameCount == 0) { missing.insert(ev.sound); continue; }
            WaveFormat(&w, rate, 16, ch);
            float* s = LoadWaveSamples(w);
            cache[ev.sound] = std::vector<float>(s, s + (size_t)w.frameCount * ch);
            UnloadWaveSamples(s);
            UnloadWave(w);
        }
        const std::vector<float>& smp = cache[ev.sound];
        size_t start = (size_t)(ev.time * rate) * ch;
        for (size_t i = 0; i < smp.size() && start + i < mix.size(); ++i)
            mix[start + i] += smp[i] * ev.volume * 0.7f;
        mixed++;
    }
    if (mixed == 0) return 0;

    // Normalisation si saturation
    float peak = 0.0f;
    for (float v : mix) peak = std::max(peak, std::fabs(v));
    float gain = peak > 0.98f ? 0.98f / peak : 1.0f;

    std::vector<short> pcm(mix.size());
    for (size_t i = 0; i < mix.size(); ++i)
        pcm[i] = (short)std::lround(std::max(-1.0f, std::min(1.0f, mix[i] * gain)) * 32767.0f);

    Wave out = {};
    out.frameCount = (unsigned int)total;
    out.sampleRate = rate;
    out.sampleSize = 16;
    out.channels = ch;
    out.data = pcm.data();
    if (!ExportWave(out, wav_path.c_str())) return -1;
    return mixed;
}

// ---------------------------------------------------------------------------
// Programme principal
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: anim <sandbox_job.txt>\n"); return 2; }
    JobConfig cfg;
    if (!load_config(argv[1], cfg)) { fprintf(stderr, "Config illisible : %s\n", argv[1]); return 2; }

    const bool preview = (cfg.mode == "preview");
    std::signal(SIGPIPE, SIG_IGN);   // si ffmpeg s'arrête, on veut une erreur propre et non un crash

    AnimContext ctx;
    ctx.width = cfg.logical_width;
    ctx.height = cfg.logical_height;
    ctx.fps = cfg.fps;
    ctx.duration = cfg.duration;
    ctx.total_frames = (int)std::lround(cfg.duration * cfg.fps);
    ctx.seed = cfg.seed;
    ctx.preview = preview;
    ctx.rng.seed(cfg.seed);

    // Fenêtre minuscule et cachée : tout est rendu dans une texture hors écran
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "anim");
    SetTargetFPS(0);

    RenderTexture2D target = LoadRenderTexture(cfg.render_width, cfg.render_height);
    Camera2D cam = {};
    cam.zoom = (float)cfg.render_width / (float)cfg.logical_width;   // la scène dessine en coordonnées logiques

    std::string video_only = cfg.out_dir + "/video_only.mp4";
    FILE* ffmpeg = nullptr;
    if (!preview) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - "
                 "-c:v libx264 -preset medium -crf %d -pix_fmt yuv420p -movflags +faststart '%s'",
                 cfg.render_width, cfg.render_height, cfg.fps, cfg.crf, video_only.c_str());
        ffmpeg = popen(cmd, "w");
        if (!ffmpeg) { fprintf(stderr, "Impossible de lancer ffmpeg\n"); return 3; }
    }

    Scene* scene = create_scene();
    scene->init(ctx);

    const float dt = 1.0f / (float)cfg.fps;
    const size_t frame_bytes = (size_t)cfg.render_width * cfg.render_height * 4;
    auto t0 = std::chrono::steady_clock::now();
    int last_pct = -1, captured = 0;

    for (int f = 0; f < ctx.total_frames; ++f) {
        ctx.frame = f;
        ctx.time = (double)f / cfg.fps;
        ctx.sounds_this_frame = 0;

        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginMode2D(cam);
        scene->draw(ctx);
        EndMode2D();
        EndTextureMode();

        // Maintient la fenêtre (cachée) en vie
        BeginDrawing();
        EndDrawing();

        bool need_pixels = !preview || cfg.capture_frames.count(f);
        if (need_pixels) {
            Image img = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&img);   // les render textures OpenGL sont stockées à l'envers
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            if (ffmpeg && fwrite(img.data, 1, frame_bytes, ffmpeg) != frame_bytes) {
                fprintf(stderr, "Écriture vers ffmpeg impossible à l'image %d\n", f);
                UnloadImage(img);
                pclose(ffmpeg);
                return 4;
            }
            if (cfg.capture_frames.count(f)) {
                char p[512];
                snprintf(p, sizeof(p), "%s/frame_%06d.png", cfg.out_dir.c_str(), f);
                ExportImage(img, p);
                captured++;
            }
            UnloadImage(img);
        }

        scene->update(ctx, dt);

        int pct = (int)(100.0 * (f + 1) / ctx.total_frames);
        if (pct / 10 != last_pct / 10) {
            printf("[RUNTIME] %s : %d%% (%d/%d images)\n", preview ? "aperçu" : "rendu", pct, f + 1, ctx.total_frames);
            fflush(stdout);
            last_pct = pct;
        }
    }

    scene->shutdown(ctx);
    delete scene;
    UnloadRenderTexture(target);
    CloseWindow();

    if (ffmpeg && pclose(ffmpeg) != 0) { fprintf(stderr, "ffmpeg a échoué pendant l'encodage\n"); return 5; }
    double render_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // --- Événements ---
    {
        std::ofstream ev(cfg.out_dir + "/events.jsonl");
        for (const auto& e : ctx.events)
            ev << "{\"t\": " << e.time << ", \"sound\": \"" << json_escape(e.sound) << "\", \"volume\": " << e.volume << "}\n";
    }

    // --- Son + assemblage final ---
    int mixed = 0;
    if (!preview) {
        std::string final_video = cfg.out_dir + "/video.mp4";
        std::string wav = cfg.out_dir + "/audio.wav";
        if (!cfg.sounds_dir.empty() && !ctx.events.empty()) mixed = mix_audio(ctx, cfg, wav);
        if (mixed < 0) { fprintf(stderr, "Écriture de audio.wav impossible\n"); return 6; }

        char cmd[2048];
        if (mixed > 0)
            snprintf(cmd, sizeof(cmd),
                     "ffmpeg -y -loglevel error -i '%s' -i '%s' -c:v copy -c:a aac -b:a 192k -shortest -movflags +faststart '%s'",
                     video_only.c_str(), wav.c_str(), final_video.c_str());
        else
            snprintf(cmd, sizeof(cmd), "mv '%s' '%s'", video_only.c_str(), final_video.c_str());
        if (std::system(cmd) != 0) { fprintf(stderr, "Assemblage vidéo/son impossible\n"); return 7; }
        std::remove(video_only.c_str());
    }

    // --- Métriques ---
    {
        std::ofstream m(cfg.out_dir + "/metrics.json");
        m << "{\n  \"mode\": \"" << cfg.mode << "\",\n"
          << "  \"frames\": " << ctx.total_frames << ",\n"
          << "  \"fps\": " << cfg.fps << ",\n"
          << "  \"duration\": " << cfg.duration << ",\n"
          << "  \"render_size\": [" << cfg.render_width << ", " << cfg.render_height << "],\n"
          << "  \"seed\": " << cfg.seed << ",\n"
          << "  \"render_seconds\": " << render_s << ",\n"
          << "  \"captured_frames\": " << captured << ",\n"
          << "  \"sound_events\": " << ctx.events.size() << ",\n"
          << "  \"sound_events_mixed\": " << mixed << ",\n"
          << "  \"sound_events_dropped\": " << ctx.dropped_sounds << ",\n"
          << "  \"scene\": {";
        bool first = true;
        for (const auto& kv : ctx.metrics) {
            double v = kv.second;
            m << (first ? "" : ", ") << "\"" << json_escape(kv.first) << "\": ";
            if (std::isfinite(v)) m << v; else m << "null";
            first = false;
        }
        m << "}\n}\n";
    }

    printf("[RUNTIME] Terminé en %.1f s (%d images, %zu sons).\n", render_s, ctx.total_frames, ctx.events.size());
    return 0;
}
