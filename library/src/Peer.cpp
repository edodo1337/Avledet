#include "Peer.h"
#include "Avledet.h"
#include "NetManager.h"
#include "ReplayManager.h"
#include "RouteManager.h"
#include "ServerSettings.h"
#include "VUtilsResource.h"
#include "ZDOManager.h"

//#include <magic_enum.hpp> //TODO magic
#include <algorithm>
#include <cstddef>
#include <magic_enum/magic_enum.hpp>
#include <quill/LogMacros.h>
#include <quill/Utility.h>

// Static globals initialized once
//std::string Peer::PASSWORD;
//std::string Peer::SALT;

Peer::Peer(ISocket::Ptr socket) :
    m_lastPing(std::chrono::steady_clock::now()),
    m_socket(std::move(socket))
{
    this->Register("Disconnect", [](Peer::Ptr self) {
        //LOG_INFO(AVL_LOGGER, "RPC_Disconnect");
        self->Disconnect();
    });

    // 1.0: client sends its invite secret key here, unused on a Steam server
    this->Register("ServerHandshake", [](Peer::Ptr rpc, std::string_view inviteSecretKey) {
        rpc->Register("PeerInfo", [](Peer::Ptr rpc, DataReader reader) {
            rpc->m_uid = reader.read<std::int64_t>();
#if AVL_IS_ON(AVL_DISALLOW_MALICIOUS_PLAYERS)
            if (!rpc->m_uid)
                throw std::runtime_error("peer provided 0 owner");
#endif
            auto version = reader.read<std::string_view>();
            LOG_INFO(AVL_LOGGER, "Client {} has version {}", rpc->m_socket->get_host_name(), version);

            // only the network version is checked, like vanilla
            if (reader.read<std::uint32_t>() != VConstants::NETWORK) {
                return rpc->close(ConnectionStatus::ErrorVersion);
            }

            rpc->m_pos = reader.read<Vector3f>();
#if AVL_IS_ON(AVL_DISALLOW_NON_CONFORMING_PLAYERS)
            if (rpc->m_pos.Hsq_magnitude()
                > ZoneManager::WORLD_RADIUS_IN_METERS * ZoneManager::WORLD_RADIUS_IN_METERS)
                throw std::runtime_error("peer position is outside of map");
#endif
            rpc->m_name = reader.read<std::string>();
#if AVL_IS_ON(AVL_DISALLOW_NON_CONFORMING_PLAYERS)
            if (!(rpc->m_name.length() >= 3 && rpc->m_name.length() <= 15))
                throw std::runtime_error("peer provided invalid length name");
#endif
            rpc->m_playfabId = reader.read<std::string>();

            // vanilla IsAllowed: banned id or name, or not on a non-empty permitted list
            auto &&host = rpc->m_socket->get_host_name();
            auto &&avl  = Avledet::instance();
            if (avl.m_bannedList.ContainsId(host) || avl.m_bannedList.Contains(rpc->m_name)
                || (!avl.m_permittedList.GetList().empty() && !avl.m_permittedList.ContainsId(host))) {
                LOG_INFO(AVL_LOGGER, "Player {} : {} is blacklisted or not in whitelist", rpc->m_name, host);
                return rpc->close(ConnectionStatus::ErrorBanned);
            }

            rpc->SetSimDistance(reader);

            auto password = reader.read<std::string_view>();
            reader.read<std::string_view>();// invite secret key, unused

            if (AVL_CONFIG.m_player_auth) {
                auto ticket = reader.read<avledet::util::ByteView>();

                if (auto steamSocket = std::dynamic_pointer_cast<SteamSocket>(rpc->m_socket)) {

                    if (!steamSocket->authenticate(ticket)) {
                        LOG_INFO(AVL_LOGGER, "Client {} has invalid ticket", rpc->m_socket->get_host_name());
                        return rpc->close(ConnectionStatus::ErrorBanned);
                    }
                }
            }

            if (NetManager::instance().GetPeers().size() >= AVL_CONFIG.m_player_limit)
                return rpc->close(ConnectionStatus::ErrorFull);

            if (password != std::string_view(NetManager::instance().m_passwordHash))
                return rpc->close(ConnectionStatus::ErrorPassword);

            // vanilla refuses only the same session id (IsConnected(uid))
            if (NetManager::instance().FindPeerByUserID(rpc->GetUserID()))
                return rpc->close(ConnectionStatus::ErrorAlreadyConnected);

            NetManager::instance().OnPeerConnect(rpc);

            return false;
        });

        bool hasPassword = !AVL_CONFIG.m_server_password.empty();

        rpc->Invoke(avledet::util::hashes::Rpc::S2C_Handshake, hasPassword,
                    std::string_view(NetManager::instance().m_passwordSalt));

        return false;
    });

    LOG_INFO(AVL_LOGGER, "{} has connected", m_socket->get_host_name());
}

void Peer::SetSimDistance(DataReader &reader)
{
    SimulationDistance desired;
    desired.m_near    = reader.read<std::int32_t>();
    desired.m_far     = reader.read<std::int32_t>();
    desired.m_classic = reader.read<bool>();

    // Avledet sends square zone areas, which match the original distance
    m_simDistance = desired.min(SimulationDistance::ORIGINAL);

    avledet::util::Writer writer;
    writer.write(m_simDistance.m_near);
    writer.write(m_simDistance.m_far);
    writer.write(m_simDistance.m_classic);
    Invoke("RPC_ValidatedSimulationDistance", writer.release());
}

