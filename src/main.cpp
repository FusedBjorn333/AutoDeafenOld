#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/GManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include <vector>
#include <fstream>

using namespace geode::prelude;

// ---------------- DATA ----------------

struct AutoDeafenLevel {
    bool enabled = false;
    short levelType = 0;
    int id = 0;
    short percentage = 50;
};

static AutoDeafenLevel currentLevel;
static std::vector<AutoDeafenLevel> savedLevels;

static std::vector<uint32_t> keybind;
static bool hasDeafened = false;
static bool hasDied = false;
static bool inMenu = false;

// ---------------- FILE ----------------

static void saveFile() {
    auto path = Mod::get()->getSaveDir() / ".autodeafen";
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return;

    file.write("ad1", 3);

    for (auto k : keybind)
        file.write((char*)&k, sizeof(uint32_t));

    uint32_t pad = 0xFFFFFFFF;
    for (int i = keybind.size(); i < 4; i++)
        file.write((char*)&pad, sizeof(uint32_t));

    for (auto const& lvl : savedLevels) {
        file.write((char*)&lvl.enabled, sizeof(bool));
        file.write((char*)&lvl.levelType, sizeof(short));
        file.write((char*)&lvl.id, sizeof(int));
        file.write((char*)&lvl.percentage, sizeof(short));
    }
}

static void loadFile() {
    auto path = Mod::get()->getSaveDir() / ".autodeafen";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return;

    char header[3];
    file.read(header, 3);
    if (strncmp(header, "ad1", 3) != 0) return;

    keybind.clear();
    savedLevels.clear();

    for (int i = 0; i < 4; i++) {
        uint32_t k;
        file.read((char*)&k, sizeof(uint32_t));
        if (k != 0xFFFFFFFF)
            keybind.push_back(k);
    }

    while (file.good()) {
        AutoDeafenLevel lvl;
        file.read((char*)&lvl.enabled, sizeof(bool));
        file.read((char*)&lvl.levelType, sizeof(short));
        file.read((char*)&lvl.id, sizeof(int));
        file.read((char*)&lvl.percentage, sizeof(short));

        if (!file) break;
        savedLevels.push_back(lvl);
    }
}

// ---------------- LEVEL TYPE ----------------

static short getLevelType(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_levelType != GJLevelType::Saved) return 1;
    if (level->m_dailyID > 0) return 2;
    if (level->m_gauntletLevel) return 3;
    return 0;
}

// ---------------- KEY TRIGGER ----------------
// NOTE: Geode 5 safe version (no OS input injection here)

static void triggerKeybind() {
    if (!currentLevel.enabled || keybind.empty()) return;

    log::info("AutoDeafen: Triggered keybind");
}

// ---------------- HOOKS ----------------

class $modify(LoadingLayer) {
    bool init(bool p0) {
        if (!LoadingLayer::init(p0)) return false;
        loadFile();
        return true;
    }
};

class $modify(GManager) {
    void save() {
        GManager::save();
        saveFile();
    }
};

class $modify(PlayerObject) {
    void playerDestroyed(bool p0) {
        if (auto playLayer = PlayLayer::get()) {
            if (playLayer->m_player1 == this && !playLayer->m_level->isPlatformer()) {
                if (!playLayer->m_isPracticeMode ||
                    Mod::get()->getSettingValue<bool>("Enabled in Practice Mode")) {

                    if (hasDeafened && !hasDied) {
                        hasDied = true;
                        triggerKeybind();
                    }
                }
            }
        }
        PlayerObject::playerDestroyed(p0);
    }
};

class $modify(PlayLayer) {

    bool init(GJGameLevel* level, bool p1, bool p2) {
        if (!PlayLayer::init(level, p1, p2)) return false;

        int id = level->m_levelID.value();
        short type = getLevelType(level);

        for (auto& l : savedLevels) {
            if (l.id == id && l.levelType == type) {
                currentLevel = l;
                return true;
            }
        }

        currentLevel = {
            Mod::get()->getSettingValue<bool>("Enabled By Default"),
            type,
            id,
            (short)Mod::get()->getSettingValue<int64_t>("Default Percentage")
        };

        hasDeafened = false;
        hasDied = false;

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        hasDied = false;
        hasDeafened = false;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (m_isPracticeMode &&
            !Mod::get()->getSettingValue<bool>("Enabled in Practice Mode"))
            return;

        int percent = PlayLayer::getCurrentPercentInt();

        if (percent >= currentLevel.percentage &&
            percent != 100 &&
            !hasDeafened) {

            hasDeafened = true;
            triggerKeybind();
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (hasDeafened) triggerKeybind();
    }

    void onQuit() {
        PlayLayer::onQuit();
        saveFile();
    }
};

// ---------------- PAUSE BUTTON ----------------

class $modify(PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto sprite = CCSprite::createWithSpriteFrameName("GJ_musicOffBtn_001.png");

        auto btn = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            [](CCObject*) {
                ConfigLayer::openMenu(nullptr);
            }
        );

        auto menu = this->getChildByID("right-button-menu");
        menu->addChild(btn);
        menu->updateLayout();
    }

    void keyDown(cocos2d::enumKeyCodes key) {
        if (!inMenu) PauseLayer::keyDown(key, 0.0);;
    }
};
