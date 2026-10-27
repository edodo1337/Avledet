#include "SyncedList.h"

#include <algorithm>
#include <fstream>

void SyncedList::Open(std::filesystem::path path, std::string_view comment)
{
    m_path      = std::move(path);
    m_lastWrite = std::filesystem::file_time_type::min();// file_time_type{} is year 2174 in libstdc++
    m_comments.clear();
    m_list.clear();
    std::error_code ec;
    if (std::filesystem::exists(m_path, ec)) {
        Load();
        return;
    }
    m_comments.push_back("// " + std::string(comment));
    Save();
}

void SyncedList::Load()
{
    std::error_code ec;
    auto time = std::filesystem::last_write_time(m_path, ec);
    if (ec || time <= m_lastWrite)
        return;
    m_lastWrite = time;
    m_comments.clear();
    m_list.clear();
    std::ifstream in(m_path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        (line.starts_with("//") ? m_comments : m_list).push_back(line);
    }
}

void SyncedList::Save()
{
    std::filesystem::create_directories(m_path.parent_path());
    {
        std::ofstream out(m_path, std::ios::trunc);
        for (auto &&line : m_comments)
            out << line << '\n';
        for (auto &&line : m_list)
            out << line << '\n';
    }
    std::error_code ec;
    m_lastWrite = std::filesystem::last_write_time(m_path, ec);
}

std::vector<std::string> const &SyncedList::GetList()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastCheck > std::chrono::seconds(10)) {
        Load();
        m_lastCheck = now;
    }
    return m_list;
}

bool SyncedList::Contains(std::string_view s)
{
    auto &&list = GetList();
    return std::find(list.begin(), list.end(), s) != list.end();
}

void SyncedList::Add(std::string const &s)
{
    Load();
    if (std::find(m_list.begin(), m_list.end(), s) == m_list.end()) {
        m_list.push_back(s);
        Save();
    }
}

void SyncedList::Remove(std::string_view s)
{
    Load();
    auto it = std::find(m_list.begin(), m_list.end(), s);
    if (it != m_list.end()) {
        m_list.erase(it);
        Save();
    }
}

bool SyncedList::ContainsId(std::string_view host)
{
    return Contains(host) || Contains("Steam_" + std::string(host));
}
