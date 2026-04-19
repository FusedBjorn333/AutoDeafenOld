#ifdef GEODE_IS_MACOS
#include <ApplicationServices/ApplicationServices.h>
#endif


#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>

#include <stdio.h>
#include <locale>
#include <codecvt>
#ifdef GEODE_IS_WINDOWS
    #include <windows.h>
    #include <WinUser.h>

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/GManager.hpp>
#include <Geode/cocos/cocoa/CCObject.h>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include <cocos2d.h>
#include <Geode/ui/TextInput.hpp>

bool isKeyDown(uint32_t key) {
#ifdef GEODE_IS_WINDOWS
    return GetKeyState(key) < 0;
#elif defined(GEODE_IS_MACOS)
    return CGEventSourceKeyState(
        kCGEventSourceStateHIDSystemState,
        (CGKeyCode)key
    );
#endif
}

using namespace geode::prelude;

struct AutoDeafenLevel {
	bool enabled = false;
	short levelType; // 0 = Normal, 1 = Local/Editor, 2 = Daily/Weekly, 3 = gauntlet
	int id = 0;
	short percentage = 50;
    AutoDeafenLevel(bool a, short b, int c, short d) : enabled(a), levelType(b), id(c), percentage(d) {}
    AutoDeafenLevel() = default;
};

AutoDeafenLevel currentlyLoadedLevel;
std::vector<AutoDeafenLevel> loadedAutoDeafenLevels;

bool hasDeafenedThisAttempt = false;
bool hasDied = false;
std::vector<uint32_t> deafenKeybind = {};

short getLevelType(GJGameLevel* level) {
	if (level -> m_levelType != GJLevelType::Saved) return 1;
	if (level -> m_dailyID > 0) return 2;
	if (level -> m_gauntletLevel) return 3;

	return 0;
}

void runEmptyDebugs() {
	log::info("{}", "Running debugs");

	log::info("Deafen Keybind: ", "");
	for (uint32_t key : deafenKeybind) {
		log::info("Key: {}", key);
	}

	log::info("{}", "Loaded levels are:");
	for (AutoDeafenLevel level : loadedAutoDeafenLevels) {
		log::info("Id {} of type {} with enabled {} and percentage {}", level.id, level.levelType, level.enabled, level.percentage);
	}
	log::info("{}", "Currently loaded level is:");
	log::info("Id {} of type {} with enabled {} and percentage {}", currentlyLoadedLevel.id, currentlyLoadedLevel.levelType, currentlyLoadedLevel.enabled, currentlyLoadedLevel.percentage);
}

void saveFile() {
	auto path = Mod::get() -> getSaveDir();
	path /= ".autodeafen";

	log::info("{}", "Saving .autodeafen file to " + path.string());

	std::ofstream file(path);
	if (file.is_open()) {
		file.write("ad1", sizeof("ad1")); // File Header - autodeafen file version 1

		int size = deafenKeybind.size();

		std::string keycodes = "";
		for (const uint32_t key : deafenKeybind)
			keycodes = keycodes + std::to_string(key) + ", ";
		keycodes.pop_back();keycodes.pop_back();

		// log::info("{}{}", "Keys: ", keycodes);
		for (uint32_t key : deafenKeybind)
			file.write(reinterpret_cast<const char*>(&key), sizeof(uint32_t));
		uint32_t filler = 0xFFFFFFFF;
		for (int i = 0; i < (4 - size); i++)
			file.write(reinterpret_cast<const char*>(&filler), sizeof(uint32_t)); // Fillers

		for (AutoDeafenLevel const& a : loadedAutoDeafenLevels) {

			if (a.percentage > 100 || a.percentage < 0 || a.levelType > 3 || a.levelType < 0 || a.id < 0) {
				log::warn("{}{}{}{}{}" "Deleted corrupted autodeafen save entry ", a.id, " with percentage ", a.percentage, " and levelType ", a.levelType);
				continue; // To "uncorrupt" the save, in case it's already corrupted from before I fixed it
			}

			file.write(reinterpret_cast<const char*>(&a.enabled), sizeof(bool));
			file.write(reinterpret_cast<const char*>(&a.levelType), sizeof(short));
			file.write(reinterpret_cast<const char*>(&a.id), sizeof(int));
			file.write(reinterpret_cast<const char*>(&a.percentage), sizeof(short));

			if (!file) {
				log::error("{}", "An error occurred when writing .autodeafen file.");
				break;
			}
		}
		file.close();
		log::debug("Successfully saved .autodeafen file.");

	} else {
		log::error("AutoDeafen file failed when trying to open and save.");
	}
}

