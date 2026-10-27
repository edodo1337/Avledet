#include <bit>
#include <functional>
#include <optional>
//#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Avledet.h"
#include "NetManager.h"
#include "Prefab.h"
#include "PrefabManager.h"
#include "Types.h"
#include "UnityTransform.h"
#include "VUtilsResource.h"
#include "ZDO.h"
#include "ZDOID.h"
#include "ZDOManager.h"
#include "ZoneManager.h"

/*
    Rev define
*/

ZDO::Rev::Rev() {}

ZDO::Rev::Rev(std::uint32_t dataRev, std::uint16_t ownerRev)
{
    this->set_data_rev(dataRev);
    this->set_owner_rev(ownerRev);
}

std::uint32_t ZDO::Rev::get_data_rev() const
{
    return m_pack.get<DATA_REVISION_PACK_INDEX>();
}

std::uint16_t ZDO::Rev::get_owner_rev() const
{
    return m_pack.get<OWNER_REVISION_PACK_INDEX>();
}

void ZDO::Rev::set_data_rev(std::uint32_t dataRev)
{
    m_pack.set<DATA_REVISION_PACK_INDEX>(dataRev);
}

void ZDO::Rev::set_owner_rev(std::uint16_t ownerRev)
{
    m_pack.set<OWNER_REVISION_PACK_INDEX>(ownerRev);
}

void ZDO::Rev::rev_data()
{
    this->set_data_rev(this->get_data_rev() + 1);
}

void ZDO::Rev::rev_owner()
{
    this->set_owner_rev(this->get_owner_rev() + 1);
}

/*
    init modernizers
*/

static auto const strippable_nodes = ankerl::unordered_dense::set<avledet::util::Hash>(
        {avledet::util::get_stable_hash("generated"), avledet::util::get_stable_hash("patrolSpawnPoint"),
         avledet::util::get_stable_hash("autoDespawn"), avledet::util::get_stable_hash("targetHear"),
         avledet::util::get_stable_hash("targetSee"), avledet::util::get_stable_hash("burnt0"),
         avledet::util::get_stable_hash("burnt1"), avledet::util::get_stable_hash("burnt2"),
         avledet::util::get_stable_hash("burnt3"), avledet::util::get_stable_hash("burnt4"),
         avledet::util::get_stable_hash("burnt5"), avledet::util::get_stable_hash("burnt6"),
         avledet::util::get_stable_hash("burnt7"), avledet::util::get_stable_hash("burnt8"),
         avledet::util::get_stable_hash("burnt9"), avledet::util::get_stable_hash("burnt10"),
         avledet::util::get_stable_hash("LookDir"), avledet::util::get_stable_hash("RideSpeed")});

static auto const strippable_long_nodes = ankerl::unordered_dense::set<avledet::util::Hash>({
        avledet::util::get_stable_hash("user_u"),
        avledet::util::get_stable_hash("user_i"),
        avledet::util::get_stable_hash("RodOwner_u"),
        avledet::util::get_stable_hash("RodOwner_i"),
        avledet::util::get_stable_hash("CatchID_u"),
        avledet::util::get_stable_hash("CatchID_i"),
});

bool ZDO::_can_strip(avledet::util::Hash key)
{
    return strippable_nodes.contains(key);
}

bool ZDO::_can_strip(avledet::util::Hash key, float data)
{
    return strippable_nodes.contains(key)
           || (key == avledet::util::get_stable_hash("scaleScalar") && avledet::util::CSU::equal(data, 1.f));
}

bool ZDO::_can_strip(avledet::util::Hash key, avledet::util::CSU::Quaternion const &data)
{
    return data == avledet::util::CSU::Quaternion::IDENTITY || _can_strip(key);
}

bool ZDO::_can_strip(avledet::util::Hash key, std::int32_t data)
{
    return data == 0 || _can_strip(key);
}

bool ZDO::_can_strip(avledet::util::Hash key, std::int64_t data)
{
    return data == 0 || _can_strip(key) || strippable_long_nodes.contains(key);
}

