#include "ServerOptions.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <ranges>

#include <nlohmann/json.hpp>
#include <quill/LogMacros.h>

#include "Avledet.h"
#include "WorldManager.h"
#include "ZoneManager.h"

static std::string Lower(std::string_view s)
{
    std::string out(s);
    for (auto &&c : out)
        c = (char) std::tolower((unsigned char) c);
    return out;
}

// List.ContainsString(s, InvariantCultureIgnoreCase)
static bool Contains(std::vector<std::string> const &list, std::string_view s)
{
    auto lower = Lower(s);
    return std::ranges::any_of(list, [&](std::string const &k) { return Lower(k) == lower; });
}

// Enum.TryParse(s, ignoreCase): a name or a number, the name back or empty
static std::string ParseEnum(std::vector<std::string_view> const &names, std::string_view s)
{
    int n = 0;
    if (!s.empty() && std::from_chars(s.data(), s.data() + s.size(), n).ptr == s.data() + s.size())
        return n >= 0 && n < (int) names.size() ? std::string(names[n]) : std::string();
    for (auto name : names)
        if (Lower(name) == Lower(s))
            return std::string(name);
    return {};
}

static std::vector<std::string_view> const WORLD_PRESETS
        = {"Default", "Custom", "Normal", "Casual", "Easy", "Hard", "Hardcore", "Immersive", "Hammer"};
static std::vector<std::string_view> const WORLD_MODIFIERS
        = {"Default", "Combat", "DeathPenalty", "Resources", "Raids", "Portals"};
static std::vector<std::string_view> const WORLD_MODIFIER_OPTIONS = {
        "Default", "None", "Less", "MuchLess", "More", "MuchMore", "Casual", "VeryEasy", "Easy", "Hard", "VeryHard",
        "Hardcore", "Most"};

// ZoneSystem.SetGlobalKey from the server: RPC_SetGlobalKey
void ServerOptions::SetGlobalKey(std::string const &key)
{
    auto &&zones = ZoneManager::instance();
    if (!zones.m_globalKeys.contains(key)) {
        zones.GlobalKeyAdd(key);
        zones.SendGlobalKeys();
    }
}

static std::vector<std::string> &StartingKeys()
{
    return WorldManager::instance().GetWorld()->m_startingGlobalKeys;
}

void ServerOptions::Load()
{
    std::ifstream in("server_options.json");
    if (!in)
        throw std::runtime_error("server_options.json missing (dumped from the 1.0 server)");
    auto j = nlohmann::json::parse(in);
    for (auto &&s : j.at("sliders")) {
        Slider slider {s[0], {}};
        for (auto &&o : s[1])
            slider.m_settings.emplace_back(o[0], o[1]);
        // the slider starts at the option without keys
        for (int i = 0; i < (int) slider.m_settings.size(); i++)
            if (slider.m_settings[i].second.empty())
                slider.m_value = i;
        m_sliders.push_back(std::move(slider));
    }
    for (auto &&t : j.at("toggles"))
        m_toggles.push_back({t});
    for (auto &&p : j.at("presets"))
        m_presets.emplace_back(p[0], p[1]);
}

// m_slider.value = v fires OnValueChanged (m_manualSet) only on a change
void ServerOptions::SetValue(Slider &s, int value)
{
    if (s.m_value != value)
        s.m_manual = true;
    s.m_value = value;
}

// ReadKeys: KeySlider/KeyToggle.TryMatch(World)
void ServerOptions::ReadKeys()
{
    auto &&keys = StartingKeys();
    for (auto &&s : m_sliders) {
        for (int i = 0; i < (int) s.m_settings.size(); i++) {
            auto &&option = s.m_settings[i].second;
            if (option.empty()) {
                SetValue(s, i);
                if (keys.empty())
                    break;
                continue;
            }
            if (std::ranges::all_of(option, [&](std::string const &k) { return Contains(keys, k); })) {
                SetValue(s, i);
                break;
            }
        }
    }
    for (auto &&t : m_toggles)
        t.m_on = std::ranges::find(keys, Lower(t.m_key)) != keys.end();
}

// SetKeys: every slider sets the keys of its option, toggles that are on their key, then the preset key
void ServerOptions::SetKeys()
{
    for (auto &&s : m_sliders)
        for (auto &&key : s.m_settings[s.m_value].second)
            SetGlobalKey(Lower(key));
    for (auto &&t : m_toggles)
        if (t.m_on)
            SetGlobalKey(Lower(t.m_key));

    std::string text;
    if (std::ranges::any_of(m_sliders, [](Slider const &s) { return s.m_manual; })) {
        for (auto &&s : m_sliders) {
            if (!text.empty())
                text += ":";
            text += Lower(s.m_modifier) + "_" + Lower(s.m_settings[s.m_value].first);
        }
    } else if (m_preset != "Default" && m_preset != "Custom")
        text = Lower(m_preset);
    if (!text.empty())
        SetGlobalKey("preset " + text);
}

