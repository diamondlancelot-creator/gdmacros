// GD Speed Macro - mod Geode
//  - Menu sendiri (BUKAN di layar pause): tekan hotkey (default Y) di dalam level
//  - Speed: tombol preset + tombol -/+ langsung di menu
//  - Rekam / putar macro berbasis frame (240 TPS), format GDR2 (sama dengan bot lain)
//  - Muat macro dari file apa saja: GDR2/GDR, JSON GDR, dan .zbf (zBot). Macro bukan 240 TPS
//    dikonversi ke 240 TPS (bisa kurang akurat).
//  - SAFE MODE: kalau speed != 1.0 atau macro sedang diputar, level ditandai test mode
//    sehingga progres / persen / completion TIDAK tersimpan atau terkirim.
//
// Batasan: hanya level klasik (bukan platformer), normal mode (bukan practice), 240 TPS.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <gdr/gdr.hpp>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

// ---------- Pembantu agar tahan terhadap beda versi API Result ----------
template <class R>
static bool resultOk(R const& r) {
    if constexpr (requires { r.isOk(); }) return r.isOk();
    else if constexpr (requires { r.has_value(); }) return r.has_value();
    else if constexpr (requires { static_cast<bool>(r); }) return static_cast<bool>(r);
    else return true;
}

template <class R>
static auto resultValue(R&& r) {
    if constexpr (requires { std::forward<R>(r).unwrap(); }) return std::forward<R>(r).unwrap();
    else if constexpr (requires { r.has_value(); *r; }) return std::move(*r);
    else return std::forward<R>(r);
}