bool ZDO::_can_strip(avledet::util::Hash key, std::string const &data)
{
    return data.empty() || _can_strip(key);
}

bool ZDO::_can_strip(avledet::util::Hash key, std::vector<char> const &data)
{
    return data.empty() || _can_strip(key);
}

// (Keep as a member function, to access m_id as needed in future)
bool ZDO::_try_convert(avledet::util::Hash key, avledet::util::CSU::Vector3f data)
{
    if (this->_can_strip(key)) {
        return true;
    }
    if (key == avledet::util::get_stable_hash("SpawnPoint")) {
        //ZDOExtraData.Set(zdoid, ZDOVars.s_spawnPoint, data);
        _set(m_id, avledet::util::get_stable_hash("spawnpoint"), data);
        return true;
    }

    // if x == y == z
    if (avledet::util::CSU::equal(data.x, data.y) && avledet::util::CSU::equal(data.y, data.z)) {
        if (key == avledet::util::get_stable_hash("scale")) {
            // 1 is unit scale
            if (avledet::util::CSU::equal(data.x, 1.f)) {
                return true;
            }
            this->_set(m_id, avledet::util::get_stable_hash("scale"), data.x);
            //ZDOExtraData.Set(zid, ZDOVars.s_scaleScalarHash, data.x);
            return true;
        }
        // 0 is default ZDO when no mapping exists
        else if (avledet::util::CSU::equal(data.x, 0.f)) {
            return true;
        }
    }
    return false;
}

std::uint32_t ZDO::_read_num_items(avledet::util::Reader &reader, int version)
{
    // version 0 is the network path, which uses varint counts
    if (version != 0 && version < 33) {
        return (std::uint32_t) reader.read<std::uint8_t>();
    }
    auto num = (std::uint32_t) reader.read<std::uint8_t>();
    if ((num & 128) != 0) {
        num = ((num & 127) << 8) | (std::uint32_t) reader.read<std::uint8_t>();
    }
    return num;
};

void ZDO::_write_num_items(DataWriter &writer, int numItems)
{
    if (numItems < 128) {
        writer.write((std::uint8_t) numItems);
        return;
    }
    writer.write((std::uint8_t)((numItems >> 8) | 128));
    writer.write((std::uint8_t) numItems);
}

/*
    ZDO define
*/

void ZDO::_revise()
{
    this->m_rev.rev_data();
}

void ZDO::_set_prefab_hash(avledet::util::Hash hash)
{
    this->m_prefab_index = PrefabManager::instance().get_prefab_index(hash);
}

// Set the owner of the ZDO without revising
void ZDO::_set_owner(avledet::util::UserID owner)
{
    if (owner) {
        ZDO_OWNERS[m_id] = owner;
    } else {
        ZDO_OWNERS.erase(m_id);
    }
}

void ZDO::_set_position(Vector3f const &pos)
{
    this->m_pos = pos;
}

void ZDO::_set_rotation(Vector3f const &rot)
{
    this->m_rotation = rot;
}

void ZDO::_set_rotation(Quaternion const &rot)
{
    this->_set_rotation(rot.euler_angles());
}

bool ZDO::_set_connection(ZDOConnector::Type type, ZDOID zdoid)
{
    auto &&insert = PAIRED_CONNECTORS.try_emplace(m_id, type, zdoid);

    auto &&connector = insert.first->second;

    // if it was not newly inserted
    //  check the old values
    if (!insert.second) {
        auto &&type2  = connector.m_type;
        auto &&zdoid2 = connector.m_target;

        if (type == type2 && zdoid2 == zdoid) {
            return false;
        }
    }

    connector.m_type   = type;
    connector.m_target = zdoid;

    //m_pack.Merge<FLAGS_PACK_INDEX>(std::to_underlying(LocalFlag::Member_Connection));

    return true;
}

void ZDO::set_connection(ZDOConnector::Type type, ZDOID zdoid)
{
    if (this->_set_connection(type, zdoid)) {
        this->_revise();
    }
}

