// Replay Tools - Geode mod
//  - Own in-game menu (NOT on the pause screen): press the hotkey (default Y) inside a level
//  - Speed: slider, presets and fine-tune buttons
//  - Record / play frame-based macros (240 TPS), GDR2 format (compatible with other bots)
//  - Load macros from any file: GDR2/GDR, GDR JSON and .zbf (zBot). Macros that are not
//    240 TPS are converted to 240 TPS (may be less accurate).
//  - SAFE MODE: while speed != 1.0, a macro is playing, or No Clip is on, the level is
//    marked as test mode so progress / percent / completion is NOT saved or submitted.
//
// Limits: classic levels only (no platformer), normal mode (no practice), 240 TPS.

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
#include <map>
#include <set>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

// ---------- Helpers that tolerate different Result API versions ----------
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
    uint8_t button;  // 1 = jump, 2 = left, 3 = right
    bool p2;
    bool down;
};

struct State {
    bool recording = false;
    bool playing = false;
    bool speedActive = false;
    bool noclip = false;         // No Clip (this game session only, not saved)
    bool injecting = false;      // true while the macro itself presses buttons (don't block / record)
    uint64_t frame = 0;          // physics step number since the start of the attempt
    std::vector<Ev> events;
    size_t cursor = 0;
    std::set<std::pair<int, bool>> held;  // buttons currently "held" by the macro
    std::string loadedName;      // name of the loaded macro file
    std::string loadedNote;      // note (e.g. TPS conversion result)
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
        return "(invalid name)";
    }
}

std::filesystem::path pathFromUtf8(std::string const& s) {
    std::u8string u(reinterpret_cast<char8_t const*>(s.data()), s.size());
    return std::filesystem::path(u);
}

// Truncates a UTF-8 string without breaking a character in the middle.
std::string truncUtf8(std::string s, size_t maxBytes, bool dots = true) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
    s.resize(cut);
    return dots ? s + "..." : s;
}

// Windows-safe file name: forbidden characters replaced, spaces become underscores.
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
    if (out.empty()) out = "Untitled";
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
        notify("No macro to save yet", NotificationIcon::Warning);
        return;
    }
    auto dir = macrosDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int levelId = static_cast<int>(pl->m_level->m_levelID.value());
    std::string levelName = sanitizeFileName(static_cast<std::string>(pl->m_level->m_levelName));
    std::string base = fmt::format("{}_{}", levelId > 0 ? std::to_string(levelId) : std::string("local"), levelName);
    auto path = dir / pathFromUtf8(base + ".gdr2");
    // If the name is taken, add a number so older recordings are never overwritten.
    for (int n = 2; n < 1000 && std::filesystem::exists(path, ec); n++) {
        path = dir / pathFromUtf8(fmt::format("{}_{}.gdr2", base, n));
    }
    auto res = geode::utils::file::writeBinary(path, buildReplay(pl));
    if (!resultOk(res)) {
        notify("Failed to save macro", NotificationIcon::Error);
        return;
    }
    g.loadedName = nameOf(path);
    g.loadedNote.clear();
    notify("Saved: " + nameOf(path), NotificationIcon::Success);
    log::info("Macro saved: {}", nameOf(path));
}

// ---------- Format readers ----------

uint64_t convertFrame(uint64_t f, double fps) {
    if (std::abs(fps - 240.0) < 0.01) return f;
    return static_cast<uint64_t>(std::llround(static_cast<double>(f) * 240.0 / fps));
}

