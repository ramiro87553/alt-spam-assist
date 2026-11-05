#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

class $modify(SpamAssistPause, PauseLayer) {
    void onOpenSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }

    void onToggleShip(CCObject*) {
        bool now = !Mod::get()->getSettingValue<bool>("ship-auto");
        Mod::get()->setSettingValue<bool>("ship-auto", now);
        Notification::create(
            now ? "Ship straight: ON" : "Ship straight: OFF",
            now ? NotificationIcon::Success : NotificationIcon::Error,
            1.f
        )->show();
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto logo = createModLogo(Mod::get());
        logo->setScale(0.8f);

        auto logoButton = CCMenuItemSpriteExtra::create(
            logo, this, menu_selector(SpamAssistPause::onOpenSettings)
        );

        auto shipSprite = ButtonSprite::create("Ship");
        shipSprite->setScale(0.5f);

        auto shipButton = CCMenuItemSpriteExtra::create(
            shipSprite, this, menu_selector(SpamAssistPause::onToggleShip)
        );
        shipButton->setPosition({-55.f, 0.f});

        auto menu = CCMenu::create();
        menu->addChild(logoButton);
        menu->addChild(shipButton);

        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 40.f, 40.f});

        this->addChild(menu);
    }
};