void loadFile() {
	auto path = Mod::get() -> getSaveDir();
	path /= ".autodeafen";

	log::info("{}", "Loading .autodeafen file from " + path.string());

	std::ifstream file(path, std::ios::binary);
	if (file.is_open()) {

		char header[4]; // Why on earth is the length of "ad1" 4??? wtf c++
		file.read(header, sizeof("ad1"));

		if (strncmp(header, "ad1", 4) == 0) {
			log::info("Loading autodeafen file version 1.");
			for (int i = 0; i < 4; i++) {
				uint32_t r;
				file.read(reinterpret_cast<char*>(&r), sizeof(uint32_t));
				if (r == 0xFFFFFFFF) continue;
				else deafenKeybind.push_back(r);
			}
			while (file.good()) {
				AutoDeafenLevel level;
				file.read(reinterpret_cast<char*>(&level.enabled), sizeof(bool));
				file.read(reinterpret_cast<char*>(&level.levelType), sizeof(short));
				file.read(reinterpret_cast<char*>(&level.id), sizeof(int));
				file.read(reinterpret_cast<char*>(&level.percentage), sizeof(short));
				loadedAutoDeafenLevels.push_back(level);
				// log::debug("{} {} {} {}", level.id, level.levelType, level.enabled, level.percentage);
			}
		}

		log::info("Successfully loaded .autodeafen file.");

		file.close();

	} else {
		log::warn("AutoDeafen file failed when trying to open and load (probably just doesn't exist). Will create a new one on exit.");
		// deafenKeybind = {0xA1, 0x48};
		// Shift + H is no longer the default, now it just isn't set, because I have had 6 different people dm me asking about it because they didn't know they had to change their keybind.
	}
}


void saveLevel(AutoDeafenLevel lvl) {
	log::info("Saving level {}", lvl.id);

	// Used to be an issue where this wouldn't work. The solution was making it a pointer.
	for (auto& level : loadedAutoDeafenLevels) {
		if (level.id == lvl.id && level.levelType == lvl.levelType) {
			level.percentage = lvl.percentage;
			level.enabled = lvl.enabled;
			// log::info("Found and replaced level {} with percentage {}, enabled {}", lvl.id, lvl.percentage, lvl.enabled);
			// log::info("End replacement is now {}, {}", level.percentage, level.enabled);
			return;
		}
	}

	bool const& enabledByDefault = Mod::get()->getSettingValue<bool>("Enabled by Default");
	short const& defaultPercentage = static_cast<short>(Mod::get()->getSettingValue<int64_t>("Default Percentage") & 0xFFFF);
	// log::info("Saving level {} with enabled: {}, percentage: {}", lvl.id, lvl.enabled, lvl.percentage);
	// log::info("Default values are enabled: {}, percentage: {}", enabledByDefault, defaultPercentage);
	if ( 
		!(lvl.enabled == enabledByDefault && lvl.percentage == defaultPercentage) // Don't bother wasting file size if it's the default already
		&& lvl.percentage <= 100 // This should never happen, so if it does, it shouldn't be on the list!
		&& lvl.levelType <= 3
		&& lvl.id >= 0
	)
	 
		loadedAutoDeafenLevels.push_back(lvl);
	if (Mod::get()->getSettingValue<bool>("Additional Debugging")) { runEmptyDebugs(); }
	// Level saving is now done on exit because it's much faster. Might also make a feature where it starts removing really old levels past a certain limit (like 1000 or something)
}

void sendKeyEvent(uint32_t key, int state) {
#ifdef GEODE_IS_WINDOWS
    INPUT inputs[1];
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.dwFlags = state;
    inputs[0].ki.wScan = 0;
    inputs[0].ki.wVk = key;
    SendInput(1, inputs, sizeof(INPUT));

#elif defined(GEODE_IS_MACOS)
    CGEventRef event = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)key, state == 0);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