ZDOID ZDO::get_connection_zdoid(ZDOConnector::Type type) const
{
    auto &&find = PAIRED_CONNECTORS.find(m_id);
    if (find != PAIRED_CONNECTORS.end()) {
        if (find->second.m_type == type)
            return find->second.m_target;
    }
    return ZDOID::NONE;
}

ZDOID ZDO::get_id() const
{
    return this->m_id;
}

Vector3f ZDO::get_position() const
{
    return this->m_pos;
}

//ZDO::Rev &ZDO::_get_revision()
//{
//    return this->m_rev;
//}

Quaternion ZDO::get_rotation() const
{
    return Quaternion::euler(this->m_rotation);
}

void ZDO::set_rotation(Quaternion rot)
{
    // the server sets it on objects it instantiates, where ZNetView.Awake saves transform.rotation:
    // the rotation setter normalized it the Unity way
    auto &&euler = avledet::unity::Normalize(rot).euler_angles();
    if (euler != this->m_rotation) {
        this->m_rotation = euler;
        this->_revise();
    }
}

Prefab::Reference ZDO::get_prefab() const
{
    return PrefabManager::instance().get_indexed_prefab(m_prefab_index);
}

avledet::util::Hash ZDO::get_prefab_hash() const
{
    return this->get_prefab().get().m_hash;
}

// vanilla ZNetView.SyncScale: always the Vector3 "scale" (the old code put a Vector3 under
// "scaleScalar", which clients read as a float, so they never saw the scale)
void ZDO::set_local_scale(Vector3f scale, bool allowIdentity)
{
    if (allowIdentity || !(scale.x == 1 && scale.y == 1 && scale.z == 1))
        this->set(avledet::util::hashes::ZDO::ZNetView::SCALE, scale);
}

avledet::util::UserID ZDO::get_owner() const
{
    // TODO optimize by checking owner bit
    auto &&find = ZDO_OWNERS.find(this->m_id);
    if (find != ZDO_OWNERS.end()) {
        return find->second;
    }
    return 0;
}

bool ZDO::is_owner(avledet::util::UserID owner) const
{
    return owner == this->get_owner();
}

bool ZDO::owned_by_me() const
{
    return this->is_owner(AVL_ID);
}

bool ZDO::has_owner() const
{
    return this->get_owner() != 0;
}

bool ZDO::claim()
{
    return this->set_owner(AVL_ID);
}

void ZDO::set_claimed(bool local)
{
    if (local) {
        this->claim();
    } else {
        this->disown();
    }
}

void ZDO::disown()
{
    this->set_owner(0);
}

bool ZDO::set_owner(avledet::util::UserID owner)
{
    // only if the owner has changed, then revise it
    if (this->get_owner() != owner) {
        this->_set_owner(owner);

        this->m_rev.rev_owner();
        return true;
    }
    return false;
}

std::uint16_t ZDO::get_owner_rev() const
{
    return this->m_rev.get_owner_rev();
}

std::uint32_t ZDO::get_data_rev() const
{
    return this->m_rev.get_data_rev();
}

bool ZDO::is_persistent() const
{
    return this->get_prefab().get().is_persistent();
}

bool ZDO::is_distant() const
{
    return this->get_prefab().get().is_distant();
}

avledet::util::ObjectType ZDO::get_type() const
{
    return this->get_prefab().get().GetObjectType();
}