namespace {

struct MacroReplay : gdr::Replay<MacroReplay, gdr::Input<>> {
    MacroReplay() : Replay("GDSpeedMacro", 1) {}
};

struct Ev {
    uint64_t frame;
    uint8_t button;  // 1 = lompat, 2 = kiri, 3 = kanan
    bool p2;
    bool down;
};

struct State {
    bool recording = false;
    bool playing = false;
    bool speedActive = false;
    uint64_t frame = 0;          // nomor langkah fisika sejak awal attempt
    std::vector<Ev> events;
    size_t cursor = 0;
    std::set<std::pair<int, bool>> held;  // tombol yang sedang "ditekan" macro
    std::string loadedName;      // nama file macro yang sedang dimuat
    std::string loadedNote;      // catatan (mis. hasil konversi TPS)
} g;

void notify(std::string const& msg, NotificationIcon icon = NotificationIcon::None) {
    Notification::create(msg, icon, 2.f)->show();
}

bool isThisPlayLayer(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
}

std::filesystem::path macrosDir() {
    return Mod::get()->getSaveDir() / "macros";
}

std::string nameOf(std::filesystem::path const& p) {
    try {
        return p.filename().string();
    } catch (...) {
        return "(nama tidak valid)";
    }
}

std::vector<uint8_t> buildReplay(PlayLayer* pl) {
    MacroReplay r;
    r.author = "GDSpeedMacro";
    r.framerate = 240.0;
    r.gameVersion = GEODE_COMP_GD_VERSION;
    r.platformer = false;
    r.levelInfo.id = static_cast<uint32_t>(pl->m_level->m_levelID.value());
    r.levelInfo.name = static_cast<std::string>(pl->m_level->m_levelName);
    r.duration = static_cast<float>(g.frame) / 240.f;
    for (auto const& e : g.events) {
        r.inputs.push_back(gdr::Input<>(e.frame, e.button, e.p2, e.down));
    }
    auto out = r.exportData();
    return resultValue(std::move(out));
}

void saveMacro(PlayLayer* pl) {
    if (g.events.empty()) {
        notify("Belum ada macro untuk disimpan", NotificationIcon::Warning);
        return;
    }
    auto dir = macrosDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    auto path = dir / fmt::format("{}_{}.gdr2", static_cast<int>(pl->m_level->m_levelID.value()), stamp);
    auto res = geode::utils::file::writeBinary(path, buildReplay(pl));
    if (!resultOk(res)) {
        notify("Gagal menyimpan macro", NotificationIcon::Error);
        return;
    }
    g.loadedName = nameOf(path);
    g.loadedNote.clear();
    notify("Tersimpan: " + nameOf(path), NotificationIcon::Success);
    log::info("Macro disimpan di {}", path.string());
}

// ---------- Pembaca format ----------

uint64_t convertFrame(uint64_t f, double fps) {
    if (std::abs(fps - 240.0) < 0.01) return f;
    return static_cast<uint64_t>(std::llround(static_cast<double>(f) * 240.0 / fps));
}

// Format .zbf (zBot frame): float delta (1/fps), float speed, lalu rekaman 6 byte:
// uint32 frame, char '1'/'0' (tekan/lepas), char '1'/'0' (player 1 / player 2).
bool parseZbf(std::vector<uint8_t> const& b, std::vector<Ev>& out, double& fps) {
    if (b.size() < 14 || (b.size() - 8) % 6 != 0) return false;
    float delta = 0.f;
    std::memcpy(&delta, b.data(), 4);
    if (!(delta > 0.f && delta < 0.1f)) return false;
    fps = std::round(1.0 / static_cast<double>(delta));
    if (fps < 1.0) return false;
    uint32_t prev = 0;
    for (size_t i = 8; i + 6 <= b.size(); i += 6) {
        uint32_t f = 0;
        std::memcpy(&f, &b[i], 4);
        uint8_t d = b[i + 4];
        uint8_t p = b[i + 5];
        if ((d != '0' && d != '1') || (p != '0' && p != '1') || f < prev) return false;
        prev = f;
        out.push_back({static_cast<uint64_t>(f), static_cast<uint8_t>(1), p == '0', d == '1'});
    }
    return !out.empty();
}

bool loadMacro(std::filesystem::path const& path) {
    auto rd = geode::utils::file::readBinary(path);
    if (!resultOk(rd)) {
        notify("Gagal membaca file", NotificationIcon::Error);
        return false;
    }
    std::vector<uint8_t> bytes = resultValue(std::move(rd));

    std::vector<Ev> evs;
    double srcFps = 240.0;
    bool platformer = false;

    auto tryGdr = [&]() -> bool {
        evs.clear();
        auto imp = MacroReplay::importData(bytes);
        if (!resultOk(imp)) return false;
        auto rep = resultValue(std::move(imp));
        if (rep.platformer) {
            platformer = true;
            return false;
        }
        srcFps = rep.framerate > 0.0 ? rep.framerate : 240.0;
        for (auto const& in : rep.inputs) {
            evs.push_back({static_cast<uint64_t>(in.frame), static_cast<uint8_t>(in.button),
                           static_cast<bool>(in.player2), static_cast<bool>(in.down)});
        }
        return !evs.empty();
    };
    auto tryZbf = [&]() -> bool {
        evs.clear();
        return parseZbf(bytes, evs, srcFps);
    };

    bool zbfFirst = path.extension() == ".zbf";
    bool done = zbfFirst ? (tryZbf() || tryGdr()) : (tryGdr() || tryZbf());
    if (!done) {
        notify(platformer ? "Macro platformer belum didukung" : "Format file tidak dikenali / kosong",
               NotificationIcon::Error);
        return false;
    }

    std::string note;
    if (std::abs(srcFps - 240.0) > 0.01) {
        for (auto& e : evs) e.frame = convertFrame(e.frame, srcFps);
        note = fmt::format("{:.0f} TPS dikonversi ke 240, bisa kurang akurat", srcFps);
    }
    std::stable_sort(evs.begin(), evs.end(), [](Ev const& a, Ev const& b) { return a.frame < b.frame; });

    g.events = std::move(evs);
    g.loadedName = nameOf(path);
    g.loadedNote = note;
    notify(fmt::format("Macro dimuat: {} input", g.events.size()) + (note.empty() ? "" : " (" + note + ")"),
           note.empty() ? NotificationIcon::Success : NotificationIcon::Warning);
    return true;
}

void releaseHeld(PlayLayer* pl) {
    if (pl) {
        for (auto const& [button, p2] : g.held) {
            pl->handleButton(false, button, !p2);
        }
    }
    g.held.clear();
}

void stopPlaying(PlayLayer* pl) {
    g.playing = false;
    releaseHeld(pl);
}

void finishRecording(PlayLayer* pl) {
    g.recording = false;
    saveMacro(pl);
}

// ---------- Speed ----------
double getSpeed() {
    return Mod::get()->getSettingValue<double>("speed");
}

void setSpeed(double v) {
    v = std::clamp(std::round(v * 100.0) / 100.0, 0.1, 5.0);
    Mod::get()->setSettingValue<double>("speed", v);
}

std::vector<std::filesystem::path> listMacros() {
    std::vector<std::filesystem::path> files;
    try {
        std::error_code ec;
        auto dir = macrosDir();
        std::filesystem::create_directories(dir, ec);
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            std::error_code ec2;
            if (entry.is_regular_file(ec2)) files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) {
            std::error_code e1, e2;
            auto ta = std::filesystem::last_write_time(a, e1);
            auto tb = std::filesystem::last_write_time(b, e2);
            return ta > tb;  // terbaru di atas
        });
    } catch (...) {
    }
    return files;
}

