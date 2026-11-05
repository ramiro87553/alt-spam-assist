#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;class $modify(SpamAssistPause, PauseLayer) {
    void onOpenSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }

    void customSetup() {
        PauseLayer::customSetup();
    }
};
