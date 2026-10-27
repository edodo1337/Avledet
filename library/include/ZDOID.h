#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <ankerl/unordered_dense.h>
#include <quill/Backend.h>
#include <quill/bundled/fmt/ostream.h>
#include <quill/bundled/fmt/ranges.h>
#include <quill/Frontend.h>
#include <quill/HelperMacros.h>
#include <quill/Logger.h>
#include <quill/LogMacros.h>
#include <quill/sinks/ConsoleSink.h>

#include "CompileSettings.h"
#include "DataStream.h"

namespace avledet::util {
    // user id and id, as in vanilla (ZDOID: long + uint)
    class ZDOID
    {
        friend struct ankerl::unordered_dense::hash<ZDOID>;

        std::int64_t m_user_id {};
        std::uint32_t m_id {};

      public:
        static const ZDOID NONE;

        ZDOID() = default;
        ZDOID(std::int64_t user_id, std::uint32_t id) : m_user_id(user_id), m_id(id) {}

        friend auto operator<=>(ZDOID const &lhs, ZDOID const &rhs) noexcept = default;

        // Return whether this has a value besides NONE
        operator bool() const noexcept { return *this != ZDOID::NONE; }

        std::int64_t get_user_id() const { return m_user_id; }
        void set_user_id(std::int64_t user_id) { m_user_id = user_id; }

        std::uint32_t get_id() const { return m_id; }
        void set_id(std::uint32_t id) { m_id = id; }
    };

}// namespace avledet::util

using ZDOID = avledet::util::ZDOID;

// Basic ankerl hash
template<>
struct ankerl::unordered_dense::hash<avledet::util::ZDOID>
{
    using is_avalanching = void;// high quality hash

    //static_assert(std::has_unique_object_representations_v<avledet::util::ZDOID>);

    auto operator()(avledet::util::ZDOID const &value) const noexcept -> std::uint64_t
    {
        return ankerl::unordered_dense::detail::wyhash::mix(static_cast<std::uint64_t>(value.m_user_id),
                                                            0x9E3779B97F4A7C15ull ^ value.m_id);
    }
};

template<>
struct avledet::util::Streamer<avledet::util::ZDOID>
{
    void operator()(avledet::util::Writer &writer, avledet::util::ZDOID const &zdoid)
    {
        //assert(false); // TODO
        writer.write(zdoid.get_user_id());//8 is first
        writer.write(zdoid.get_id());     //4 is second
    }

    avledet::util::ZDOID operator()(avledet::util::Reader &reader)
    {
        auto user_id = reader.read<std::int64_t>();
        auto id      = reader.read<std::uint32_t>();
        return avledet::util::ZDOID(user_id, id);
    }
};

std::ostream &operator<<(std::ostream &st, ZDOID const &zdoid);

QUILL_LOGGABLE_DEFERRED_FORMAT(ZDOID)