#endif
}
void triggerDeafenKeybind() {

	if (deafenKeybind.size() == 0) {
		log::info("Tried to trigger deafen keybind, but keybind wasn't there. Whoops!");
		return;
	}

	if (currentlyLoadedLevel.enabled) {
		log::info("Triggered deafen keybind.");

		const int size = deafenKeybind.size();

		for (int i = 0; i < size - 1; i++) {
			uint32_t key = deafenKeybind[i];
			sendKeyEvent(key, 0);
		}

		// The non-modifier key will always be at the end because of how I coded it.
		uint32_t nm = deafenKeybind[size - 1];
		sendKeyEvent(nm, 0);
		sendKeyEvent(nm, 2);

		for (int i = 0; i < size - 1; i++) {
			uint32_t key = deafenKeybind[i];
			sendKeyEvent(key, 2);
		}

	}
}


class $modify(LoadingLayer) {
	bool init(bool p0) {
		if (!LoadingLayer::init(p0)) return false;
		loadFile();
		return true;
	}
};

class $modify(PlayerObject) {
	void playerDestroyed(bool p0) {
		if (this != nullptr) {
		if (auto playLayer = PlayLayer::get()) {
			if (auto level = playLayer->m_level) {
				if (playLayer->m_player1 != nullptr &&
						this == (playLayer->m_player1) &&
						!(level->isPlatformer())
					) {
						// log::info("{} | {}", playLayer->m_isPracticeMode, Mod::get()->getSettingValue<bool>("Enabled in Practice Mode"));
						if (!playLayer->m_isPracticeMode || (playLayer->m_isPracticeMode && Mod::get()->getSettingValue<bool>("Enabled in Practice Mode")) ) {
							if (hasDeafenedThisAttempt && !hasDied) {
								hasDied = true;
								triggerDeafenKeybind();
							}
						}
					}
				}
			}
		}
		PlayerObject::playerDestroyed(p0);
	}
};

class $modify(GManager) {
	void save() {
		GManager::save();
		saveFile();
	}
};

class $modify(PlayLayer) {
	bool init(GJGameLevel* level, bool p1, bool p2) {
		if (!PlayLayer::init(level, p1, p2)) { return false; }

		int id = m_level -> m_levelID.value();
		short levelType = getLevelType(level);
		if (levelType == 1) id = m_level -> m_M_ID;
		for (AutoDeafenLevel level : loadedAutoDeafenLevels)
			if (level.id == id && level.levelType == levelType) { currentlyLoadedLevel = level; return true; }

		currentlyLoadedLevel = AutoDeafenLevel(Mod::get()->getSettingValue<bool>("Enabled by Default"), levelType, id, static_cast<short>(Mod::get()->getSettingValue<int64_t>("Default Percentage") & 0xFFFF) );
		hasDeafenedThisAttempt = false;

		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		hasDied = false;
		hasDeafenedThisAttempt = false;
	}
	
	void postUpdate(float p0) {
		PlayLayer::postUpdate(p0);
		if (this->m_isPracticeMode && !Mod::get()->getSettingValue<bool>("Enabled in Practice Mode")) { return; }

		int percent = PlayLayer::getCurrentPercentInt();
		// log::info("{}", currentlyLoadedLevel.percentage);
		// log::info("{}", percent);
		if (percent >= currentlyLoadedLevel.percentage && percent != 100 && !hasDeafenedThisAttempt) {
			hasDeafenedThisAttempt = true;
			triggerDeafenKeybind();
		}
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		if (hasDeafenedThisAttempt) {
			hasDeafenedThisAttempt = false;
			triggerDeafenKeybind();
		}
	}

	void onQuit() {
		PlayLayer::onQuit();
		saveLevel(currentlyLoadedLevel);
		currentlyLoadedLevel = AutoDeafenLevel();
		if (hasDeafenedThisAttempt) {
			hasDeafenedThisAttempt = false;
			triggerDeafenKeybind();
		}
	}
};

// Shamelessly copied from https://gist.github.com/radj307/201e82048751713eb522386b46d94955
std::wstring KeyNameFromScanCode(const unsigned scanCode) {
	wchar_t buf[32]{};
	GetKeyNameTextW(scanCode << 16, buf, sizeof(buf));
	return{ buf };
}

