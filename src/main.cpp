#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include "BlockSets.hpp"

using namespace geode::prelude;

namespace {
    blocksets::ObjectInfo objectInfo(int id) {
        blocksets::ObjectInfo info;
        auto obj = GameObject::createWithKey(id);
        if (!obj) return info;

        switch (obj->m_objectType) {
            case GameObjectType::Solid: info.kind = blocksets::Kind::Block; break;
            case GameObjectType::Slope: info.kind = blocksets::Kind::Slope; break;
            case GameObjectType::Decoration: info.kind = blocksets::Kind::Decoration; break;
            default: info.kind = blocksets::Kind::Other; break;
        }
        auto size = obj->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            info.width = size.width;
            info.height = size.height;
        }
        return info;
    }

    blocksets::Options optionsFromSettings() {
        auto mod = Mod::get();
        blocksets::Options options;
        options.minRepeats = static_cast<int>(mod->getSettingValue<int64_t>("min-repeats"));
        options.maxSets = static_cast<size_t>(mod->getSettingValue<int64_t>("max-sets"));
        options.attachDistance = static_cast<float>(mod->getSettingValue<double>("attach-distance"));
        options.includeSlopes = mod->getSettingValue<bool>("include-slopes");
        options.stripGroups = mod->getSettingValue<bool>("strip-groups");
        return options;
    }

    // Saved levels are gzipped + base64 encoded ("H4sI..."), the editor's string isn't
    std::string decompressIfNeeded(std::string const& data) {
        if (data.starts_with("H4sI")) {
            return ZipUtils::decompressString(data, false, 0);
        }
        return data;
    }

    // Scans a level and offers to save what was found. `editor` is set when scanning from the editor,
    // so its Custom Objects tab can be refreshed afterwards.
    void scanLevel(std::string const& rawLevelString, std::string const& levelName, Ref<EditorUI> editor) {
        auto levelString = decompressIfNeeded(rawLevelString);
        if (levelString.empty()) {
            FLAlertLayer::create("Auto Object Skins", "This level has no data to scan.", "OK")->show();
            return;
        }

        auto options = optionsFromSettings();
        auto result = blocksets::scan(levelString, objectInfo, options);

        if (result.sets.empty()) {
            FLAlertLayer::create(
                "Auto Object Skins",
                result.decoratedBases == 0
                    ? std::string("No <cr>decorated blocks</c> were found in this level.")
                    : fmt::format(
                        "Found <cy>{}</c> decorated blocks, but none of their designs repeat "
                        "<cy>{}</c>+ times. Try lowering <cg>Minimum Repeats</c> in the mod's settings.",
                        result.decoratedBases, options.minRepeats
                    ),
                "OK"
            )->show();
            return;
        }

        size_t objectCount = 0;
        std::vector<std::string> customObjects;
        for (auto const& set : result.sets) {
            objectCount += set.objects.size();
            customObjects.push_back(blocksets::customObjectString(result, set, options.stripGroups));
        }

        createQuickPopup(
            "Auto Object Skins",
            fmt::format(
                "Found <cg>{}</c> block sets ({} objects) in <cy>{}</c>.\n"
                "Add them to your <cl>Custom Objects</c>?",
                customObjects.size(), objectCount, levelName
            ),
            "Cancel", "Add",
            [customObjects = std::move(customObjects), editor](FLAlertLayer*, bool add) {
                if (!add) return;
                auto gm = GameManager::get();
                for (auto const& str : customObjects) {
                    gm->addNewCustomObject(str);
                }
                if (editor) {
                    editor->reloadCustomItems();
                }
                Notification::create(
                    fmt::format("Added {} custom objects", customObjects.size()),
                    NotificationIcon::Success
                )->show();
            }
        );
    }

    CCMenuItemSpriteExtra* createScanButton(CCObject* target, SEL_MenuHandler callback) {
        auto spr = CircleButtonSprite::createWithSpriteFrameName(
            "square_01_001.png", .9f, CircleBaseColor::Green, CircleBaseSize::Small
        );
        auto btn = CCMenuItemSpriteExtra::create(spr, target, callback);
        btn->setID("scan-button"_spr);
        return btn;
    }

    // Adds the button to an existing menu (from Node IDs) or to a new menu at a fallback position
    void addScanButton(CCNode* parent, CCMenuItemSpriteExtra* btn, char const* menuID, CCPoint fallback) {
        if (auto menu = parent->getChildByID(menuID)) {
            menu->addChild(btn);
            menu->updateLayout();
            return;
        }
        auto ownMenu = CCMenu::create();
        ownMenu->setID("scan-menu"_spr);
        ownMenu->setPosition(fallback);
        ownMenu->addChild(btn);
        parent->addChild(ownMenu);
    }

    bool canScan(GJGameLevel* level) {
        if (level->m_levelType == GJLevelType::Editor) return true;
        // 1 means "free to copy"
        if (level->m_password.value() == 1) return true;
        return level->m_userID.value() == GameManager::get()->m_playerUserID.value();
    }
}

class $modify(SkinsEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto winSize = CCDirector::get()->getWinSize();
        addScanButton(
            this, createScanButton(this, menu_selector(SkinsEditorUI::onScanLevel)),
            "settings-menu", { winSize.width - 25.f, winSize.height - 105.f }
        );
        return true;
    }

    void onScanLevel(CCObject*) {
        auto name = m_editorLayer->m_level ? std::string(m_editorLayer->m_level->m_levelName) : "this level";
        scanLevel(m_editorLayer->getLevelString(), name, this);
    }
};

class $modify(SkinsLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        addScanButton(
            this, createScanButton(this, menu_selector(SkinsLevelInfoLayer::onScanLevel)),
            "left-side-menu", { 30.f, 120.f }
        );
        return true;
    }

    void onScanLevel(CCObject*) {
        if (!canScan(m_level)) {
            FLAlertLayer::create(
                "Auto Object Skins",
                "This level isn't <cg>free to copy</c>, so its deco can't be scanned.",
                "OK"
            )->show();
            return;
        }
        if (std::string(m_level->m_levelString).empty()) {
            FLAlertLayer::create(
                "Auto Object Skins",
                "This level hasn't been <cy>downloaded</c> yet. Wait for it to finish loading and try again.",
                "OK"
            )->show();
            return;
        }
        scanLevel(m_level->m_levelString, m_level->m_levelName, nullptr);
    }
};

class $modify(SkinsEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        addScanButton(
            this, createScanButton(this, menu_selector(SkinsEditLevelLayer::onScanLevel)),
            "level-actions-menu", { 30.f, 120.f }
        );
        return true;
    }

    void onScanLevel(CCObject*) {
        scanLevel(m_level->m_levelString, m_level->m_levelName, nullptr);
    }
};