#if AVL_IS_ON(AVL_LEGACY_WORLD_LOADING)
void ZDO::Load31Pre(DataReader &pkg, std::int32_t worldVersion)
{
    (void) pkg;
    (void) worldVersion;
    assert(false);//TODO
    /*
    pkg.read<std::uint32_t>();       // owner rev
    pkg.read<std::uint32_t>();       // data rev
    pkg.read<bool>();           // persistent

    pkg.read<std::int64_t>();        // owner
    auto timeCreated = pkg.read<std::int64_t>();
    pkg.read<std::int32_t>();        // pgw

    if (worldVersion >= 16 && worldVersion < 24)
        pkg.read<std::int32_t>();

    if (worldVersion >= 23)
        pkg.read<std::uint8_t>();    // type

    if (worldVersion >= 22) {
        pkg.read<bool>();       // distant
    }

    if (worldVersion < 13) {
        pkg.read<char16_t>();
        pkg.read<char16_t>();
    }

    const Prefab* prefab = nullptr;

    avledet::util::Hash prefabHash{};

    if (worldVersion >= 17) {
        prefabHash = pkg.read<avledet::util::Hash>();
        prefab = &PrefabManager::instance().RequirePrefabByHash(prefabHash);
        _SetPrefabHash(prefabHash);
    }

    pkg.read<Vector2i>(); // m_sector
    this->_set_position(pkg.read<Vector3f>());
    this->_SetRotation(pkg.read<Quaternion>());

    // will get or create an empty default
    auto&& members = ZDO_MEMBERS[get_id()];

    _TryReadType<float,         char16_t>(pkg, members);
    _TryReadType<Vector3f,      char16_t>(pkg, members);
    _TryReadType<Quaternion,    char16_t>(pkg, members);
    _TryReadType<std::int32_t,       char16_t>(pkg, members);
    _TryReadType<std::int64_t,       char16_t>(pkg, members);
    _TryReadType<std::string,   char16_t>(pkg, members);
    
    if (worldVersion >= 27)
        _TryReadType<avledet::util::Bytes,   char16_t>(pkg, members);

    if (worldVersion < 17) {
        prefabHash = get_int(avledet::util::hashes::ZDO::ZDO::PREFAB);
        prefab = &PrefabManager::instance().RequirePrefabByHash(prefabHash);
        _SetPrefabHash(prefabHash);
    }

    assert(prefab);

    if (worldVersion < 31) {
        // Convert owners
        {
            auto&& zdoid = get_zdoid("user");
            if (zdoid) {
                claim();
                set(avledet::util::hashes::ZDO::USER, zdoid.GetOwner());
            }
        }

        {
            auto&& zdoid = get_zdoid("RodOwner");
            if (zdoid) {
                claim();
                set(avledet::util::hashes::ZDO::FishingFloat::ROD_OWNER, zdoid.GetOwner());
            }
        }

        if (prefab->AnyFlagsPresent(Prefab::Flag::TERRAIN_MODIFIER)
            || (get_prefab_hash() == avledet::util::hashes::Object::ship_construction))
        {
            set(avledet::util::hashes::ZDO::TerrainModifier::TIME_CREATED, timeCreated);
        }

        // Convert seeds
        member_map copy = members;
        for (auto&& pair : copy) {
            if (xhash_to_hash<std::int32_t>(pair.first) == avledet::util::hashes::ZDO::VisEquipment::ITEM_LEFT) {
                // assign an arbitrary random seed based off its get_id()
                set("seed", 
                    static_cast<std::int32_t>(ankerl::unordered_dense::hash<ZDOID>{}(get_id())));
            }
        }
    }*/
}
#endif//AVL_LEGACY_WORLD_LOADING