std::wstring KeyNameFromVirtualKeyCode(const unsigned virtualKeyCode){
	return KeyNameFromScanCode(MapVirtualKeyW(virtualKeyCode, MAPVK_VK_TO_VSC));
}

bool currentlyInMenu = false;

class EditKeybindLayer : public geode::Popup<std::string const&> {

	public:
		CCLabelBMFont* primaryLabel;
		CCLabelBMFont* secondaryLabel;
		CCLabelBMFont* confirmationLabel;
		bool alreadyUsed = false;
	protected:
		virtual void keyDown(cocos2d::enumKeyCodes key) {
			
			if (alreadyUsed) return;
			if (key + 1 > 0xA0) return;
			log::debug("Key: {}", key + 0);

			std::vector<uint32_t> keys = {}; // Max 4 keys

			for (uint32_t i = 0; i < 6; i++)
				if (isKeyDown(160 + i) && keys.size() < 3) {
					keys.push_back(160 + i);
					log::debug("Mod key {} is pressed", 160 + i);
				}
			keys.push_back(key);

			std::string str = "";
			std::string keycodes = "";

			for (const uint32_t key : keys) {
				str = str + geode::utils::string::wideToUtf8(KeyNameFromVirtualKeyCode(key)) + " + ";
				keycodes = keycodes + std::to_string(key) + ", ";
			}
			str.pop_back();str.pop_back();str.pop_back();
			keycodes.pop_back();keycodes.pop_back();

			// log::debug("{}{}{}{}", "Keys: ", str, " ||  Keycodes: ", keycodes );

			char* strarray = new char[str.length() + 1];
			strcpy(strarray, str.c_str());

			for (uint32_t key : keys) {
				if (key > 0x11E) { // Also doesn't support controller (because discord doesn't either)
					log::info("Unsupported key {}", key);
					secondaryLabel->setString("Geode-Unsupported key in keybind!");
					confirmationLabel->setString("Change your keybind and try again!");
					return;
				}
			}

			secondaryLabel->setString( strarray, true );
			confirmationLabel->setString("Saved Keybind. You may close this menu.");
			deafenKeybind = keys;
			alreadyUsed = true;

		}
		
		bool setup(std::string const& value) override {
			this -> setKeyboardEnabled(true);
			currentlyInMenu = true;

			auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
			CCPoint topMiddle = ccp(m_size.width/2.0f, m_size.height);//winSize/2.f-ccp(m_size.width/2.f,-m_size.height/2.f);

			primaryLabel = CCLabelBMFont::create("Edit Keybind", "goldFont.fnt");
			primaryLabel->setAnchorPoint({0.5, 0.5});
			primaryLabel->setScale(1.0f);
			primaryLabel->setPosition(topMiddle); // 80 = center

			std::string str = "";
			for (const uint32_t key : deafenKeybind)
				str = str + geode::utils::string::wideToUtf8(KeyNameFromVirtualKeyCode(key)) + " + ";

			secondaryLabel = CCLabelBMFont::create("Press your deafen keybind...", "bigFont.fnt"); 
			secondaryLabel->setAnchorPoint({0.5, 0.5});
			secondaryLabel->setScale(0.5f);
			secondaryLabel->setPosition(topMiddle + ccp(0, -100));

			confirmationLabel = CCLabelBMFont::create("", "goldFont.fnt"); 
			confirmationLabel->setAnchorPoint({0.5, 0.5});
			confirmationLabel->setScale(0.5f);
			confirmationLabel->setPosition(topMiddle + ccp(0, -150));

			m_mainLayer -> addChild(primaryLabel);
			m_mainLayer -> addChild(secondaryLabel);
			m_mainLayer -> addChild(confirmationLabel);

			return true;
		}
		void onClose(CCObject* a) override {
			Popup::onClose(a);
			currentlyInMenu = false;
		}
		static EditKeybindLayer* create() {
			auto ret = new EditKeybindLayer();
			if (ret && ret->initAnchored(300, 200, "", "GJ_square02.png")) {
				ret->autorelease();
				return ret;
			}
			CC_SAFE_DELETE(ret);
			return nullptr;
		}
	public:
		static EditKeybindLayer* openMenu() {
			auto layer = create();
			layer -> show();
			return layer;
		}

};

