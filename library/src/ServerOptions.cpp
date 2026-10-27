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
#include "VUtilsString.h"

// List.ContainsString(s, InvariantCultureIgnoreCase)
static bool Contains(std::vector<std::string> const &list, std::string_view s)
{
    auto lower = avledet::lexicon::to_lower(s);
    return std::ranges::any_of(list, [&](std::string const &k) { return avledet::lexicon::to_lower(k) == lower; });
}

// Enum.TryParse(s, ignoreCase): a name or a number, the name back or empty
static std::string ParseEnum(std::vector<std::string_view> const &names, std::string_view s)
{
    int n = 0;
    if (!s.empty() && std::from_chars(s.data(), s.data() + s.size(), n).ptr == s.data() + s.size())
        return n >= 0 && n < (int) names.size() ? std::string(names[n]) : std::string();
    for (auto name : names)
        if (avledet::lexicon::to_lower(name) == avledet::lexicon::to_lower(s))
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

// enum GlobalKeys in order (the first 41 are the server options)
static std::vector<std::string_view> const GLOBAL_KEYS = {
        "PlayerDamage", "EnemyDamage", "WorldLevel", "EventRate", "ResourceRate", "StaminaRate", "AdrenalineRate",
        "EitrRate", "DurabilityRate", "FoodRate", "MoveStaminaRate", "StaminaRegenRate", "SkillGainRate",
        "SkillReductionRate", "EnemySpeedSize", "EnemyLevelUpRate", "CarryWeightRate", "PlayerEvents", "Fire",
        "DeathKeepEquip", "DeathDeleteItems", "DeathDeleteUnequipped", "DeathSkillsReset", "DeathKeepInventory",
        "NoBuildCost", "NoCraftCost", "AllPiecesUnlocked", "NoWorkbench", "AllRecipesUnlocked",
        "WorldLevelLockedTools", "PassiveMobs", "NoMap", "NoPortals", "NoBossPortals", "DungeonBuild",
        "TeleportAll", "NoPseudoDrops", "NoBuildingFall", "NoHeavySnow", "AllHeavySnow", "Preset",
        "NonServerOption", "defeated_eikthyr", "defeated_dragon", "defeated_goblinking", "defeated_gdking",
        "defeated_bonemass", "activeBosses", "StoneCircle", "KilledTroll", "killed_surtling", "KilledBat",
        "AshlandsOcean", "Count"};

// Enum.TryParse(ignoreCase) plus the ToString check: the enum number, -1 when it is not a name
static int EnumIndex(std::vector<std::string_view> const &names, std::string_view s)
{
    auto lower = avledet::lexicon::to_lower(s);
    for (std::size_t i = 0; i < names.size(); i++)
        if (avledet::lexicon::to_lower(names[i]) == lower)
            return (int) i;
    return -1;
}

std::string ServerOptions::EscapeTag(std::string_view s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\' || c == ',' || c == '=')
            out += '\\';
        out += c;
    }
    return out;
}

std::optional<std::string> ServerOptions::ModifiersTag(std::vector<std::string> const &keys)
{
    // a Dictionary: a repeated key keeps its place and takes the new value
    std::vector<std::pair<std::string, std::optional<std::string>>> kvp;
    for (auto &&key : keys) {
        auto space = key.find(' ');
        auto name  = key.substr(0, space);
        std::optional<std::string> value;
        if (space != std::string::npos)
            value = key.substr(space + 1);
        int index = EnumIndex(GLOBAL_KEYS, name);
        if (index < 0)
            return std::nullopt;
        if (GLOBAL_KEYS[index] == "Preset") {
            if (!value)
                return std::nullopt;
            // each part: Modifier_Option as numbers, or a WorldPresets number
            std::string compact;
            bool first = true;
            for (auto &&range : std::views::split(*value, ':')) {
                std::string_view part(range.begin(), range.end());
                if (!first)
                    compact += ':';
                first = false;
                // IndexOf('_', start) looks past the part: an "_" of a later part makes vanilla throw
                auto under = value->find('_', part.data() - value->data());
                if (under != std::string::npos && under >= (std::size_t) (part.data() - value->data()) + part.size())
                    return std::nullopt;
                under = part.find('_');
                if (under == std::string_view::npos) {
                    int preset = EnumIndex(WORLD_PRESETS, part);
                    if (preset < 0)
                        return std::nullopt;
                    compact += std::to_string(preset);
                } else {
                    int modifier = EnumIndex(WORLD_MODIFIERS, part.substr(0, under));
                    int option   = EnumIndex(WORLD_MODIFIER_OPTIONS, part.substr(under + 1));
                    if (modifier < 0 || option < 0)
                        return std::nullopt;
                    compact += std::to_string(modifier) + "_" + std::to_string(option);
                }
            }
            value = compact;
        }
        auto id = std::to_string(index);
        auto it = std::ranges::find_if(kvp, [&](auto &&p) { return p.first == id; });
        if (it != kvp.end())
            it->second = value;
        else
            kvp.emplace_back(id, value);
    }
    std::string tag;
    for (auto &&[k, v] : kvp) {
        if (!tag.empty())
            tag += ',';
        tag += EscapeTag(k);
        if (v)
            tag += "=" + EscapeTag(*v);
    }
    return tag;
}