void restartLevel() {
    auto scene = CCDirector::get()->getRunningScene();
    PauseLayer* pause = scene ? scene->getChildByType<PauseLayer>(0) : nullptr;
    if (pause) pause->onRestart(nullptr);
    else if (auto pl = PlayLayer::get()) pl->resetLevel();
}

// ---------- Menu mod (hotkey), tidak menempel di layar pause ----------
class MacroMenu : public CCLayer {
protected:
    struct Btn {
        CCNode* node;
        float baseScale;
        std::function<void()> cb;
    };
    std::vector<Btn> m_btns;
    int m_pressed = -1;
    int m_page = 0;

    static constexpr int kPerPage = 3;

    bool init() override {
        if (!CCLayer::init()) return false;
        this->setTouchEnabled(true);
        this->rebuild();
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -500, true);
    }

    void addLabel(std::string const& text, CCPoint pos, float scale, char const* font = "bigFont.fnt") {
        auto lbl = CCLabelBMFont::create(text.c_str(), font);
        lbl->setScale(scale);
        lbl->setPosition(pos);
        this->addChild(lbl, 2);
    }

    void addButton(std::string const& text, CCPoint pos, float scale, std::function<void()> cb) {
        auto spr = ButtonSprite::create(text.c_str());
        spr->setScale(scale);
        spr->setPosition(pos);
        this->addChild(spr, 2);
        m_btns.push_back({spr, scale, std::move(cb)});
    }

    void rebuild() {
        m_btns.clear();
        m_pressed = -1;
        this->removeAllChildrenWithCleanup(true);

        auto win = CCDirector::get()->getWinSize();
        float cx = win.width / 2.f, cy = win.height / 2.f;

        auto dim = CCLayerColor::create(ccColor4B{0, 0, 0, 150});
        this->addChild(dim, 0);

        auto panel = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png");
        panel->setContentSize({390.f, 285.f});
        panel->setPosition({cx, cy});
        this->addChild(panel, 1);

        addLabel("GD Speed Macro", {cx, cy + 124.f}, 0.7f, "goldFont.fnt");

        // ----- Speed -----
        double sp = getSpeed();
        addLabel(fmt::format("Speed: {:.2f}x{}", sp, std::abs(sp - 1.0) > 0.001 ? "  (progres tidak disimpan)" : ""),
                 {cx, cy + 100.f}, 0.42f);

        double presets[] = {0.25, 0.5, 1.0, 2.0, 3.0, 5.0};
        for (int i = 0; i < 6; i++) {
            double v = presets[i];
            addButton(fmt::format("{}x", v), {cx - 140.f + i * 56.f, cy + 74.f}, 0.5f, [this, v]() {
                setSpeed(v);
                this->rebuild();
            });
        }
        double steps[] = {-0.5, -0.1, 0.1, 0.5};
        char const* stepNames[] = {"-0.5", "-0.1", "+0.1", "+0.5"};
        for (int i = 0; i < 4; i++) {
            double d = steps[i];
            addButton(stepNames[i], {cx - 105.f + i * 70.f, cy + 50.f}, 0.5f, [this, d]() {
                setSpeed(getSpeed() + d);
                this->rebuild();
            });
        }

        // ----- Macro -----
        std::string status = g.loadedName.empty()
                                 ? "Macro: belum ada"
                                 : fmt::format("Macro: {} ({} input)", g.loadedName.substr(0, 24), g.events.size());
        addLabel(status, {cx, cy + 24.f}, 0.36f);
        if (!g.loadedNote.empty()) addLabel(g.loadedNote, {cx, cy + 11.f}, 0.28f);

        addButton(g.recording ? "Stop Rekam" : "Rekam", {cx - 105.f, cy - 12.f}, 0.55f, [this]() { onRecord(); });
        addButton(g.playing ? "Stop Putar" : "Putar", {cx, cy - 12.f}, 0.55f, [this]() { onPlay(); });
        addButton("Simpan", {cx + 100.f, cy - 12.f}, 0.55f, [this]() {
            if (auto pl = PlayLayer::get()) saveMacro(pl);
            this->rebuild();
        });

        // ----- Daftar file macro (semua jenis file di folder macros) -----
        auto files = listMacros();
        int pages = std::max(1, static_cast<int>((files.size() + kPerPage - 1) / kPerPage));
        m_page = std::clamp(m_page, 0, pages - 1);
        addLabel(fmt::format("File di folder macros ({} file) - hal {}/{}", files.size(), m_page + 1, pages),
                 {cx, cy - 38.f}, 0.3f);
        if (files.empty()) {
            addLabel("Kosong. Taruh file macro apa saja di folder (tombol Folder).", {cx, cy - 66.f}, 0.3f);
        }
        for (int i = 0; i < kPerPage; i++) {
            size_t idx = static_cast<size_t>(m_page) * kPerPage + i;
            if (idx >= files.size()) break;
            auto path = files[idx];
            addButton(nameOf(path).substr(0, 30), {cx, cy - 60.f - i * 22.f}, 0.4f, [this, path]() {
                loadMacro(path);
                this->rebuild();
            });
        }

        // ----- Baris bawah -----
        float by = cy - 124.f;
        addButton("<", {cx - 170.f, by}, 0.45f, [this]() {
            m_page--;
            this->rebuild();
        });
        addButton("Folder", {cx - 112.f, by}, 0.42f, []() {
            std::error_code ec;
            std::filesystem::create_directories(macrosDir(), ec);
            (void)geode::utils::file::openFolder(macrosDir());
        });
        addButton("Dari Setting", {cx - 28.f, by}, 0.42f, [this]() {
            auto path = Mod::get()->getSettingValue<std::filesystem::path>("macro-file");
            if (path.empty()) {
                notify("Pilih file di Settings mod (File macro) dulu", NotificationIcon::Warning);
                return;
            }
            loadMacro(path);
            this->rebuild();
        });
        addButton("Tutup", {cx + 66.f, by}, 0.42f, [this]() { this->close(); });
        addButton(">", {cx + 170.f, by}, 0.45f, [this]() {
            m_page++;
            this->rebuild();
        });
    }

    void onRecord() {
        auto pl = PlayLayer::get();
        if (!pl) return;
        if (g.recording) {
            finishRecording(pl);
            this->rebuild();
            return;
        }
        if (pl->m_isPracticeMode) {
            notify("Rekam hanya di mode normal (bukan practice)", NotificationIcon::Warning);
            return;
        }
        if (g.playing) stopPlaying(pl);
        g.events.clear();
        g.loadedName.clear();
        g.loadedNote.clear();
        g.recording = true;
        notify("Merekam dari awal level...", NotificationIcon::Success);
        restartLevel();
        this->close();
    }

    void onPlay() {
        auto pl = PlayLayer::get();
        if (!pl) return;
        if (g.playing) {
            stopPlaying(pl);
            notify("Macro dihentikan");
            this->rebuild();
            return;
        }
        if (g.events.empty()) {
            notify("Muat atau rekam macro dulu", NotificationIcon::Warning);
            return;
        }
        if (pl->m_isPracticeMode) {
            notify("Putar hanya di mode normal (bukan practice)", NotificationIcon::Warning);
            return;
        }
        g.recording = false;
        g.playing = true;
        g.cursor = 0;
        g.held.clear();
        notify("Memutar macro (progres tidak disimpan)", NotificationIcon::Success);
        restartLevel();
        this->close();
    }

    int hit(CCTouch* t) {
        auto p = this->convertToNodeSpace(t->getLocation());
        for (int i = static_cast<int>(m_btns.size()) - 1; i >= 0; i--) {
            if (m_btns[i].node->boundingBox().containsPoint(p)) return i;
        }
        return -1;
    }

