// Replay Tools - Geode mod
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
#include <string_view>
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
    MacroReplay() : Replay("ReplayTools", 1) {}
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
    bool noclip = false;         // No Clip aktif (hanya selama sesi game, tidak disimpan)
    bool injecting = false;      // true saat macro sendiri yang menekan tombol (jangan diblok / direkam)
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
        auto u = p.filename().u8string();
        return std::string(reinterpret_cast<char const*>(u.data()), u.size());
    } catch (...) {
        return "(nama tidak valid)";
    }
}

std::filesystem::path pathFromUtf8(std::string const& s) {
    std::u8string u(reinterpret_cast<char8_t const*>(s.data()), s.size());
    return std::filesystem::path(u);
}

// Memotong string UTF-8 tanpa merusak karakter di tengah.
std::string truncUtf8(std::string s, size_t maxBytes, bool dots = true) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
    s.resize(cut);
    return dots ? s + "..." : s;
}

// Nama file yang aman di Windows: karakter terlarang diganti, spasi jadi garis bawah.
std::string sanitizeFileName(std::string const& in) {
    std::string out;
    for (unsigned char c : in) {
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos) {
            out += '_';
        } else if (c == ' ') {
            out += '_';
        } else {
            out += static_cast<char>(c);
        }
    }
    while (!out.empty() && (out.back() == '_' || out.back() == '.')) out.pop_back();
    out = truncUtf8(out, 40, false);
    while (!out.empty() && (out.back() == '_' || out.back() == '.')) out.pop_back();
    if (out.empty()) out = "Tanpa_Nama";
    return out;
}

