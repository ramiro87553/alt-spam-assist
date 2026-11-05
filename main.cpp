#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <random>
#include <string>
#include <vector>

using namespace geode::prelude;

template <class T>
static T S(char const* key) {
    return Mod::get()->template getSettingValue<T>(key);
}

static double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

static double rand01() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng());
}

struct SpamState {
    std::deque<double> downs;
    bool active = false;
    bool autoShip = false;
    bool physDown = false;
    bool gameDown = false;
    bool injecting = false;
    bool pressWasAlt = false;
    bool targetP1 = true;
    double physDownAt = 0.0;
    double lastPhys = 0.0;
    double phaseStart = 0.0;
    double jit = 1.0;
    double cps = 0.0;
    double drift = 0.0;
    double driftTarget = 0.0;
    double driftTimer = 0.0;
    bool hasTarget = false;
    double targetY = 0.0;
    double lastY = 0.0;
    double bias = 0.5;
    bool shipLocked = false;
    long shownTenths = -1;
    bool shownShip = false;
    unsigned altCounter = 0;
    WeakRef<CCLabelBMFont> label;
};

static SpamState g;

static void resetState() {
    auto weak = g.label;
    g = SpamState{};
    g.label = weak;
    if (auto label = weak.lock()) label->setVisible(false);
}

static void updateLabel() {
    auto label = g.label.lock();
    if (!label) return;
    bool show = g.active && S<bool>("show-indicator");
    label->setVisible(show);
    if (!show) {
        g.shownTenths = -1;
        return;
    }
    bool fixedCps = g.autoShip || S<std::string>("mode") == "Fixed";
    double cps = fixedCps ? S<double>("fixed-cps") : g.cps;
    cps = std::min(cps, S<double>("max-cps"));
    long tenths = std::lround(cps * 10.0);
    if (tenths != g.shownTenths || g.shownShip != g.shipLocked) {
        g.shownTenths = tenths;
        g.shownShip = g.shipLocked;
        label->setString(
            fmt::format("SPAM ASSIST {:.1f} CPS{}", tenths / 10.0, g.shipLocked ? " | SHIP" : "").c_str()
        );
    }
}

static bool isActivePlayLayer(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
}

static bool inShip(GJBaseGameLayer* l) {
    return l->m_player1 && l->m_player1->m_isShip;
}

static void sendInput(GJBaseGameLayer* l, bool down) {
    g.injecting = true;
    bool dual = l->m_gameState.m_isDualMode;
    if (down) {
        bool alt = dual && S<bool>("alternate-players");
        g.pressWasAlt = alt;
        if (alt) {
            g.targetP1 = (g.altCounter++ % 2 == 0);
            l->handleButton(true, 1, g.targetP1);
        } else {
            l->handleButton(true, 1, true);
            if (dual) l->handleButton(true, 1, false);
        }
    } else {
        if (g.pressWasAlt) {
            l->handleButton(false, 1, g.targetP1);
        } else {
            l->handleButton(false, 1, true);
            if (dual) l->handleButton(false, 1, false);
        }
    }
    g.injecting = false;
    g.gameDown = down;
}

static void deactivate(GJBaseGameLayer* l) {
    if (!g.active) return;
    g.active = false;
    g.autoShip = false;
    if (g.physDown) {
        if (!g.gameDown) {
            sendInput(l, true);
        } else if (g.pressWasAlt) {
            sendInput(l, false);
            sendInput(l, true);
        }
    } else if (g.gameDown) {
        sendInput(l, false);
    }
    g.downs.clear();
    g.hasTarget = false;
    g.shipLocked = false;
    updateLabel();
}

static bool keyMode() {
    return S<bool>("trigger-keys-only");
}

static void noteDown(double t) {
    if (!g.downs.empty() && t - g.downs.back() < 4.0) {
        g.physDown = true;
        g.lastPhys = t;
        return;
    }
    if (g.active && !g.downs.empty()) {
        double gap = t - g.downs.back();
        if (gap > 1.0) {
            double inst = std::clamp(1000.0 / gap, 2.0, 60.0);
            g.cps = g.cps * 0.7 + inst * 0.3;
        }
    }
    g.physDown = true;
    g.physDownAt = t;
    g.lastPhys = t;
    g.downs.push_back(t);
    if (g.downs.size() > 64) g.downs.pop_front();
}

