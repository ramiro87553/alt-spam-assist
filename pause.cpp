#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

class $modify(SpamAssistPause, PauseLayer) {
    void onOpenSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto logo = createModLogo(Mod::get());
        logo->setScale(0.8f);

        auto button = CCMenuItemSpriteExtra::create(
            logo, this, menu_selector(SpamAssistPause::onOpenSettings)
        );

        auto menu = CCMenu::create();
        menu->addChild(button);

        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 40.f, 40.f});

        this->addChild(menu);
    }
};