void Peer::update()
{
    ZoneScoped;

    auto now(std::chrono::steady_clock::now());

    if (m_closeAt && now - *m_closeAt > std::chrono::seconds(1)) {
        Disconnect();
        return;
    }

    // Send packet data
    //m_socket->Update();

    // Read packets
    while (auto opt = this->Recv()) {
        auto &&bytes = opt.value();
        DataReader reader(bytes);

        auto hash = reader.read<avledet::util::Hash>();
        if (hash == 0) [[unlikely]] {
            if (reader.read<bool>()) {
                // Reply to the server with a pong
                DataWriter writer;
                writer.write((avledet::util::Hash) 0);
                writer.write(false);
                this->Send(writer.release());
            } else {
                m_lastPing = now;
            }
        } else [[likely]] {
            auto max_size = std::min(reader.size() - reader.get_pos(), (std::size_t)32);
            // cap printing
            if (max_size > 32) {
                LOG_TRACE_L2(AVL_LOGGER, "{}, {}, {} ... [{}]", m_socket->get_host_name(), hash, 
                                    quill::utility::to_hex(reader.data() + reader.get_pos(), max_size), reader.size());
            } else {
                LOG_TRACE_L2(AVL_LOGGER, "{}, {}, {}", m_socket->get_host_name(), hash, 
                    quill::utility::to_hex(reader.data() + reader.get_pos(), max_size));
            }

            LOG_BACKTRACE(AVL_LOGGER, "{} rpc {} {}", m_name.empty() ? m_socket->get_host_name() : m_name,
                          method_name(hash), hash);
            // ZRpc.Update: a short package drops the peer (incompatible version), other errors are logged
            try {
                InternalInvoke(hash, reader);
            } catch (avledet::util::EndOfStream const &) {
                throw;
            } catch (std::exception const &e) {
                LOG_ERROR(AVL_LOGGER, "Exception in rpc {} from {}: {}", method_name(hash), m_name, e.what());
            }
        }
        
        if (AVL_CONFIG.m_replay_mode == ReplayMode::CAPTURE) {
            //assert(false); // TODO
            avledet::replay::ReplayManager::instance().on_packet(shared_from_this(), std::move(bytes));
        }
    }

    if (AVL_CONFIG.m_player_timeout > 0s && now - m_lastPing > AVL_CONFIG.m_player_timeout) [[unlikely]] {
        LOG_INFO(AVL_LOGGER, "{} has timed out", this->m_socket->get_host_name());
        Disconnect();
    }
}

void Peer::InternalInvoke(avledet::util::Hash hash, DataReader &reader)
{
    this->internal_invoke(shared_from_this(), hash, reader);
}

bool Peer::close(ConnectionStatus status)
{
    LOG_INFO(AVL_LOGGER, "{} rejected: {}", m_socket->get_host_name(), magic_enum::enum_name(status));
    // the client reads the error and leaves; closed a second later like a kick, so it is not lost
    Invoke(avledet::util::hashes::Rpc::S2C_Error, status);
    if (!m_closeAt)
        m_closeAt = std::chrono::steady_clock::now();
    return false;
}

bool Peer::IsAdmin() const
{
    return Avledet::instance().m_adminList.ContainsId(m_socket->get_host_name());
}

bool Peer::IsMapVisible() const
{
    return m_pack.get<VISIBLE_PACK_INDEX>();
}

bool Peer::IsGated() const
{
    return m_pack.get<GATED_PACK_INDEX>();
}

void Peer::SetAdmin(bool enable)
{
    auto &&list = Avledet::instance().m_adminList;
    if (enable)
        list.Add(m_socket->get_host_name());
    else
        list.Remove(m_socket->get_host_name());
}

void Peer::SetMapVisible(bool enable)
{
    m_pack.set<VISIBLE_PACK_INDEX>(enable);
}

void Peer::SetGated(bool enable)
{
    m_pack.set<GATED_PACK_INDEX>(enable);
}

ZDO::optional Peer::find_zdo()
{
    return ZdoManager::instance().find_zdo(m_characterID);
}

void Peer::Teleport(Vector3f pos, Quaternion rot, bool animation)
{
    this->Route(avledet::util::hashes::Routed::S2C_RequestTeleport, pos, rot, animation);
}

void Peer::RouteParams(avledet::util::UserID const &sender, ZDOID targetZDO, avledet::util::Hash hash,
                       avledet::util::Bytes params)
{
    DataWriter writer;

    writer.write(avledet::util::hashes::Rpc::RoutedRPC);
    {
        avledet::util::WriterScopedEncap scoped(writer);

        RouteManager::instance().prepare_packet(writer, sender, this->GetUserID(), targetZDO, hash);

        writer.write(params);
    }

    this->Send(writer.release());

    //this->Invoke(avledet::util::hashes::Rpc::RoutedRPC,
    //RouteManager::instance().Serialize(sender, this->GetUserID(), targetZDO, hash, std::move(params)));
}

void Peer::RouteParams(ZDOID targetZDO, avledet::util::Hash hash, avledet::util::Bytes params)
{
    this->RouteParams(AVL_ID, targetZDO, hash, std::move(params));
}

void Peer::ZDOSectorInvalidated(ZDO::reference zdo)
{
    if (zdo->is_owner(this->GetUserID()))
        return;

    if (!ZoneManager::instance().ZonesOverlap(zdo->get_zone(), m_pos)) {
        if (m_zdos.erase(zdo->get_id())) {
            m_invalidSector.insert(zdo->get_id());
        }
    }
}

bool Peer::IsOutdatedZDO(ZDO::reference zdo, decltype(m_zdos)::iterator &outItr)
{
    auto &&find = m_zdos.find(zdo->get_id());

    outItr = find;

    return find == m_zdos.end() || zdo->get_owner_rev() > find->second.first.get_owner_rev()
           || zdo->get_data_rev() > find->second.first.get_data_rev();
}