// GD Speed Macro - mod Geode
//  - Kecepatan game (slider di Settings, atau tombol -/+ di menu pause)
//  - Rekam / putar macro berbasis frame (240 TPS), format GDR2 (sama dengan bot lain)
//  - SAFE MODE: kalau speed != 1.0 atau macro sedang diputar, level ditandai test mode
//    sehingga progres / persen / completion TIDAK tersimpan atau terkirim.
//
// Batasan: hanya level klasik (bukan platformer), normal mode (bukan practice), 240 TPS.

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <gdr/gdr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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
} g;

void notify(std::string const& msg, NotificationIcon icon = NotificationIcon::None) {
    Notification::create(msg, icon, 2.f)->show();
}

bool isThisPlayLayer(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
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
    auto dir = Mod::get()->getSaveDir() / "macros";
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
    notify("Tersimpan: " + path.filename().string(), NotificationIcon::Success);
    log::info("Macro disimpan di {}", path.string());
}

bool loadMacro(std::filesystem::path const& path) {
    auto rd = geode::utils::file::readBinary(path);
    if (!resultOk(rd)) {
        notify("Gagal membaca file", NotificationIcon::Error);
        return false;
    }
    auto bytes = resultValue(std::move(rd));
    auto imp = MacroReplay::importData(bytes);
    if (!resultOk(imp)) {
        notify("Format macro tidak valid (butuh GDR2)", NotificationIcon::Error);
        return false;
    }
    auto rep = resultValue(std::move(imp));
    if (rep.platformer) {
        notify("Macro platformer belum didukung", NotificationIcon::Error);
        return false;
    }
    if (std::abs(rep.framerate - 240.0) > 0.01) {
        notify("Macro bukan 240 TPS, tidak didukung", NotificationIcon::Error);
        return false;
    }
    std::vector<Ev> evs;
    for (auto const& in : rep.inputs) {
        evs.push_back({static_cast<uint64_t>(in.frame), static_cast<uint8_t>(in.button), static_cast<bool>(in.player2),
                       static_cast<bool>(in.down)});
    }
    std::stable_sort(evs.begin(), evs.end(), [](Ev const& a, Ev const& b) { return a.frame < b.frame; });
    if (evs.empty()) {
        notify("File macro kosong", NotificationIcon::Error);
        return false;
    }
    g.events = std::move(evs);
    notify(fmt::format("Macro dimuat: {} input", g.events.size()), NotificationIcon::Success);
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
        if (mine) g.frame++;
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

// ---------- Menu di layar pause ----------
class $modify(MacroPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = CCMenu::create();
        menu->setID("gd-speed-macro-menu"_spr);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 55.f, win.height / 2.f});

        auto addBtn = [&](char const* text, SEL_MenuHandler cb, float y) {
            auto spr = ButtonSprite::create(text);
            spr->setScale(.55f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
            btn->setPosition({0.f, y});
            menu->addChild(btn);
        };

        addBtn(g.recording ? "Stop Rekam" : "Rekam", menu_selector(MacroPause::onRec), 85.f);
        addBtn(g.playing ? "Stop Putar" : "Putar", menu_selector(MacroPause::onPlayMacro), 55.f);
        addBtn("Simpan", menu_selector(MacroPause::onSave), 25.f);
        addBtn("Muat", menu_selector(MacroPause::onLoad), -5.f);
        addBtn("Speed -", menu_selector(MacroPause::onSlower), -35.f);
        addBtn("Speed +", menu_selector(MacroPause::onFaster), -65.f);

        this->addChild(menu);
    }

    void onRec(CCObject*) {
        auto pl = PlayLayer::get();
        if (!pl) return;
        if (g.recording) {
            finishRecording(pl);
            return;
        }
        if (pl->m_isPracticeMode) {
            notify("Rekam hanya di mode normal (bukan practice)", NotificationIcon::Warning);
            return;
        }
        if (g.playing) stopPlaying(pl);
        g.events.clear();
        g.recording = true;
        notify("Merekam dari awal level...", NotificationIcon::Success);
        this->onRestart(nullptr);
    }

    void onPlayMacro(CCObject*) {
        auto pl = PlayLayer::get();
        if (!pl) return;
        if (g.playing) {
            stopPlaying(pl);
            notify("Macro dihentikan");
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
        this->onRestart(nullptr);
    }

    void onSave(CCObject*) {
        if (auto pl = PlayLayer::get()) saveMacro(pl);
    }

    void onLoad(CCObject*) {
        auto path = Mod::get()->getSettingValue<std::filesystem::path>("macro-file");
        if (path.empty()) {
            notify("Pilih file macro di Settings mod dulu", NotificationIcon::Warning);
            return;
        }
        loadMacro(path);
    }

    void changeSpeed(double delta) {
        double v = Mod::get()->getSettingValue<double>("speed") + delta;
        v = std::clamp(std::round(v * 100.0) / 100.0, 0.1, 5.0);
        Mod::get()->setSettingValue<double>("speed", v);
        notify(fmt::format("Speed: {:.2f}x{}", v, std::abs(v - 1.0) > 0.001 ? " (progres tidak disimpan)" : ""));
    }

    void onSlower(CCObject*) { changeSpeed(-0.1); }
    void onFaster(CCObject*) { changeSpeed(+0.1); }
};