CCMenuItemToggler* enabledButton;
class ButtonLayer : public CCLayer {
	public:
		void toggleEnabled(CCObject* sender) {
			if (deafenKeybind.size() > 0) {
				currentlyLoadedLevel.enabled = !currentlyLoadedLevel.enabled;
				enabledButton -> toggle(!currentlyLoadedLevel.enabled);
			}
		};
};

TextInput* percentageInput;
class ConfigLayer : public geode::Popup<std::string const&> {
	public:
		void editKeybind(CCObject*) {
			this->onClose(nullptr);

			EditKeybindLayer::openMenu();
			currentlyInMenu = true;

			// this -> setTouchEnabled(true);
			// this -> setKeyboardEnabled(true);
			// popup -> setTouchEnabled(true);
			// popup -> setKeyboardEnabled(true); // may as well just enable all of them
		};
		// void runDebugs(CCObject*) {
		// 	log::info("{}", "Running debugs");
		// 	log::info("{}", "Loaded levels are:");
		// 	for (AutoDeafenLevel level : loadedAutoDeafenLevels) {
		// 		log::info("Id {} of type {} with enabled {} and percentage {}", level.id, level.levelType, level.enabled, level.percentage);
		// 	}
		// 	log::info("{}", "Currently loaded level is:");
		// 	log::info("Id {} of type {} with enabled {} and percentage {}", currentlyLoadedLevel.id, currentlyLoadedLevel.levelType, currentlyLoadedLevel.enabled, currentlyLoadedLevel.percentage);
		// }
	protected:
		bool setup(std::string const& value) override {
			this->setKeyboardEnabled(true);
			currentlyInMenu = true;

			bool keybindSet = deafenKeybind.size() > 0;

			auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();

			CCPoint topLeftCorner = ccp(0, m_size.height);//winSize/2.f - ccp(m_size.width/2.f,-m_size.height/2.f);
			CCPoint topMiddle = ccp(m_size.width/2.f, m_size.height);

			auto topLabel = CCLabelBMFont::create("AutoDeafen", "goldFont.fnt"); 
			topLabel->setAnchorPoint({0.5, 0.5});
			topLabel->setScale(1.0f);
			topLabel->setPosition(topLeftCorner + ccp(142, 5));

			CCLabelBMFont* enabledLabel;
			CCLabelBMFont* percentageLabel;
			CCLabelBMFont* keybindLabel;

			if (keybindSet) {
				enabledLabel = CCLabelBMFont::create("Enabled", "bigFont.fnt"); 
				enabledLabel->setAnchorPoint({0, 0.5});
				enabledLabel->setScale(0.7f);
				enabledLabel->setPosition(topLeftCorner + ccp(60, -60)); // 80 = center

				enabledButton = CCMenuItemToggler::create(
					CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
					CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
					this,
					menu_selector(ButtonLayer::toggleEnabled)
				);

				enabledButton -> setPosition(enabledLabel->getPosition() + ccp(140,0));
				enabledButton -> setScale(0.85f);
				enabledButton -> setClickable(true);
				enabledButton -> toggle(currentlyLoadedLevel.enabled);

				percentageInput = TextInput::create(100.f, "%");

				percentageInput -> setFilter("0123456789");
				percentageInput -> setPosition(enabledButton->getPosition() + ccp(0, -40));
				percentageInput -> setScale(0.85f);
				percentageInput -> setMaxCharCount(2);
				percentageInput -> setEnabled(true);
				percentageInput -> setString(std::to_string(currentlyLoadedLevel.percentage));

				percentageLabel = CCLabelBMFont::create("Percent", "bigFont.fnt"); 
				percentageLabel->setAnchorPoint({0, 0.5});
				percentageLabel->setScale(0.7f);
				percentageLabel->setPosition(topLeftCorner + ccp(60, -100));
			} else {
				keybindLabel = CCLabelBMFont::create("To use the mod, press the\nEdit Keybind button, then press\nyour Discord Toggle Deafen Keybind\nset in Discord Settings > Keybinds", "bigFont.fnt");
				// Can't figure out colors :(
				// keybindLabel = CCLabelBMFont::create("To use the mod, press the \n<cg>Edit Keybind</c> button\nand press your \ndiscord <co>Toggle Deafen</c> keybind \nset in \n<cb>Discord Settings</c> > <cp>Keybinds</c>", "chatFont-uhd.fnt"); 
				keybindLabel->setAnchorPoint({0.5, 0});
				keybindLabel->setScale(0.4f);
				keybindLabel->setAlignment(kCCTextAlignmentCenter);
				keybindLabel->setPosition(topMiddle + ccp(0, -100));
			}

			auto editKeybindButton = CCMenuItemSpriteExtra::create(
				ButtonSprite::create("Edit Keybind"),
				this, 
				menu_selector(ConfigLayer::editKeybind)
			);
			editKeybindButton->setAnchorPoint({0.5, 0.5});
			editKeybindButton->setPosition(topLeftCorner + ccp(142, -150));

			// auto debugButton = CCMenuItemSpriteExtra::create(
			// 	ButtonSprite::create("Debug"),
			// 	this, 
			// 	menu_selector(ConfigLayer::runDebugs)
			// );
			// debugButton->setAnchorPoint({0.5, 0.5});
			// debugButton->setPosition(topLeftCorner + ccp(142, -175));

			auto menu = CCMenu::create();
			menu -> setPosition( {0, 0} );
			
			if (keybindSet) {
				menu -> addChild(enabledButton);
				menu -> addChild(percentageInput);
			}
			
			menu -> addChild(editKeybindButton);
			// menu -> addChild(debugButton);
			m_mainLayer -> addChild(topLabel);
			if (keybindSet) {
				m_mainLayer -> addChild(enabledLabel);
				m_mainLayer -> addChild(percentageLabel);
			} else {
				m_mainLayer -> addChild(keybindLabel);
			}

			m_mainLayer -> addChild(menu);

			return true;
		}
		