public:
    static MacroMenu* create() {
        auto ret = new MacroMenu();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void close() { this->removeFromParentAndCleanup(true); }

    bool ccTouchBegan(CCTouch* t, CCEvent*) override {
        m_pressed = hit(t);
        if (m_pressed >= 0) m_btns[m_pressed].node->setScale(m_btns[m_pressed].baseScale * 0.92f);
        return true;  // semua sentuhan ditelan supaya tidak tembus ke game
    }

    void ccTouchEnded(CCTouch* t, CCEvent*) override {
        int idx = m_pressed;
        m_pressed = -1;
        if (idx < 0 || idx >= static_cast<int>(m_btns.size())) return;
        m_btns[idx].node->setScale(m_btns[idx].baseScale);
        if (hit(t) == idx) {
            auto cb = m_btns[idx].cb;  // salin: rebuild() bisa menghapus m_btns
            cb();
        }
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        if (m_pressed >= 0 && m_pressed < static_cast<int>(m_btns.size())) {
            m_btns[m_pressed].node->setScale(m_btns[m_pressed].baseScale);
        }
        m_pressed = -1;
    }
};

void toggleMenu() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    if (auto old = scene->getChildByID("menu"_spr)) {
        old->removeFromParentAndCleanup(true);
        return;
    }
    auto pl = PlayLayer::get();
    if (!pl) return;
    // game dijeda dulu supaya tidak jalan di belakang menu
    if (!scene->getChildByType<PauseLayer>(0)) pl->pauseGame(false);
    if (auto menu = MacroMenu::create()) {
        menu->setID("menu"_spr);
        scene->addChild(menu, 9999);
    }
}

