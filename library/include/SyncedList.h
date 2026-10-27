#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Vanilla SyncedList (adminlist.txt, bannedlist.txt, permittedlist.txt in the save dir):
// one entry per line, lines starting with "//" are comments. Re-read when the file changes.
class SyncedList
{
    std::filesystem::path m_path;
    std::vector<std::string> m_comments;
    std::vector<std::string> m_list;
    std::filesystem::file_time_type m_lastWrite = std::filesystem::file_time_type::min();
    std::chrono::steady_clock::time_point m_lastCheck {};

    void Load();
    void Save();

  public:
    void Open(std::filesystem::path path, std::string_view comment);

    // checks the file at most every 10 s, like vanilla
    std::vector<std::string> const &GetList();
    bool Contains(std::string_view s);
    void Add(std::string const &s);
    void Remove(std::string_view s);

    // vanilla ListContainsId for a Steam host name: "<steamid>" or "Steam_<steamid>"
    bool ContainsId(std::string_view host);
};