static void noteUp(GJBaseGameLayer* l, double t) {
    if (!g.physDown) return;
    g.physDown = false;
    g.lastPhys = t;
    double held = t - g.physDownAt;
    if (held > static_cast<double>(S<int64_t>("hold-release-ms"))) {
        g.downs.clear();
        deactivate(l);
    }
}

static bool shouldEngage(GJBaseGameLayer* l) {
    if (l->m_player1 && l->m_player1->m_isDead) return false;
    double now = nowMs();
    double window = static_cast<double>(S<int64_t>("window-ms"));
    while (!g.downs.empty() && now - g.downs.front() > window) g.downs.pop_front();
    size_t n = g.downs.size();
    if (n < static_cast<size_t>(S<int64_t>("min-clicks"))) return false;
    double span = g.downs.back() - g.downs.front();
    if (span <= 0.0) return false;
    double cps = (n - 1) * 1000.0 / span;
    if (cps < S<double>("cps-threshold")) return false;

    if (S<bool>("smart-detect")) {
        std::vector<double> iv;
        iv.reserve(n - 1);
        for (size_t i = 1; i < n; i++) iv.push_back(g.downs[i] - g.downs[i - 1]);
        double mean = span / static_cast<double>(n - 1);
        double var = 0.0;
        for (double v : iv) var += (v - mean) * (v - mean);
        var /= static_cast<double>(iv.size());
        double cv = std::sqrt(var) / mean;
        if (cv > S<double>("consistency")) return false;
        if (iv.back() > mean * 1.8) return false;
    }
    g.cps = cps;
    return true;
}

static double shipDuty(GJBaseGameLayer* l, double dt) {
    auto p = l->m_player1;
    double y = p->getPositionY();
    double sdt = std::clamp(dt, 1e-4, 0.05);
    if (!g.hasTarget) {
        g.targetY = y;
        g.lastY = y;
        g.bias = 0.5;
        g.hasTarget = true;
        return 0.5;
    }
    double vy = (y - g.lastY) / sdt;
    g.lastY = y;
    if (std::abs(g.targetY - y) > 150.0) {
        g.targetY = y;
        g.bias = 0.5;
        return 0.5;
    }
    double k = S<double>("ship-correction");
    double err = g.targetY - y;
    double sign = p->m_isUpsideDown ? -1.0 : 1.0;
    g.bias += sign * 0.002 * k * err * sdt;
    g.bias = std::clamp(g.bias, 0.2, 0.8);
    double u = g.bias + sign * (0.006 * k * err - 0.003 * k * vy);
    return std::clamp(u, 0.12, 0.88);
}

static double phaseJitter(bool humanizeActive) {
    if (!humanizeActive) return 1.0;
    double sigma = S<double>("humanize-timing") / 100.0 / 2.0;
    if (sigma <= 0.0) return 1.0;
    std::normal_distribution<double> nd(0.0, sigma);
    return std::clamp(1.0 + nd(rng()), 0.5, 1.6);
}

