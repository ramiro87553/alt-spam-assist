class $modify(SpamAssistPause, PauseLayer) {
    void onOpenSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }

    void customSetup() {
        PauseLayer::customSetup();
    }
};