void ZDO::unpack(DataReader &reader, std::int32_t version)
{
    auto flags = reader.read<std::uint16_t>();

    if (version) {
        auto zone = reader.read<avledet::util::CSU::Vector2s>();// sector

        this->_set_position(reader.read<avledet::util::CSU::Vector3f>());
        if (zone != get_zone())                                 // a mismatch indicates a 99% of corruption
            throw std::runtime_error("sector mismatch");
    }

    auto prefab_hash = reader.read<std::int32_t>();
    if (m_prefab_index == Prefab::NONE) {// Init once
        this->_set_prefab_hash(prefab_hash);
    } else {
        // prefab index is set once for world loads,
        //assert(version == 0);
    }

    //if ((num & 4096) > 0)
    if (flags & (1 << NETWORK_Rotation)) {
        this->_set_rotation(reader.read<avledet::util::CSU::Vector3f>());
    }

    //ZDOConnector::Type type = ZDOConnector::Type::None;
    if (flags & (1 << NETWORK_Connection)) {
        auto type = reader.read<ZDOConnector::Type>();
        if (version) {// disk
            auto hash = reader.read<avledet::util::Hash>();
            //manager->s_connectionsHashData[m_uid] = std::make_pair(reader.read<ConnectionType>(), reader.read<avledet::util::Hash>());

            auto &&connector = TYPED_CONNECTORS[get_id()];// = ZDOConnector{ .m_type = type, .m_hash = hash };
            connector.m_type = type;
            connector.m_hash = hash;
        } else {                                          // network
            auto target      = reader.read<ZDOID>();
            auto &&connector = PAIRED_CONNECTORS[get_id()];
            // set connection
            connector.m_type   = type;
            connector.m_target = target;
            //type &= ~ZDOConnector::Type::Target;
        }
        //m_pack.Merge<FLAGS_PACK_INDEX>(std::to_underlying(LocalFlag::Member_Connection));
    } else {
        // Remove connector flag
        //m_pack.Unset<FLAGS_PACK_INDEX>(std::to_underlying(LocalFlag::Member_Connection));
        //m_pack.Set<FLAGS_PACK_INDEX>(
        //m_pack.Get<FLAGS_PACK_INDEX>() & (~std::to_underlying(LocalFlag::Member_Connection)));
    }

    if (flags
        & ((1 << NETWORK_Float) | (1 << NETWORK_Vec3) | (1 << NETWORK_Quat) | (1 << NETWORK_Int)
           | (1 << NETWORK_Long) | (1 << NETWORK_String | (1 << NETWORK_ByteArray)))) {
        if (flags & (1 << NETWORK_Float))
            this->_load_vars(reader, version, m_floats);
        if (flags & (1 << NETWORK_Vec3))
            this->_load_vars(reader, version, m_vec3);
        if (flags & (1 << NETWORK_Quat))
            this->_load_vars(reader, version, m_quats);
        if (flags & (1 << NETWORK_Int))
            this->_load_vars(reader, version, m_ints);
        if (flags & (1 << NETWORK_Long))
            this->_load_vars(reader, version, m_longs);
        if (flags & (1 << NETWORK_String))
            this->_load_vars(reader, version, m_strings);
        if (flags & (1 << NETWORK_ByteArray))
            this->_load_vars(reader, version, m_byteArrays);
    }
}

namespace {
    // ZDOVars.s_sessionHashes: never saved to disk
    auto const SESSION_KEYS = [] {
        avledet::util::Set<avledet::util::Hash> keys;
        for (auto name : {"CatchID_u", "CatchID_i", "alert", "animation_speed", "body_avel", "body_vel",
                          "BodyVelocity", "haveTarget", "InUse", "landed", "LookTarget", "noise", "support",
                          "tiltrot", "vel", "velRel"})
            keys.insert(avledet::util::get_stable_hash(name));
        return keys;
    }();

    // Utils.SmallPosition: integer x/z with y == +0 fit in two shorts, far ones get clamped to 20000
    std::optional<Vector2s> SmallPosition(Vector3f v)
    {
        if (std::bit_cast<std::uint32_t>(v.y) != 0)
            return std::nullopt;
        // C# (short) cast of a float: through int32, then the low 16 bits
        Vector2s s((std::int16_t) (std::int32_t) v.x, (std::int16_t) (std::int32_t) v.z);
        bool xOk = (float) s.x == v.x;
        bool zOk = (float) s.y == v.z;
        if (xOk) {
            if (zOk)
                return s;
            if (v.z < -20000.f || v.z > 20000.f) {
                s.y = v.z < 0 ? -20000 : 20000;
                return s;
            }
        }
        if (zOk && (v.x < -20000.f || v.x > 20000.f)) {
            s.x = v.x < 0 ? -20000 : 20000;
            return s;
        }
        return std::nullopt;
    }