std::string ServerOptions::SteamTags(std::vector<std::string> const &startingKeys)
{
    auto tags = "g=" + EscapeTag(VConstants::GAME) + ",n=" + std::to_string(VConstants::NETWORK);
    if (auto m = ModifiersTag(startingKeys))
        tags += ",m=" + EscapeTag(*m);
    return tags;
}

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
        t.m_on = std::ranges::find(keys, avledet::lexicon::to_lower(t.m_key)) != keys.end();
}

// SetKeys: every slider sets the keys of its option, toggles that are on their key, then the preset key
void ServerOptions::SetKeys()
{
    for (auto &&s : m_sliders)
        for (auto &&key : s.m_settings[s.m_value].second)
            SetGlobalKey(avledet::lexicon::to_lower(key));
    for (auto &&t : m_toggles)
        if (t.m_on)
            SetGlobalKey(avledet::lexicon::to_lower(t.m_key));

    std::string text;
    if (std::ranges::any_of(m_sliders, [](Slider const &s) { return s.m_manual; })) {
        for (auto &&s : m_sliders) {
            if (!text.empty())
                text += ":";
            text += avledet::lexicon::to_lower(s.m_modifier) + "_" + avledet::lexicon::to_lower(s.m_settings[s.m_value].first);
        }
    } else if (m_preset != "Default" && m_preset != "Custom")
        text = avledet::lexicon::to_lower(m_preset);
    if (!text.empty())
        SetGlobalKey("preset " + text);
}

// SetPreset(WorldPresets): the button sets its keys, then OnPresetButton matches the sliders to them
bool ServerOptions::SetPreset(std::string_view preset)
{
    auto it = std::ranges::find_if(m_presets, [&](auto &&p) { return avledet::lexicon::to_lower(p.first) == avledet::lexicon::to_lower(preset); });
    if (it == m_presets.end())
        return false;
    auto &&keys = it->second;
    for (auto &&key : keys)
        SetGlobalKey(avledet::lexicon::to_lower(key));
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
        if (avledet::lexicon::to_lower(s.m_modifier) != avledet::lexicon::to_lower(modifier))
            continue;
        for (int i = 0; i < (int) s.m_settings.size(); i++) {
            if (avledet::lexicon::to_lower(s.m_settings[i].first) != avledet::lexicon::to_lower(option))
                continue;
            SetValue(s, i);
            for (auto &&key : s.m_settings[i].second)
                SetGlobalKey(avledet::lexicon::to_lower(key));
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
    auto name = avledet::lexicon::to_lower(args[0]);
    auto rest = args.size() >= 2 ? std::string(line.substr(args[0].size() + 1)) : std::string();
    auto &&zones = ZoneManager::instance();

    // Terminal: a cheat command without devcommands only prints a hint
    static constexpr std::string_view CHEATS[] {"sleep", "skiptime", "resetkeys", "listkeys", "players", "confirmcheats"};
    if (!m_cheat && std::ranges::find(CHEATS, name) != std::end(CHEATS)) {
        LOG_INFO(AVL_LOGGER, "$achievements_confirm_cheat");
        return true;
    }

    if (name == "devcommands") {
        m_cheat = !m_cheat;
        LOG_INFO(AVL_LOGGER, "Dev commands: {}", m_cheat);
    } else if (name == "sleep") {
        Avledet::instance().SkipToMorning();
    } else if (name == "skiptime") {
        // TryParameterFloat(1, 240f)
        float seconds = 240;
        if (args.size() >= 2) {
            float parsed;
            auto &&a = args[1];
            // float.TryParse takes the whole text: "100abc" is no number
            auto [ptr, ec] = std::from_chars(a.data(), a.data() + a.size(), parsed);
            if (ec == std::errc {} && ptr == a.data() + a.size())
                seconds = parsed;
        }
        Avledet::instance().SkipTime(seconds);
        LOG_INFO(AVL_LOGGER, "Skipping {:.0f}s , Day:{}", seconds,
                 (int) (Avledet::instance().GetWorldTime() / Avledet::WORLD_TIME_LENGTH));
    } else if (name == "resetkeys") {
        zones.GlobalKeysReset();
        LOG_INFO(AVL_LOGGER, "Global and player keys cleared");
    } else if (name == "listkeys") {
        LOG_INFO(AVL_LOGGER, "Keys: {}", zones.m_globalKeys.size());
        for (auto &&key : zones.m_globalKeys)
            LOG_INFO(AVL_LOGGER, "  {}", key);
    } else if (name == "players" || name == "confirmcheats") {
        // Game.SetForcePlayerDifficulty and a text: nothing a client gets from the dedicated server
        LOG_INFO(AVL_LOGGER, "{}", line);
    } else if (name == "setkey") {
        // the "not a world modifier key" check passes: the dedicated server counts as cheated
        if (args.size() >= 2)
            SetGlobalKey(rest);
    } else if (name == "removekey") {
        if (args.size() >= 2 && zones.GlobalKeyRemove(rest))
            zones.SendGlobalKeys();
    } else if (name == "resetworldkeys") {
        // ZoneSystem.ResetWorldKeys: the 41 GlobalKeys before NonServerOption, one RemoveGlobalKey RPC each
        for (auto key : ZoneManager::SERVER_OPTION_KEYS)
            if (zones.GlobalKeyRemove(key))
                zones.SendGlobalKeys();
    } else if (name == "genloc") {
        if (args.size() >= 2 && avledet::lexicon::to_lower(args[1]) == "alt")
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