// .zbf (zBot frame): float delta (1/fps), float speed, then 6-byte records:
// uint32 frame, char '1'/'0' (press/release), char '1'/'0' (player 1 / player 2).
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
        notify("Failed to read file", NotificationIcon::Error);
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
        notify(platformer ? "Platformer macros are not supported" : "Unknown format or empty file",
               NotificationIcon::Error);
        return false;
    }

    std::string note;
    if (std::abs(srcFps - 240.0) > 0.01) {
        for (auto& e : evs) e.frame = convertFrame(e.frame, srcFps);

        // Clean up after conversion: drop duplicate events and make sure very short
        // clicks (press and release landing on the same frame) are not lost.
        struct Key {
            bool down = false;
            uint64_t downFrame = 0;
        };
        std::map<std::pair<uint8_t, bool>, Key> state;
        std::vector<Ev> clean;
        for (auto e : evs) {
            auto& st = state[{e.button, e.p2}];
            if (e.down) {
                if (st.down) continue;  // already held, ignore
                st.down = true;
                st.downFrame = e.frame;
            } else {
                if (!st.down) continue;  // not held, ignore
                if (e.frame <= st.downFrame) e.frame = st.downFrame + 1;  // keep short clicks
                st.down = false;
            }
            clean.push_back(e);
        }
        evs = std::move(clean);

        note = fmt::format("{:.0f} TPS converted to 240, may be less accurate", srcFps);
    }
    std::stable_sort(evs.begin(), evs.end(), [](Ev const& a, Ev const& b) { return a.frame < b.frame; });

    g.events = std::move(evs);
    g.loadedName = nameOf(path);
    g.loadedNote = note;
    notify(fmt::format("Macro loaded: {} inputs", g.events.size()) + (note.empty() ? "" : " (" + note + ")"),
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

// The level is marked as test mode when safe mode is on and any assist is active.
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
            return ta > tb;  // newest first
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

// ---------- Mod menu (hotkey), not attached to the pause screen ----------
using Box = cocos2d::extension::CCScale9Sprite;

Box* makeBox(CCSize size, ccColor3B col, GLubyte opacity = 255) {
    auto b = Box::create("square02b_001.png");
    if (!b) b = Box::create();
    b->setContentSize(size);
    b->setColor(col);
    b->setOpacity(opacity);
    return b;
}

class MacroMenu : public CCLayer {
protected:
    enum class Tab { Speed = 0, Macro = 1, Files = 2, Options = 3 };

    struct Btn {
        CCNode* node;
        float baseScale;
        std::function<void()> cb;
    };
    std::vector<Btn> m_btns;
    CCNode* m_root = nullptr;
    CCNode* m_content = nullptr;
    CCNode* m_cur = nullptr;  // node that new widgets are added to
    CCLabelBMFont* m_speedLabel = nullptr;
    CCLabelBMFont* m_speedNote = nullptr;
    CCLayerColor* m_fill = nullptr;
    CCNode* m_knob = nullptr;
    int m_pressed = -1;
    int m_page = 0;
    Tab m_tab = Tab::Speed;
    bool m_dragging = false;
    bool m_outside = false;
    std::filesystem::path m_confirm;  // file waiting for delete confirmation

    static constexpr int kPerPage = 4;
    static constexpr float kTrackL = -92.f;
    static constexpr float kTrackR = 184.f;
    static constexpr float kTrackY = 17.f;

    // Palette: gold / orange / cyan on deep navy
    static inline const ccColor3B kWhite{255, 255, 255};
    static inline const ccColor3B kGold{255, 200, 40};
    static inline const ccColor3B kOrange{255, 140, 20};
    static inline const ccColor3B kCyan{40, 225, 255};
    static inline const ccColor3B kDark{18, 20, 36};
    static inline const ccColor3B kBg{15, 17, 38};
    static inline const ccColor3B kBar{24, 27, 58};
    static inline const ccColor3B kSide{21, 24, 50};
    static inline const ccColor3B kCard{27, 31, 64};
    static inline const ccColor3B kBtn{46, 52, 100};
    static inline const ccColor3B kOff{62, 66, 108};
    static inline const ccColor3B kMuted{150, 156, 195};
    static inline const ccColor3B kGood{110, 255, 160};
    static inline const ccColor3B kWarn{255, 190, 70};
    static inline const ccColor3B kBad{255, 100, 100};

    bool init() override {
        if (!CCLayer::init()) return false;
        this->setTouchEnabled(true);

        auto dim = CCLayerColor::create(ccColor4B{0, 0, 0, 0});
        dim->runAction(CCFadeTo::create(0.2f, 170));
        this->addChild(dim, 0);

        auto win = CCDirector::get()->getWinSize();
        m_root = CCNode::create();
        m_root->setPosition({win.width / 2.f, win.height / 2.f});
        this->addChild(m_root, 1);

        this->rebuild(true);

        m_root->setScale(0.9f);
        m_root->runAction(CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.f)));
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -500, true);
    }

    // ----- Widget helpers -----
    Box* addBox(CCPoint pos, CCSize size, ccColor3B col, GLubyte op = 255, int z = 1) {
        auto b = makeBox(size, col, op);
        b->setPosition(pos);
        m_cur->addChild(b, z);
        return b;
    }

    CCLabelBMFont* addText(std::string const& text, CCPoint pos, float scale, ccColor3B col = kWhite,
                           float anchorX = 0.f, char const* font = "bigFont.fnt") {
        auto lbl = CCLabelBMFont::create(text.c_str(), font);
        lbl->setAnchorPoint({anchorX, 0.5f});
        lbl->setScale(scale);
        lbl->setPosition(pos);
        lbl->setColor(col);
        m_cur->addChild(lbl, 4);
        return lbl;
    }

    Box* addBtn(std::string const& text, CCPoint pos, CCSize size, ccColor3B col, ccColor3B textCol, float textScale,
                std::function<void()> cb) {
        auto b = makeBox(size, col);
        b->setPosition(pos);
        m_cur->addChild(b, 2);
        auto lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        lbl->setScale(textScale);
        lbl->setColor(textCol);
        lbl->setPosition({size.width / 2.f, size.height / 2.f});
        b->addChild(lbl, 1);
        m_btns.push_back({b, 1.f, std::move(cb)});
        return b;
    }

    void addToggle(CCPoint pos, std::function<bool()> get, std::function<void(bool)> set) {
        bool on = get();
        auto pill = makeBox({42.f, 22.f}, on ? kCyan : kOff);
        pill->setPosition(pos);
        m_cur->addChild(pill, 2);
        auto knob = makeBox({18.f, 18.f}, kWhite);
        knob->setPosition({on ? 31.f : 11.f, 11.f});
        pill->addChild(knob, 1);
        m_btns.push_back({pill, 1.f, [pill, knob, get, set]() {
                              bool next = !get();
                              set(next);
                              ccColor3B c = next ? kCyan : kOff;
                              pill->stopAllActions();
                              pill->setScale(1.f);
                              pill->runAction(CCTintTo::create(0.12f, c.r, c.g, c.b));
                              knob->stopAllActions();
                              knob->runAction(
                                  CCEaseOut::create(CCMoveTo::create(0.12f, {next ? 31.f : 11.f, 11.f}), 2.f));
                          }});
    }

    // ----- Layout -----
    void buildFrame() {
        m_cur = m_root;
        addBox({0.f, -3.f}, {448.f, 278.f}, {0, 0, 0}, 90, 0);
        addBox({0.f, 0.f}, {440.f, 270.f}, kBg, 250, 0);
        addBox({0.f, 119.f}, {432.f, 28.f}, kBar, 255, 0);

        auto accent = CCLayerGradient::create(ccColor4B{255, 200, 40, 255}, ccColor4B{255, 120, 20, 255});
        accent->setContentSize({432.f, 2.f});
        accent->setVector({1.f, 0.f});
        accent->setPosition({-216.f, 104.f});
        m_root->addChild(accent, 1);

        addText("REPLAY TOOLS", {-206.f, 119.f}, 0.5f, kGold);
        addBtn("X", {198.f, 119.f}, {28.f, 20.f}, ccColor3B{190, 55, 70}, kWhite, 0.4f, [this]() { this->close(); });
    }

    void buildSidebar() {
        m_cur = m_root;
        addBox({-170.f, -13.f}, {92.f, 228.f}, kSide, 255, 0);
        char const* names[] = {"Speed", "Macro", "Files", "Options"};
        for (int i = 0; i < 4; i++) {
            bool sel = static_cast<int>(m_tab) == i;
            addBtn(names[i], {-170.f, 82.f - i * 36.f}, {80.f, 28.f}, sel ? kGold : kBtn, sel ? kDark : kWhite, 0.38f,
                   [this, i]() {
                       if (static_cast<int>(m_tab) != i) {
                           m_confirm.clear();
                           m_tab = static_cast<Tab>(i);
                           this->rebuild(true);
                       }
                   });
        }
        std::string key = Mod::get()->getSettingValue<std::string>("menu-key");
        if (key.empty()) key = "Y";
        key = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(key[0]))));
        addText("Hotkey: " + key, {-170.f, -118.f}, 0.26f, kMuted, 0.5f);
    }

    void rebuild(bool animate = false) {
        m_btns.clear();
        m_pressed = -1;
        m_dragging = false;
        m_speedLabel = nullptr;
        m_speedNote = nullptr;
        m_fill = nullptr;
        m_knob = nullptr;
        m_content = nullptr;
        m_root->removeAllChildrenWithCleanup(true);

        buildFrame();
        buildSidebar();

        m_content = CCNode::create();
        m_root->addChild(m_content, 2);
        m_cur = m_content;
        switch (m_tab) {
            case Tab::Speed: buildSpeedTab(); break;
            case Tab::Macro: buildMacroTab(); break;
            case Tab::Files: buildFilesTab(); break;
            case Tab::Options: buildOptionsTab(); break;
        }
        m_cur = m_root;

        if (animate) {
            m_content->setPositionX(14.f);
            m_content->runAction(CCEaseOut::create(CCMoveTo::create(0.16f, {0.f, 0.f}), 2.f));
        }
    }

    // ----- Speed tab -----
    void updateSpeedUI() {
        double sp = getSpeed();
        bool normal = std::abs(sp - 1.0) <= 0.001;
        if (m_speedLabel) {
            m_speedLabel->setString(fmt::format("{:.2f}x", sp).c_str());
            m_speedLabel->setColor(normal ? kCyan : kGold);
        }
        if (m_speedNote) {
            if (normal) {
                m_speedNote->setString("Normal speed");
                m_speedNote->setColor(kMuted);
            } else if (safeMode()) {
                m_speedNote->setString("Safe mode: progress will not be saved");
                m_speedNote->setColor(kWarn);
            } else {
                m_speedNote->setString("Safe mode OFF: progress may be saved");
                m_speedNote->setColor(kBad);
            }
        }
        float k = static_cast<float>((sp - 0.1) / 4.9);
        float x = kTrackL + std::clamp(k, 0.f, 1.f) * (kTrackR - kTrackL);
        if (m_fill) m_fill->setContentSize({std::max(1.f, x - kTrackL), 6.f});
        if (m_knob) m_knob->setPositionX(x);
    }

    void buildSpeedTab() {
        // Readout card
        addBox({48.f, 68.f}, {324.f, 62.f}, kCard, 255, 0);
        addText("GAME SPEED", {-106.f, 88.f}, 0.3f, kGold);
        m_speedLabel = addText("", {206.f, 72.f}, 0.85f, kCyan, 1.f);
        m_speedNote = addText("", {-106.f, 52.f}, 0.24f, kMuted);

        // Slider card
        addBox({48.f, 10.f}, {324.f, 48.f}, kCard, 255, 0);
        auto track = CCLayerColor::create(ccColor4B{58, 64, 112, 255}, kTrackR - kTrackL, 6.f);
        track->setPosition({kTrackL, kTrackY - 3.f});
        m_cur->addChild(track, 1);
        m_fill = CCLayerColor::create(ccColor4B{255, 190, 40, 255}, 10.f, 6.f);
        m_fill->setPosition({kTrackL, kTrackY - 3.f});
        m_cur->addChild(m_fill, 2);
        auto knob = makeBox({22.f, 22.f}, kGold);
        knob->setPosition({kTrackL, kTrackY});
        m_cur->addChild(knob, 3);
        m_knob = knob;
        addText("0.1x", {kTrackL, -4.f}, 0.22f, kMuted, 0.5f);
        addText("5x", {kTrackR, -4.f}, 0.22f, kMuted, 0.5f);

        // Presets + fine tune card
        addBox({48.f, -74.f}, {324.f, 114.f}, kCard, 255, 0);
        addText("PRESETS", {-106.f, -29.f}, 0.26f, kGold);
        double presets[] = {0.25, 0.5, 1.0, 2.0, 3.0, 5.0};
        for (int i = 0; i < 6; i++) {
            double v = presets[i];
            addBtn(fmt::format("{}x", v), {-82.f + i * 52.f, -52.f}, {46.f, 24.f}, kBtn, kWhite, 0.3f, [this, v]() {
                setSpeed(v);
                this->updateSpeedUI();
            });
        }
        addText("FINE TUNE", {-106.f, -78.f}, 0.26f, kGold);
        double steps[] = {-0.5, -0.1, 0.1, 0.5};
        char const* stepNames[] = {"-0.5", "-0.1", "+0.1", "+0.5"};
        for (int i = 0; i < 4; i++) {
            double d = steps[i];
            addBtn(stepNames[i], {-30.f + i * 52.f, -102.f}, {46.f, 24.f}, kBtn, kWhite, 0.3f, [this, d]() {
                setSpeed(getSpeed() + d);
                this->updateSpeedUI();
            });
        }

        this->updateSpeedUI();
    }

    // ----- Macro tab -----
    void buildMacroTab() {
        addBox({48.f, 64.f}, {324.f, 72.f}, kCard, 255, 0);
        addText("STATUS", {-106.f, 90.f}, 0.3f, kGold);
        char const* state = g.recording ? "Recording" : (g.playing ? "Playing" : "Idle");
        addText(state, {206.f, 90.f}, 0.34f, g.recording ? kBad : (g.playing ? kGood : kMuted), 1.f);
        if (g.loadedName.empty()) {
            addText("No macro loaded", {-106.f, 68.f}, 0.34f, kMuted);
        } else {
            addText(truncUtf8(g.loadedName, 30), {-106.f, 68.f}, 0.32f, kWhite);
        }
        addText(fmt::format("{} inputs   |   frame {}   |   input {}/{}", g.events.size(), g.frame, g.cursor,
                            g.events.size()),
                {-106.f, 50.f}, 0.22f, kMuted);
        if (!g.loadedNote.empty()) addText(g.loadedNote, {-106.f, 38.f}, 0.2f, kWarn);

        addBox({48.f, -8.f}, {324.f, 56.f}, kCard, 255, 0);
        addBtn(g.recording ? "Stop" : "Record", {-56.f, -8.f}, {92.f, 30.f}, g.recording ? ccColor3B{190, 55, 70} : kGold,
               g.recording ? kWhite : kDark, 0.38f, [this]() { onRecord(); });
        addBtn(g.playing ? "Stop" : "Play", {48.f, -8.f}, {92.f, 30.f}, g.playing ? ccColor3B{190, 55, 70} : kCyan,
               g.playing ? kWhite : kDark, 0.38f, [this]() { onPlay(); });
        addBtn("Save", {152.f, -8.f}, {92.f, 30.f}, kBtn, kWhite, 0.38f, [this]() {
            if (auto pl = PlayLayer::get()) saveMacro(pl);
            this->rebuild();
        });

        addBox({48.f, -76.f}, {324.f, 70.f}, kCard, 255, 0);
        addText("Records at 240 TPS (GDR2 format).", {-106.f, -58.f}, 0.24f, kMuted);
        addText("Classic levels and normal mode only.", {-106.f, -74.f}, 0.24f, kMuted);
        addText("Playback always starts from the beginning.", {-106.f, -90.f}, 0.24f, kMuted);
    }

    // ----- Files tab -----
    void buildFilesTab() {
        auto files = listMacros();
        int pages = std::max(1, static_cast<int>((files.size() + kPerPage - 1) / kPerPage));
        m_page = std::clamp(m_page, 0, pages - 1);

        addText("MACROS", {-106.f, 94.f}, 0.3f, kGold);
        addText(fmt::format("{} files   |   page {}/{}", files.size(), m_page + 1, pages), {206.f, 94.f}, 0.24f, kMuted,
                1.f);

        if (files.empty()) {
            addBox({48.f, 40.f}, {324.f, 80.f}, kCard, 255, 0);
            addText("No macros yet.", {48.f, 50.f}, 0.36f, kWhite, 0.5f);
            addText("Record one, or put files in the macros folder.", {48.f, 30.f}, 0.24f, kMuted, 0.5f);
        }
        for (int i = 0; i < kPerPage; i++) {
            size_t idx = static_cast<size_t>(m_page) * kPerPage + i;
            if (idx >= files.size()) break;
            auto path = files[idx];
            float y = 66.f - i * 34.f;
            addBox({48.f, y}, {324.f, 30.f}, kCard, 255, 0);
            addText(truncUtf8(nameOf(path), 26), {-102.f, y}, 0.28f, kWhite);
            addBtn("Load", {128.f, y}, {48.f, 22.f}, kCyan, kDark, 0.3f, [this, path]() {
                m_confirm.clear();
                loadMacro(path);
                this->rebuild();
            });
            bool armed = !m_confirm.empty() && m_confirm == path;
            addBtn(armed ? "Sure?" : "Delete", {182.f, y}, {52.f, 22.f}, armed ? ccColor3B{190, 55, 70} : kBtn, kWhite,
                   0.28f, [this, path, armed]() {
                       if (armed) {
                           std::error_code ec;
                           std::filesystem::remove(path, ec);
                           m_confirm.clear();
                           if (ec) notify("Failed to delete file", NotificationIcon::Error);
                           else notify("Deleted: " + nameOf(path), NotificationIcon::Success);
                       } else {
                           m_confirm = path;
                       }
                       this->rebuild();
                   });
        }

        float by = -106.f;
        addBtn("<", {-98.f, by}, {30.f, 24.f}, kBtn, kWhite, 0.4f, [this]() {
            m_confirm.clear();
            m_page--;
            this->rebuild();
        });
        addBtn(">", {-62.f, by}, {30.f, 24.f}, kBtn, kWhite, 0.4f, [this]() {
            m_confirm.clear();
            m_page++;
            this->rebuild();
        });
        addBtn("Open Folder", {20.f, by}, {96.f, 24.f}, kBtn, kWhite, 0.28f, []() {
            std::error_code ec;
            std::filesystem::create_directories(macrosDir(), ec);
            (void)geode::utils::file::openFolder(macrosDir());
        });
        addBtn("From Settings", {138.f, by}, {108.f, 24.f}, kGold, kDark, 0.26f, [this]() {
            auto path = Mod::get()->getSettingValue<std::filesystem::path>("macro-file");
            if (path.empty()) {
                notify("Pick a file in the mod settings (Macro file) first", NotificationIcon::Warning);
                return;
            }
            m_confirm.clear();
            loadMacro(path);
            this->rebuild();
        });
    }

    // ----- Options tab -----
    void buildOptionsTab() {
        addBox({48.f, 66.f}, {324.f, 60.f}, kCard, 255, 0);
        addText("Safe Mode", {-106.f, 80.f}, 0.4f, kWhite);
        addText("Marks the level as test mode so progress is not saved.", {-106.f, 58.f}, 0.2f, kMuted);
        addToggle({166.f, 68.f}, []() { return safeMode(); }, [this](bool v) {
            Mod::get()->setSettingValue<bool>("safe-mode", v);
            if (!v) notify("Safe mode off: progress and completions can be saved", NotificationIcon::Warning);
        });

        addBox({48.f, -2.f}, {324.f, 60.f}, kCard, 255, 0);
        addText("No Clip", {-106.f, 12.f}, 0.4f, kWhite);
        addText("Ignore deaths. Resets when the game restarts.", {-106.f, -10.f}, 0.2f, kMuted);
        addToggle({166.f, 0.f}, []() { return g.noclip; }, [](bool v) {
            g.noclip = v;
            notify(v ? "No Clip on" : "No Clip off");
        });

        addBox({48.f, -66.f}, {324.f, 52.f}, kCard, 255, 0);
        addText("Menu key and game speed can also be changed", {-106.f, -56.f}, 0.2f, kMuted);
        addText("in the mod settings (Geode > Replay Tools).", {-106.f, -72.f}, 0.2f, kMuted);
    }

    // ----- Actions -----
    void onRecord() {
        auto pl = PlayLayer::get();
        if (!pl) return;
        if (g.recording) {
            finishRecording(pl);
            this->rebuild();
            return;
        }
        if (pl->m_isPracticeMode) {
            notify("Recording works in normal mode only (not practice)", NotificationIcon::Warning);
            return;
        }
        if (g.playing) stopPlaying(pl);
        g.events.clear();
        g.loadedName.clear();
        g.loadedNote.clear();
        g.recording = true;
        notify("Recording from the start of the level...", NotificationIcon::Success);
        restartLevel();
        this->close();
    }

    void onPlay() {
        auto pl = PlayLayer::get();
        if (!pl) return;
        if (g.playing) {
            stopPlaying(pl);
            notify("Macro stopped");
            this->rebuild();
            return;
        }
        if (g.events.empty()) {
            notify("Load or record a macro first", NotificationIcon::Warning);
            return;
        }
        if (pl->m_isPracticeMode) {
            notify("Playback works in normal mode only (not practice)", NotificationIcon::Warning);
            return;
        }
        g.recording = false;
        g.playing = true;
        g.cursor = 0;
        g.held.clear();
        notify("Playing macro (progress will not be saved)", NotificationIcon::Success);
        restartLevel();
        this->close();
    }

    // ----- Touch handling -----
    int hit(CCTouch* t) {
        auto loc = t->getLocation();
        for (int i = static_cast<int>(m_btns.size()) - 1; i >= 0; i--) {
            auto n = m_btns[i].node;
            auto par = n->getParent();
            if (!par) continue;
            if (n->boundingBox().containsPoint(par->convertToNodeSpace(loc))) return i;
        }
        return -1;
    }

    bool inSliderArea(CCTouch* t) {
        if (!m_content || !m_knob) return false;
        auto p = m_content->convertToNodeSpace(t->getLocation());
        return p.x >= kTrackL - 14.f && p.x <= kTrackR + 14.f && std::abs(p.y - kTrackY) <= 16.f;
    }

    void setSliderFromTouch(CCTouch* t) {
        auto p = m_content->convertToNodeSpace(t->getLocation());
        float k = std::clamp((p.x - kTrackL) / (kTrackR - kTrackL), 0.f, 1.f);
        double v = 0.1 + static_cast<double>(k) * 4.9;
        setSpeed(std::round(v * 20.0) / 20.0);
        this->updateSpeedUI();
    }

    bool isOutsideWindow(CCTouch* t) {
        auto p = m_root->convertToNodeSpace(t->getLocation());
        return !(std::abs(p.x) <= 224.f && std::abs(p.y) <= 139.f);
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
        m_dragging = false;
        m_outside = false;
        m_pressed = hit(t);
        if (m_pressed >= 0) {
            auto& b = m_btns[m_pressed];
            b.node->stopAllActions();
            b.node->runAction(CCScaleTo::create(0.06f, b.baseScale * 0.94f));
        } else if (inSliderArea(t)) {
            m_dragging = true;
            setSliderFromTouch(t);
        } else {
            m_outside = isOutsideWindow(t);
        }
        return true;  // swallow all touches so they don't reach the game
    }

    void ccTouchMoved(CCTouch* t, CCEvent*) override {
        if (m_dragging) setSliderFromTouch(t);
    }

    void ccTouchEnded(CCTouch* t, CCEvent*) override {
        if (m_dragging) {
            m_dragging = false;
            return;
        }
        if (m_outside) {
            m_outside = false;
            if (isOutsideWindow(t)) this->close();
            return;
        }
        int idx = m_pressed;
        m_pressed = -1;
        if (idx < 0 || idx >= static_cast<int>(m_btns.size())) return;
        auto& b = m_btns[idx];
        b.node->stopAllActions();
        b.node->runAction(CCScaleTo::create(0.08f, b.baseScale));
        if (hit(t) == idx) {
            auto cb = b.cb;  // copy: rebuild() can clear m_btns
            cb();
        }
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        if (m_pressed >= 0 && m_pressed < static_cast<int>(m_btns.size())) {
            auto& b = m_btns[m_pressed];
            b.node->stopAllActions();
            b.node->setScale(b.baseScale);
        }
        m_pressed = -1;
        m_dragging = false;
        m_outside = false;
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
    // pause the game first so it doesn't keep running behind the menu
    if (!scene->getChildByType<PauseLayer>(0)) pl->pauseGame(false);
    if (auto menu = MacroMenu::create()) {
        menu->setID("menu"_spr);
        scene->addChild(menu, 9999);
    }
}