    // ZPackage.WriteSmallRotation: half degree steps
    void WriteSmallRotation(DataWriter &writer, Vector3f v)
    {
        auto x = (std::uint32_t) (std::int64_t) (v.x * 2.f);
        auto y = (std::uint32_t) (std::int64_t) (v.y * 2.f);
        auto z = (std::uint32_t) (std::int64_t) (v.z * 2.f);
        if ((x <= 1 || x >= 719) && (z <= 1 || z >= 719)) {
            writer.write((std::uint16_t) (y | 0x8000));
            return;
        }
        std::uint32_t packed = x | (y << 10) | (z << 20);
        writer.write((std::uint16_t) (packed >> 16));
        writer.write((std::uint16_t) packed);
    }

    Vector3f ReadSmallRotation(DataReader &reader)
    {
        std::uint32_t a = reader.read<std::uint16_t>();
        if (a & 0x8000)
            return Vector3f(0, (float) (a & 0x7FFF) * .5f, 0);
        a = (a << 16) | reader.read<std::uint16_t>();
        return Vector3f((float) (a & 0x3FF), (float) ((a >> 10) & 0x3FF), (float) ((a >> 20) & 0x3FF)) * .5f;
    }

    // Utils.CloseToZero
    bool CloseToZero(Vector3f v)
    {
        return (int) std::abs(v.x * 2.f) < 1 && (int) std::abs(v.y * 2.f) < 1 && (int) std::abs(v.z * 2.f) < 1;
    }
}// namespace

void ZDO::save(DataWriter &writer, ZDOConnectorData const *connection) const
{
    // ExtraDataFlags
    enum : std::uint16_t
    {
        Connections   = 0x01,
        Persistent    = 0x100,
        Distant       = 0x200,
        Rotation      = 0x1000,
        SmallPos      = 0x2000,
    };

    bool hasRot = !CloseToZero(m_rotation);
    auto small  = SmallPosition(m_pos);

    std::uint16_t flags = 0;
    if (connection && connection->m_type != ZDOConnector::Type::None)
        flags |= Connections;
    if (is_persistent())
        flags |= Persistent;
    if (is_distant())
        flags |= Distant;
    flags |= std::to_underlying(get_type()) << 10;
    if (hasRot)
        flags |= Rotation;
    if (small)
        flags |= SmallPos;

    auto const flagPos = writer.get_pos();
    writer.write(flags);
    if (small)
        writer.write(*small);
    else
        writer.write(m_pos);
    writer.write(get_prefab_hash());
    if (hasRot)
        WriteSmallRotation(writer, m_rotation);
    if (flags & Connections) {
        writer.write(connection->m_type);
        writer.write(connection->m_hash);
    }

    // typed lists in the network order, flag bits 1..7
    int bit         = 1;
    auto writeVars = [&]<typename T>(VarMap<T> const &) {
        bit <<= 1;
        auto &&tree = _find_var_tree<T>(m_id);
        if (!tree)
            return;
        int count = 0;
        for (auto &&pair : *tree)
            count += !SESSION_KEYS.contains(pair.first);
        if (!count)
            return;
        flags |= bit;
        _write_num_items(writer, count);
        for (auto &&[key, value] : *tree) {
            if (!SESSION_KEYS.contains(key)) {
                writer.write(key);
                writer.write(value);
            }
        }
    };
    writeVars(m_floats);
    writeVars(m_vec3);
    writeVars(m_quats);
    writeVars(m_ints);
    writeVars(m_longs);
    writeVars(m_strings);
    writeVars(m_byteArrays);

    auto const endPos = writer.get_pos();
    writer.set_pos(flagPos);
    writer.write(flags);
    writer.set_pos(endPos);
}