		void onClose(CCObject* a) override {
			Popup::onClose(a);
			if (percentageInput != nullptr)
				currentlyLoadedLevel.percentage = stoi(percentageInput -> getString());
			currentlyInMenu = false;
		}
		
		static ConfigLayer* create() {
			auto ret = new ConfigLayer();
			if (ret && ret->initAnchored(300, 200, "", "GJ_square02.png")) {
				ret->autorelease();
				return ret;
			}
			CC_SAFE_DELETE(ret);
			return nullptr;
		}
	public:
		void openMenu(CCObject*) {
			auto layer = create();
			layer -> show();
		}
};

class $modify(PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();
		auto winSize = CCDirector::sharedDirector() -> getWinSize();

		CCSprite* sprite = CCSprite::createWithSpriteFrameName("GJ_musicOffBtn_001.png");

		auto btn = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(ConfigLayer::openMenu) );
		auto menu = this -> getChildByID("right-button-menu");

		menu->addChild(btn);
		menu->updateLayout();
	}

	void keyDown(cocos2d::enumKeyCodes p0) {
		if (!currentlyInMenu) { PauseLayer::keyDown(p0); }
	}
	void onResume(CCObject* sender) {
		if (!currentlyInMenu) { PauseLayer::onResume(sender); }
	}
	void onRestart(CCObject* sender) {
		if (!currentlyInMenu) { PauseLayer::onRestart(sender); }
		if (hasDeafenedThisAttempt) { triggerDeafenKeybind(); }
	}
	void onRestartFull(CCObject* sender) {
		if (!currentlyInMenu) { PauseLayer::onRestartFull(sender); }
		if (hasDeafenedThisAttempt) { triggerDeafenKeybind(); }
	}
	void onQuit(CCObject* sender) {
		if (!currentlyInMenu) { PauseLayer::onQuit(sender); }
		if (hasDeafenedThisAttempt) { triggerDeafenKeybind(); }
	}
	void onPracticeMode(CCObject* sender) {
		if (!currentlyInMenu) { PauseLayer::onPracticeMode(sender); }
	}
	void onSettings(CCObject* sender) {
		if (!currentlyInMenu) { PauseLayer::onSettings(sender); }
	}
};