std::vector<uint8_t> buildReplay(PlayLayer* pl) {
    MacroReplay r;
    r.author = "ReplayTools";
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
    int levelId = static_cast<int>(pl->m_level->m_levelID.value());
    std::string levelName = sanitizeFileName(static_cast<std::string>(pl->m_level->m_levelName));
    std::string base = fmt::format("{}_{}", levelId > 0 ? std::to_string(levelId) : std::string("local"), levelName);
    auto path = dir / pathFromUtf8(base + ".gdr2");
    // Jika nama sudah dipakai, tambahkan nomor urut agar rekaman lama tidak tertimpa.
    for (int n = 2; n < 1000 && std::filesystem::exists(path, ec); n++) {
        path = dir / pathFromUtf8(fmt::format("{}_{}.gdr2", base, n));
    }
    auto res = geode::utils::file::writeBinary(path, buildReplay(pl));
    if (!resultOk(res)) {
        notify("Gagal menyimpan macro", NotificationIcon::Error);
        return;
    }
    g.loadedName = nameOf(path);
    g.loadedNote.clear();
    notify("Tersimpan: " + nameOf(path), NotificationIcon::Success);
    log::info("Macro disimpan: {}", nameOf(path));
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
        g.injecting = true;
        for (auto const& [button, p2] : g.held) {
            pl->handleButton(false, button, !p2);
        }
        g.injecting = false;
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

// ---------- Safe mode ----------
bool safeMode() {
    return Mod::get()->getSettingValue<bool>("safe-mode");
}

// Level ditandai test mode bila safe mode aktif dan ada bantuan (macro, speed, atau no clip).
bool needTestMode() {
    return safeMode() && (g.playing || g.speedActive || g.noclip);
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
    CCNode* m_root = nullptr;
    int m_pressed = -1;
    int m_page = 0;
    std::filesystem::path m_confirm;  // file yang menunggu konfirmasi hapus

    static constexpr int kPerPage = 3;

    static inline const ccColor3B kWhite{255, 255, 255};
    static inline const ccColor3B kGold{255, 215, 100};
    static inline const ccColor3B kMuted{165, 170, 180};
    static inline const ccColor3B kGood{120, 255, 140};
    static inline const ccColor3B kWarn{255, 200, 80};
    static inline const ccColor3B kBad{255, 120, 110};

    bool init() override {
        if (!CCLayer::init()) return false;
        this->setTouchEnabled(true);

        auto dim = CCLayerColor::create(ccColor4B{0, 0, 0, 160});
        this->addChild(dim, 0);

        auto win = CCDirector::get()->getWinSize();
        m_root = CCNode::create();
        m_root->setPosition({win.width / 2.f, win.height / 2.f});
        this->addChild(m_root, 1);

        this->rebuild();

        // animasi muncul singkat
        m_root->setScale(0.85f);
        m_root->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -500, true);
    }

    void addLabel(std::string const& text, CCPoint pos, float scale, float anchorX = 0.5f,
                  char const* font = "bigFont.fnt", ccColor3B color = kWhite) {
        auto lbl = CCLabelBMFont::create(text.c_str(), font);
        lbl->setAnchorPoint({anchorX, 0.5f});
        lbl->setScale(scale);
        lbl->setPosition(pos);
        lbl->setColor(color);
        m_root->addChild(lbl, 3);
    }

    void addCard(CCPoint center, CCSize size) {
        auto card = cocos2d::extension::CCScale9Sprite::create("square02b_001.png");
        if (!card) return;
        card->setContentSize(size);
        card->setPosition(center);
        card->setColor({0, 0, 0});
        card->setOpacity(95);
        m_root->addChild(card, 1);
    }

    void addButton(std::string const& text, CCPoint pos, float scale, std::function<void()> cb) {
        auto spr = ButtonSprite::create(text.c_str());
        spr->setScale(scale);
        spr->setPosition(pos);
        m_root->addChild(spr, 4);
        m_btns.push_back({spr, scale, std::move(cb)});
    }

    void rebuild() {
        m_btns.clear();
        m_pressed = -1;
        m_root->removeAllChildrenWithCleanup(true);

        // ----- Panel utama -----
        if (auto panel = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png")) {
            panel->setContentSize({420.f, 312.f});
            m_root->addChild(panel, 0);
        }
        addLabel("Replay Tools", {0.f, 141.f}, 0.65f, 0.5f, "goldFont.fnt");

        // ----- Kartu: Kecepatan -----
        addCard({0.f, 92.f}, {396.f, 64.f});
        double sp = getSpeed();
        bool normal = std::abs(sp - 1.0) <= 0.001;
        addLabel("KECEPATAN", {-188.f, 114.f}, 0.3f, 0.f, "bigFont.fnt", kGold);
        if (!normal) {
            bool safe = safeMode();
            addLabel(safe ? "progres tidak disimpan" : "safe mode mati: progres bisa tersimpan", {-110.f, 114.f}, 0.24f,
                     0.f, "bigFont.fnt", safe ? kWarn : kBad);
        }
        addLabel(fmt::format("{:.2f}x", sp), {188.f, 114.f}, 0.5f, 1.f, "bigFont.fnt", normal ? kGood : kWarn);

        double presets[] = {0.25, 0.5, 1.0, 2.0, 3.0, 5.0};
        for (int i = 0; i < 6; i++) {
            double v = presets[i];
            addButton(fmt::format("{}x", v), {-145.f + i * 58.f, 96.f}, 0.5f, [this, v]() {
                setSpeed(v);
                this->rebuild();
            });
        }
        double steps[] = {-0.5, -0.1, 0.1, 0.5};
        char const* stepNames[] = {"-0.5", "-0.1", "+0.1", "+0.5"};
        for (int i = 0; i < 4; i++) {
            double d = steps[i];
            addButton(stepNames[i], {-105.f + i * 70.f, 73.f}, 0.5f, [this, d]() {
                setSpeed(getSpeed() + d);
                this->rebuild();
            });
        }

        // ----- Kartu: Macro -----
        addCard({0.f, 27.f}, {396.f, 58.f});
        addLabel("MACRO", {-188.f, 48.f}, 0.3f, 0.f, "bigFont.fnt", kGold);
        char const* state = g.recording ? "Merekam" : (g.playing ? "Memutar" : "Siap");
        addLabel(fmt::format("{}  |  frame {}  |  {}/{}", state, g.frame, g.cursor, g.events.size()),
                 {188.f, 48.f}, 0.24f, 1.f, "bigFont.fnt", g.recording ? kBad : (g.playing ? kGood : kMuted));
        if (g.loadedName.empty()) {
            addLabel("Belum ada macro dimuat", {-188.f, 35.f}, 0.32f, 0.f, "bigFont.fnt", kMuted);
        } else {
            addLabel(fmt::format("{} ({} input)", truncUtf8(g.loadedName, 34), g.events.size()), {-188.f, 35.f}, 0.32f,
                     0.f);
        }
        if (!g.loadedNote.empty()) addLabel(g.loadedNote, {-188.f, 26.f}, 0.22f, 0.f, "bigFont.fnt", kWarn);

        addButton(g.recording ? "Stop Rekam" : "Rekam", {-110.f, 10.f}, 0.55f, [this]() { onRecord(); });
        addButton(g.playing ? "Stop Putar" : "Putar", {0.f, 10.f}, 0.55f, [this]() { onPlay(); });
        addButton("Simpan", {110.f, 10.f}, 0.55f, [this]() {
            if (auto pl = PlayLayer::get()) saveMacro(pl);
            this->rebuild();
        });

        // ----- Kartu: Opsi -----
        addCard({0.f, -20.f}, {396.f, 28.f});
        bool safeNow = safeMode();
        addButton(safeNow ? "Safe Mode: ON" : "Safe Mode: OFF", {-95.f, -20.f}, 0.5f, [this]() {
            bool next = !safeMode();
            Mod::get()->setSettingValue<bool>("safe-mode", next);
            if (!next) {
                notify("Safe mode mati: progres dan completion bisa tersimpan/terkirim", NotificationIcon::Warning);
            }
            this->rebuild();
        });
        addButton(g.noclip ? "No Clip: ON" : "No Clip: OFF", {95.f, -20.f}, 0.5f, [this]() {
            g.noclip = !g.noclip;
            notify(g.noclip ? "No Clip aktif" : "No Clip mati");
            this->rebuild();
        });

        // ----- Kartu: File macro -----
        addCard({0.f, -85.f}, {396.f, 94.f});
        auto files = listMacros();
        int pages = std::max(1, static_cast<int>((files.size() + kPerPage - 1) / kPerPage));
        m_page = std::clamp(m_page, 0, pages - 1);
        addLabel("FILE MACRO", {-188.f, -48.f}, 0.3f, 0.f, "bigFont.fnt", kGold);
        addLabel(fmt::format("{} file  |  hal {}/{}", files.size(), m_page + 1, pages), {188.f, -48.f}, 0.24f, 1.f,
                 "bigFont.fnt", kMuted);
        if (files.empty()) {
            addLabel("Belum ada file. Rekam macro, atau taruh file di folder macros.", {0.f, -92.f}, 0.28f, 0.5f,
                     "bigFont.fnt", kMuted);
        }
        for (int i = 0; i < kPerPage; i++) {
            size_t idx = static_cast<size_t>(m_page) * kPerPage + i;
            if (idx >= files.size()) break;
            auto path = files[idx];
            float y = -69.f - i * 22.f;
            addButton(truncUtf8(nameOf(path), 28), {-42.f, y}, 0.4f, [this, path]() {
                m_confirm.clear();
                loadMacro(path);
                this->rebuild();
            });
            bool armed = !m_confirm.empty() && m_confirm == path;
            addButton(armed ? "Yakin?" : "Hapus", {152.f, y}, 0.4f, [this, path, armed]() {
                if (armed) {
                    std::error_code ec;
                    std::filesystem::remove(path, ec);
                    m_confirm.clear();
                    if (ec) notify("Gagal menghapus file", NotificationIcon::Error);
                    else notify("Dihapus: " + nameOf(path), NotificationIcon::Success);
                } else {
                    m_confirm = path;
                }
                this->rebuild();
            });
        }

        // ----- Baris bawah -----
        float by = -145.f;
        addButton("<", {-172.f, by}, 0.45f, [this]() {
            m_confirm.clear();
            m_page--;
            this->rebuild();
        });
        addButton("Folder", {-108.f, by}, 0.42f, []() {
            std::error_code ec;
            std::filesystem::create_directories(macrosDir(), ec);
            (void)geode::utils::file::openFolder(macrosDir());
        });
        addButton("Dari Setting", {-26.f, by}, 0.42f, [this]() {
            auto path = Mod::get()->getSettingValue<std::filesystem::path>("macro-file");
            if (path.empty()) {
                notify("Pilih file di Settings mod (File macro) dulu", NotificationIcon::Warning);
                return;
            }
            m_confirm.clear();
            loadMacro(path);
            this->rebuild();
        });
        addButton("Tutup", {62.f, by}, 0.42f, [this]() { this->close(); });
        addButton(">", {172.f, by}, 0.45f, [this]() {
            m_confirm.clear();
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
        auto p = m_root->convertToNodeSpace(t->getLocation());
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
                    g.injecting = true;
                    GJBaseGameLayer::handleButton(e.down, e.button, !e.p2);
                    g.injecting = false;
                    if (e.down) g.held.insert({e.button, e.p2});
                    else g.held.erase({e.button, e.p2});
                }
                if (g.cursor >= g.events.size()) {
                    stopPlaying(pl);
                    notify("Macro selesai, kontrol kembali ke Anda", NotificationIcon::Success);
                }
            }
            // SAFE MODE: tandai sebagai test mode supaya tidak ada progres yang tersimpan
            if (needTestMode()) pl->m_isTestMode = true;
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (mine && !isHalfTick) g.frame++;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (isThisPlayLayer(this) && !g.injecting) {
            if (g.playing) return;  // input manual diblok selama macro berjalan
            if (g.recording) {
                g.events.push_back({g.frame, static_cast<uint8_t>(button), !isPlayer1, down});
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(MacroPlay, PlayLayer) {
    // No Clip: abaikan kematian, kecuali dari anticheat spike milik game
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g.noclip && object != m_anticheatSpike) return;
        PlayLayer::destroyPlayer(player, object);
    }

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
        if (needTestMode()) m_isTestMode = true;  // safe mode
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