bool ZDO::load(DataReader &reader)
{
    auto flags = reader.read<std::uint16_t>();
    if (flags & 0x2000) {
        auto small = reader.read<Vector2s>();
        _set_position(Vector3f(small.x, 0, small.y));
    } else {
        _set_position(reader.read<Vector3f>());
    }
    auto prefab = reader.read<avledet::util::Hash>();
    bool known  = PrefabManager::instance().find_prefab(prefab);
    if (known)
        _set_prefab_hash(prefab);
    if (flags & 0x1000)
        _set_rotation(ReadSmallRotation(reader));
    if (flags & 0x01) {
        auto &&connector = TYPED_CONNECTORS[m_id];
        connector.m_type = reader.read<ZDOConnector::Type>();
        connector.m_hash = reader.read<avledet::util::Hash>();
    }
    // version 0: no old-key conversions, varint counts as on disk since version 33
    if (flags & 0x02)
        _load_vars(reader, 0, m_floats);
    if (flags & 0x04)
        _load_vars(reader, 0, m_vec3);
    if (flags & 0x08)
        _load_vars(reader, 0, m_quats);
    if (flags & 0x10)
        _load_vars(reader, 0, m_ints);
    if (flags & 0x20)
        _load_vars(reader, 0, m_longs);
    if (flags & 0x40)
        _load_vars(reader, 0, m_strings);
    if (flags & 0x80)
        _load_vars(reader, 0, m_byteArrays);
    return known;
}

// ZDO specific-methods

void ZDO::set_position(Vector3f pos)
{
    if (this->get_position() != pos) {
        if (ZoneManager::WorldToZonePos(pos) != get_zone()) {
            ZdoManager::instance()._InvalidateZDOZone(this->smart_from_this());

            ZdoManager::instance()._RemoveFromSector(this->smart_from_this());
            this->_set_position(pos);//unrevised
            ZdoManager::instance()._AddZDOToZone(this->smart_from_this());
        } else {
            this->_set_position(pos);
        }

        assert(ZoneManager::WorldToZonePos(pos) == this->get_zone());

        if (this->owned_by_me())
            this->_revise();
    }
}

ZoneID ZDO::get_zone() const
{
    return ZoneManager::WorldToZonePos(this->get_position());
}

void ZDO::pack(DataWriter &writer, bool network) const
{
    bool hasRot = this->m_rotation != Vector3f::ZERO;

    std::uint16_t flags {};

    if (this->is_persistent())
        flags |= 1 << NETWORK_Persistent;
    if (this->is_distant())
        flags |= 1 << NETWORK_Distant;
    flags |= std::to_underlying(this->get_type()) << NETWORK_Type1;
    if (hasRot)
        flags |= 1 << NETWORK_Rotation;

    auto const flagPos = writer.get_pos();
    writer.write(flags);// dummy spacer
    if (!network) {
        writer.write(this->get_zone());
        writer.write(this->get_position());
    }
    writer.write(this->get_prefab_hash());
    if (hasRot) {
        writer.write(this->m_rotation);
    }

    if (network) {
        auto &&find = PAIRED_CONNECTORS.find(this->m_id);
        if (find != PAIRED_CONNECTORS.end() && find->second.m_type != ZDOConnector::Type::None) {
            auto &&connector = find->second;
            writer.write(connector.m_type);
            writer.write(connector.m_target);

            flags |= 1 << NETWORK_Connection;
        }
    } else {
        auto &&find = TYPED_CONNECTORS.find(this->m_id);
        if (find != TYPED_CONNECTORS.end() && find->second.m_type != ZDOConnector::Type::None) {
            auto &&connector = find->second;
            writer.write(connector.m_type);
            writer.write(connector.m_hash);

            flags |= 1 << NETWORK_Connection;
        }
    }

    if (this->_try_write_type<float>(writer))
        flags |= 1 << NETWORK_Float;
    if (this->_try_write_type<Vector3f>(writer))
        flags |= 1 << NETWORK_Vec3;
    if (this->_try_write_type<Quaternion>(writer))
        flags |= 1 << NETWORK_Quat;
    if (this->_try_write_type<std::int32_t>(writer))
        flags |= 1 << NETWORK_Int;
    if (this->_try_write_type<std::int64_t>(writer))
        flags |= 1 << NETWORK_Long;
    if (this->_try_write_type<std::string>(writer))
        flags |= 1 << NETWORK_String;
    if (this->_try_write_type<avledet::util::Bytes>(writer))
        flags |= 1 << NETWORK_ByteArray;

    auto const endPos = writer.get_pos();
    writer.set_pos(flagPos);
    writer.write(flags);
    writer.set_pos(endPos);
}