// SetPreset(WorldPresets): the button sets its keys, then OnPresetButton matches the sliders to them
bool ServerOptions::SetPreset(std::string_view preset)
{
    auto it = std::ranges::find_if(m_presets, [&](auto &&p) { return Lower(p.first) == Lower(preset); });
    if (it == m_presets.end())
        return false;
    auto &&keys = it->second;
    for (auto &&key : keys)
        SetGlobalKey(Lower(key));
    for (auto &&s : m_sliders) {
        // TryMatch(keys): the option whose keys are all there, the one with most keys
        int fallback = 0, best = -1, most = 0;
        for (int i = 0; i < (int) s.m_settings.size(); i++) {
            auto &&option = s.m_settings[i].second;
            if (option.empty())
                fallback = i;
            bool missing = option.empty()
                        || std::ranges::any_of(option, [&](std::string const &k) { return !Contains(keys, k); });
            if (!missing && (int) option.size() >= most) {
                best = i;
                most = (int) option.size();
            }
        }
        SetValue(s, best >= 0 ? best : fallback);
        s.m_manual = false;
    }
    for (auto &&t : m_toggles)
        t.m_on = Contains(keys, t.m_key);
    m_preset = it->first;
    return true;
}

bool ServerOptions::SetModifier(std::string_view modifier, std::string_view option)
{
    for (auto &&s : m_sliders) {
        if (Lower(s.m_modifier) != Lower(modifier))
            continue;
        for (int i = 0; i < (int) s.m_settings.size(); i++) {
            if (Lower(s.m_settings[i].first) != Lower(option))
                continue;
            SetValue(s, i);
            for (auto &&key : s.m_settings[i].second)
                SetGlobalKey(Lower(key));
            return true;
        }
    }
    return false;
}

bool ServerOptions::Run(std::string_view line)
{
    std::vector<std::string> args;
    for (auto &&part : std::views::split(line, ' '))
        args.emplace_back(part.begin(), part.end());
    if (args.empty())
        return false;
    auto name = Lower(args[0]);
    auto rest = args.size() >= 2 ? std::string(line.substr(args[0].size() + 1)) : std::string();
    auto &&zones = ZoneManager::instance();

    if (name == "setkey") {
        // the "not a world modifier key" check passes: the dedicated server counts as cheated
        if (args.size() >= 2)
            SetGlobalKey(rest);
    } else if (name == "removekey") {
        if (args.size() >= 2 && zones.GlobalKeyRemove(rest))
            zones.SendGlobalKeys();
    } else if (name == "resetworldkeys") {
        // ZoneSystem.ResetWorldKeys: the 41 GlobalKeys before NonServerOption, one RemoveGlobalKey RPC each
        for (auto key : {"PlayerDamage", "EnemyDamage", "WorldLevel", "EventRate", "ResourceRate", "StaminaRate",
                         "AdrenalineRate", "EitrRate", "DurabilityRate", "FoodRate", "MoveStaminaRate",
                         "StaminaRegenRate", "SkillGainRate", "SkillReductionRate", "EnemySpeedSize",
                         "EnemyLevelUpRate", "CarryWeightRate", "PlayerEvents", "Fire", "DeathKeepEquip",
                         "DeathDeleteItems", "DeathDeleteUnequipped", "DeathSkillsReset", "DeathKeepInventory",
                         "NoBuildCost", "NoCraftCost", "AllPiecesUnlocked", "NoWorkbench", "AllRecipesUnlocked",
                         "WorldLevelLockedTools", "PassiveMobs", "NoMap", "NoPortals", "NoBossPortals", "DungeonBuild",
                         "TeleportAll", "NoPseudoDrops", "NoBuildingFall", "NoHeavySnow", "AllHeavySnow", "Preset"})
            if (zones.GlobalKeyRemove(key))
                zones.SendGlobalKeys();
    } else if (name == "genloc") {
        if (args.size() >= 2 && Lower(args[1]) == "alt")
            LOG_WARNING(AVL_LOGGER, "genloc alt (alt biome regeneration) is not supported");
        else
            zones.RegenerateLocations();
    } else if (name == "setworldpreset") {
        auto preset = args.size() >= 2 ? ParseEnum(WORLD_PRESETS, args[1]) : "";
        if (preset.empty())
            return false;
        Run("resetworldkeys");
        ReadKeys();
        if (!SetPreset(preset))
            LOG_WARNING(AVL_LOGGER, "Missing settings for preset: {}", preset);
        SetKeys();
    } else if (name == "setworldmodifier") {
        auto modifier = args.size() >= 3 ? ParseEnum(WORLD_MODIFIERS, args[1]) : "";
        auto option   = args.size() >= 3 ? ParseEnum(WORLD_MODIFIER_OPTIONS, args[2]) : "";
        if (modifier.empty() || option.empty())
            return false;
        ReadKeys();
        if (!SetModifier(modifier, option))
            LOG_WARNING(AVL_LOGGER, "Missing settings for preset: '{}' to '{}'", modifier, option);
        SetKeys();
    } else
        return false;
    return true;
}
