#pragma once

#include <string>
#include <string_view>
#include <vector>

// Remote admin console commands of the dedicated server (ZNet.RPC_RemoteCommand -> Terminal). Cheats are
// off there (devcommands is local only), so only the non-cheat remote commands run: setkey, removekey,
// resetworldkeys, setworldmodifier, setworldpreset. The world modifier ones go through ServerOptionsGUI,
// whose slider/toggle state persists between commands; it is kept here (server_options.json).
class ServerOptions
{
    struct Slider
    {
        std::string m_modifier;
        std::vector<std::pair<std::string, std::vector<std::string>>> m_settings;// option, keys
        int m_value  = 0;
        bool m_manual = false;
    };
    struct Toggle
    {
        std::string m_key;
        bool m_on = false;
    };
    // m_modifiers in GUI order
    std::vector<Slider> m_sliders;
    std::vector<Toggle> m_toggles;
    std::vector<std::pair<std::string, std::vector<std::string>>> m_presets;
    std::string m_preset = "Default";

    static void SetGlobalKey(std::string const &key);
    void SetValue(Slider &s, int value);
    void ReadKeys();
    void SetKeys();
    bool SetPreset(std::string_view preset);
    bool SetModifier(std::string_view modifier, std::string_view option);

  public:
    void Load();
    // runs a remote command line; false when it is not one the dedicated server runs
    bool Run(std::string_view line);
};