class $modify(SpamAssistLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g.injecting || button != 1 || !S<bool>("enabled") || !isActivePlayLayer(this)) {
            return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }

        bool byKeys = keyMode();

        if (!byKeys) {
            double t = nowMs();
            if (down) noteDown(t);
            else noteUp(this, t);
        }

        if (g.active && g.autoShip && down) {
            deactivate(this);
            return;
        }

        if (g.active) return;

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        g.gameDown = down;

        if (!byKeys && down && shouldEngage(this)) {
            g.active = true;
            g.phaseStart = nowMs();
            g.jit = 1.0;
            g.hasTarget = false;
            updateLabel();
        }
    }

    void update(float dt) {
        if (!g.active && isActivePlayLayer(this) && S<bool>("enabled") && S<bool>("ship-auto") &&
            !keyMode() && inShip(this) && !this->m_player1->m_isDead &&
            !g.physDown && nowMs() - g.lastPhys > 150.0) {
            g.active = true;
            g.autoShip = true;
            g.phaseStart = nowMs();
            g.jit = 1.0;
            g.hasTarget = false;
            updateLabel();
        }

        if (g.active && isActivePlayLayer(this)) {
            double t = nowMs();
            double hold = static_cast<double>(S<int64_t>("hold-release-ms"));
            double idle = static_cast<double>(S<int64_t>("idle-timeout-ms"));

            if (!g.autoShip && g.downs.size() >= 4) {
                size_t n = g.downs.size();
                double recent = (g.downs[n - 1] - g.downs[n - 4]) / 3.0;
                idle = std::min(idle, std::max(40.0, recent * 1.5));
            }

            if (!S<bool>("enabled")) {
                deactivate(this);
            } else if (!g.autoShip && g.physDown && t - g.physDownAt > hold) {
                deactivate(this);
            } else if (!g.autoShip && !g.physDown && t - g.lastPhys > idle) {
                deactivate(this);
            } else if (g.autoShip && (!inShip(this) || !S<bool>("ship-auto"))) {
                deactivate(this);
            } else if (this->m_player1 && this->m_player1->m_isDead) {
                deactivate(this);
            } else {
                bool ship = inShip(this) && (g.autoShip || S<bool>("ship-straight"));
                g.shipLocked = ship;
                if (!ship) g.hasTarget = false;

                bool human = S<bool>("humanize") && (!ship || S<bool>("humanize-ship"));

                double drift = 0.0;
                if (human) {
                    g.driftTimer -= dt;
                    if (g.driftTimer <= 0.0) {
                        g.driftTarget = (rand01() * 2.0 - 1.0) * S<double>("humanize-drift");
                        g.driftTimer = 0.4 + rand01() * 0.9;
                    }
                    g.drift += (g.driftTarget - g.drift) * std::min(1.0, static_cast<double>(dt) * 3.0);
                    drift = g.drift;
                } else {
                    g.drift = g.driftTarget = 0.0;
                }

                bool fixedCps = g.autoShip || S<std::string>("mode") == "Fixed";
                double cps = fixedCps ? S<double>("fixed-cps") : g.cps;
                cps = std::clamp(cps + drift, 2.0, std::max(2.0, S<double>("max-cps")));
                double period = 1000.0 / cps;
                double duty = std::clamp<int64_t>(S<int64_t>("duty-cycle"), 10, 90) / 100.0;
                if (ship) duty = shipDuty(this, dt);
                double downDur = period * duty;
                double upDur = period - downDur;

                if (t - g.phaseStart > period * 2.0) g.phaseStart = t;

                double dur = (g.gameDown ? downDur : upDur) * g.jit;
                if (t - g.phaseStart >= dur) {
                    g.phaseStart += dur;
                    g.jit = phaseJitter(human);
                    if (g.gameDown) {
                        sendInput(this, false);
                    } else {
                        bool miss = human && !ship &&
                                    rand01() < S<double>("humanize-miss") / 100.0;
                        if (!miss) sendInput(this, true);
                    }
                }
                updateLabel();
            }
        }
        GJBaseGameLayer::update(dt);
    }
};

class $modify(SpamAssistPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        g.label = WeakRef<CCLabelBMFont>();
        resetState();
        PlayLayer::setupHasCompleted();
        resetState();

        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("indicator"_spr);
        label->setScale(0.35f);
        label->setOpacity(170);
        label->setAnchorPoint({0.f, 1.f});
        auto win = CCDirector::get()->getWinSize();
        label->setPosition({6.f, win.height - 6.f});
        label->setVisible(false);
        label->setZOrder(100);
        this->m_uiLayer->addChild(label);
        g.label = label;
    }

    void resetLevel() {
        resetState();
        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        resetState();
        PlayLayer::destroyPlayer(player, obj);
    }

    void pauseGame(bool p0) {
        resetState();
        PlayLayer::pauseGame(p0);
    }

    void onQuit() {
        resetState();
        g.label = WeakRef<CCLabelBMFont>();
        PlayLayer::onQuit();
    }
};

static void onTriggerKey(bool down) {
    if (!S<bool>("enabled") || !keyMode()) return;

    auto pl = PlayLayer::get();
    auto scene = CCScene::get();
    bool paused = scene && scene->getChildByType<PauseLayer>(0);
    if (!pl || paused) return;

    auto layer = static_cast<GJBaseGameLayer*>(pl);
    double t = nowMs();

    if (down) {
        noteDown(t);
        if (!g.active && shouldEngage(layer)) {
            g.active = true;
            g.phaseStart = t;
            g.jit = 1.0;
            g.hasTarget = false;
            if (!g.gameDown) sendInput(layer, true);
            updateLabel();
        }
    } else {
        noteUp(layer, t);
    }
}

$on_game(Loaded) {
    listenForKeybindSettingPresses("trigger-keybinds",
        [](Keybind const& keybind, bool down, bool repeat, double timestamp) {
            if (repeat) return;
            onTriggerKey(down);
        }
    );
}