// The hotkey is read straight from the keyboard (Windows). Default Y, changeable in mod settings.
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

// ---------- Speed: scale the scheduler's delta time ----------
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
        pollHotkey();  // after the original update so changing the scene is safe
    }
};

// ---------- Input: record / play per physics step ----------
class $modify(MacroBase, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        bool mine = isThisPlayLayer(this);
        if (mine) {
            auto pl = PlayLayer::get();
            if (g.playing) {
                while (g.cursor < g.events.size() && g.events[g.cursor].frame <= g.frame) {
                    auto const& e = g.events[g.cursor++];
                    // Call the original (not the hook) so it isn't recorded / blocked
                    g.injecting = true;
                    GJBaseGameLayer::handleButton(e.down, e.button, !e.p2);
                    g.injecting = false;
                    if (e.down) g.held.insert({e.button, e.p2});
                    else g.held.erase({e.button, e.p2});
                }
                if (g.cursor >= g.events.size()) {
                    stopPlaying(pl);
                    notify("Macro finished, you have control again", NotificationIcon::Success);
                }
            }
            // SAFE MODE: mark as test mode so no progress is saved
            if (needTestMode()) pl->m_isTestMode = true;
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (mine && !isHalfTick) g.frame++;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (isThisPlayLayer(this) && !g.injecting) {
            if (g.playing) return;  // manual input is blocked while a macro runs
            if (g.recording) {
                g.events.push_back({g.frame, static_cast<uint8_t>(button), !isPlayer1, down});
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(MacroPlay, PlayLayer) {
    // No Clip: ignore deaths, except from the game's own anticheat spike
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g.noclip && object != m_anticheatSpike) return;
        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g.frame = 0;
        if (g.recording) g.events.clear();  // new attempt = record again from the start
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