// Hotkey dibaca langsung dari keyboard (Windows). Default Y, bisa diganti di Settings mod.
void pollHotkey() {
#ifdef GEODE_IS_WINDOWS
    static bool prev = false;
    bool now = false;
    if (PlayLayer::get()) {
        auto key = Mod::get()->getSettingValue<std::string>("menu-key");
        int vk = 'Y';
        if (!key.empty() && std::isalnum(static_cast<unsigned char>(key[0]))) {
            vk = std::toupper(static_cast<unsigned char>(key[0]));
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        now = pid == GetCurrentProcessId() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    if (now && !prev) toggleMenu();
    prev = now;
#endif
}

}  // namespace

// ---------- Speed: skala delta waktu scheduler ----------
class $modify(MacroScheduler, CCScheduler) {
    void update(float dt) {
        float s = 1.f;
        if (PlayLayer::get()) {
            auto scene = CCDirector::get()->getRunningScene();
            bool paused = scene && scene->getChildByType<PauseLayer>(0) != nullptr;
            if (!paused) s = static_cast<float>(Mod::get()->getSettingValue<double>("speed"));
        }
        g.speedActive = std::abs(s - 1.f) > 0.001f;
        CCScheduler::update(dt * s);
        pollHotkey();  // setelah update asli supaya aman mengubah scene
    }
};

// ---------- Input: rekam / putar per langkah fisika ----------
class $modify(MacroBase, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        bool mine = isThisPlayLayer(this);
        if (mine) {
            auto pl = PlayLayer::get();
            if (g.playing) {
                while (g.cursor < g.events.size() && g.events[g.cursor].frame <= g.frame) {
                    auto const& e = g.events[g.cursor++];
                    // Memanggil versi asli (bukan hook) agar tidak ikut terekam / terblokir
                    GJBaseGameLayer::handleButton(e.down, e.button, !e.p2);
                    if (e.down) g.held.insert({e.button, e.p2});
                    else g.held.erase({e.button, e.p2});
                }
                if (g.cursor >= g.events.size()) {
                    stopPlaying(pl);
                    notify("Macro selesai, kontrol kembali ke Anda", NotificationIcon::Success);
                }
            }
            // SAFE MODE: tandai sebagai test mode supaya tidak ada progres yang tersimpan
            if (g.playing || g.speedActive) pl->m_isTestMode = true;
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (mine && !isHalfTick) g.frame++;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (isThisPlayLayer(this)) {
            if (g.playing) return;  // input manual diblok selama macro berjalan
            if (g.recording) {
                g.events.push_back({g.frame, static_cast<uint8_t>(button), !isPlayer1, down});
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(MacroPlay, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g.frame = 0;
        if (g.recording) g.events.clear();  // attempt baru = rekam ulang dari awal
        if (g.playing) {
            g.cursor = 0;
            g.held.clear();
        }
    }

    void levelComplete() {
        if (g.playing || g.speedActive) m_isTestMode = true;  // safe mode
        bool wasRecording = g.recording;
        PlayLayer::levelComplete();
        if (wasRecording) finishRecording(this);
        if (g.playing) stopPlaying(this);
    }

    void onQuit() {
        if (g.recording) finishRecording(this);
        g.playing = false;
        g.recording = false;
        g.held.clear();
        PlayLayer::onQuit();
    }
};
