//wtf am i using openssl for
//#include <openssl/md5.h>
//#include <openssl/rand.h>
#include <isteamgameserver.h>
#include <quill/LogMacros.h>
#include <string>
#include <string_view>
#include <vector>

#include "Avledet.h"
#include "Crypto.h"
#include "DataStream.h"
#include "DiscordManager.h"
#include "Hashes.h"
#include "ModManager.h"
#include "NetAcceptor.h"
#include "NetManager.h"
#include "NetSocket.h"
#include "Peer.h"
#include "ReplayManager.h"
#include "RouteManager.h"
#include "ServerSettings.h"
#include "Types.h"
#include "VUtils.h"
#include "VUtilsRandom.h"
#include "VUtilsResource.h"
#include "WorldManager.h"
#include "ZDOManager.h"
#include "ZoneManager.h"

static void RefuseNotAdmin(Peer::Ptr const &peer)
{
    LOG_WARNING(AVL_LOGGER, "{} is not admin, command refused", peer->m_name);
    peer->ConsoleMessage("You are not admin");
}

// vanilla InternalKick: by host ("Steam_" prefix allowed), then by player name
Peer::Ptr NetManager::Kick(std::string_view user)
{
    if (user.empty())
        return nullptr;
    auto id = user;
    if (id.starts_with("Steam_"))
        id.remove_prefix(6);
    auto &&peer = FindPeerByHost(id);
    if (!peer)
        peer = FindPeerByName(user);
    if (peer)
        peer->Kick();
    return peer;
}

// vanilla Save(saveOtherPlayerProfiles: true): clients save their characters with the world
void NetManager::SaveWorld()
{
    for (auto &&peer : m_connectedPeers)
        peer->Invoke("SavePlayerProfile");
    WorldManager::instance().Save();
}

// vanilla InternalBan: a player online by name is banned by host; kicked by UpdateBanList
Peer::Ptr NetManager::Ban(std::string_view user)
{
    auto &&peer = FindPeerByName(user);
    Avledet::instance().m_bannedList.Add(peer ? peer->m_socket->get_host_name() : std::string(user));
    return peer;
}

bool NetManager::Unban(std::string_view user)
{
    bool had = Avledet::instance().m_bannedList.Contains(user);
    Avledet::instance().m_bannedList.Remove(user);
    return had;
}

// vanilla UpdateBanList, every 5 s
void NetManager::KickBanned()
{
    auto &&avl      = Avledet::instance();
    bool permitOnly = !avl.m_permittedList.GetList().empty();
    for (auto &&peer : m_onlinePeers) {
        auto &&host = peer->m_socket->get_host_name();
        if (permitOnly && !avl.m_permittedList.ContainsId(host))
            peer->Kick();
    }
    for (auto &&user : avl.m_bannedList.GetList())
        Kick(user);
}

void NetManager::SendDisconnect()
{
    LOG_INFO(AVL_LOGGER, "Sending disconnect msg");

    for (auto &&peer : m_connectedPeers) {
        peer->SendDisconnect();
    }
}

// vanilla GetUniqueDisplayName: "name#N" when several known users share a display name
static std::string UniqueDisplayName(std::vector<std::array<std::string, 4>> const &history,
                                     std::vector<std::array<std::string, 4>> const &players,
                                     std::array<std::string, 4> const &user)
{
    bool found   = false;
    int position = 1;
    int matching = 0;
    auto check   = [&](auto &&list) {
        for (auto &&other : list) {
            if (other[1] != user[1])
                continue;
            matching++;
            if (!found) {
                if (other[0] == user[0])
                    found = true;
                else
                    position++;
            }
        }
    };
    check(history);
    if (!found)
        check(players);
    return matching > 1 ? user[1] + "#" + std::to_string(position) : user[1];
}

void NetManager::SendPlayerList()
{
    if (m_onlinePeers.empty())
        return;
    if (!AVL_SCRIPT_EVENT(ScriptManager::Events::PlayerList))
        return;

    // CrossNetworkUserInfo: platform id, display name, server display name, playfab id
    std::vector<std::array<std::string, 4>> users;
    for (auto &&peer : m_onlinePeers) {
        auto &&name = peer->m_syncData.find("platformDisplayName");
        users.push_back({"Steam_" + peer->m_socket->get_host_name(),
                         name != peer->m_syncData.end() ? name->second : "", "", peer->m_playfabId});
    }
    auto &&history = WorldManager::instance().GetWorld()->m_playerHistory;
    for (auto &&user : users)
        user[2] = UniqueDisplayName(history, users, user);
    // UpdatePlayerHistory
    for (auto &&user : users) {
        auto it = std::find_if(history.begin(), history.end(), [&](auto &&h) { return h[0] == user[0]; });
        if (it == history.end()) {
            history.push_back(user);
            m_historyUpdated = true;
        } else if (*it != user) {
            *it              = user;
            m_historyUpdated = true;
        }
    }

    DataWriter writer;
    writer.write(avledet::util::hashes::Rpc::S2C_UpdatePlayerList);// rpc hash
    {
        avledet::util::WriterScopedEncap scoped(writer);
        writer.write((std::uint32_t) m_onlinePeers.size());
        for (std::size_t i = 0; i < m_onlinePeers.size(); i++) {
            auto &&peer = m_onlinePeers[i];
            writer.write(peer->m_name);
            writer.write(peer->m_characterID);
            for (auto &&field : users[i])
                writer.write(field);
            writer.write(peer->IsMapVisible() || AVL_CONFIG.playerListForceVisible);
            if (peer->IsMapVisible() || AVL_CONFIG.playerListForceVisible) {
                if (AVL_CONFIG.playerListSmoothUpdating >= 2s)
                    writer.write(peer->m_pos);
                else {// quickly dynamic map
                    auto &&zdo = peer->find_zdo();
                    writer.write(zdo ? zdo->get_position() : peer->m_pos);
                }
            }
        }
    }
    for (auto &&peer : m_onlinePeers)
        peer->Send(writer.get_buf());
}

void NetManager::SendHistoricalPlayerList()
{
    m_historyUpdated = false;
    auto &&history   = WorldManager::instance().GetWorld()->m_playerHistory;
    DataWriter writer;
    writer.write((std::int32_t) history.size());
    for (auto &&user : history)
        for (auto &&field : user)
            writer.write(field);
    auto pkg = writer.release();
    for (auto &&peer : m_onlinePeers)
        peer->Invoke("HistoricalPlayerList", pkg);
}

void NetManager::SendAdminList()
{
    auto &&admins = Avledet::instance().m_adminList.GetList();
    DataWriter writer;
    writer.write((std::int32_t) admins.size());
    for (auto &&admin : admins)
        writer.write(admin);
    auto pkg = writer.release();
    for (auto &&peer : m_onlinePeers)
        peer->Invoke("AdminList", pkg);
}

void NetManager::SendNetTime()
{
    for (auto &&peer : m_onlinePeers) {
        peer->Invoke(avledet::util::hashes::Rpc::S2C_UpdateTime, Avledet::instance().GetWorldTime());
    }
}

void NetManager::SendPeerInfo(Peer::Ptr peer)
{
    avledet::util::Writer writer;
    {
        //avledet::util::WriterScopedEncap scoped(writer);

        writer.write(Avledet::instance().ID());
        writer.write(std::string_view(VConstants::GAME));
        writer.write(VConstants::NETWORK);
        writer.write(Vector3f(1000000, 0, 1000000));// dedicated server reference position (Game.FixedUpdate)
        writer.write(std::string_view("Stranger"));   // its profile name
        writer.write(std::string_view(""));// playfab id, read and dropped by client
        writer.write(SimulationDistance::ORIGINAL.m_near);
        writer.write(SimulationDistance::ORIGINAL.m_far);
        writer.write(SimulationDistance::ORIGINAL.m_classic);

        auto world = WorldManager::instance().GetWorld();

        writer.write(world->m_name);
        writer.write(world->m_seed);
        writer.write(world->m_seedName);// Peer does not seem to use
        writer.write(world->m_uid);
        writer.write(world->m_worldGenVersion);
        writer.write(Avledet::instance().GetWorldTime());
    }

    peer->Invoke(avledet::util::hashes::Rpc::PeerInfo, writer.release());
}

//void NetManager::OnNewClient(ISocket::Ptr socket, avledet::util::UserID uuid, const std::string &name, const Vector3f &pos) {
void NetManager::OnPeerConnect(Peer::Ptr peer)
{
    if (!AVL_SCRIPT_EVENT(ScriptManager::Events::Join, peer)) {
        return peer->Disconnect();
    }

    AVL_DISPATCH_WEBHOOK(peer->m_name + " has joined");

    // 1.0: client asks again whenever its graphics settings change
    peer->Register("RPC_RequestValidSimulationDistance", [](Peer::Ptr peer, avledet::util::Bytes pkg) {
        DataReader reader(pkg);
        peer->SetSimDistance(reader);
    });

    // Important
    peer->Register("ServerSyncedPlayerData", [](Peer::Ptr peer, avledet::util::Bytes pkg) {
        //DataReader reader(pkg);
        //auto reader = DataReader(std::vector<char>(pkg.begin(), pkg.end()));
        // TODO cannabilize package?
        DataReader reader(pkg);

        peer->m_pos = reader.read<Vector3f>();
        peer->SetMapVisible(reader.read<bool>());

        // TODO create template guide
        //reader.read<avledet::util::Map<std::string_view, std::string_view>>();

        auto count = reader.read<std::int32_t>();
        for (int i = 0; i < count; i++) {
            // Read player event data (only 2):
            //  'possibleEvents'
            //  'baseValue' // used to be a zdo member
            auto key              = reader.read<std::string_view>();// key
            peer->m_syncData[key] = reader.read<std::string>();     // value
        }
    });

    // isnt 'ban' a command?
    //  it should be part of RemoteCommand
    peer->Register("RPC_RemoteCommand",
                   [](Peer::Ptr peer, std::string_view command) {
                       if (!peer->IsAdmin())
                           return RefuseNotAdmin(peer);

                       // TODO run commands or something?
                       //  this is still in beta and subject to change
                       //  although unlikely because this commands gets funneled to
                       //  valheim commands, which have existed for a while.
                       //  The only difference is that some commands are now classified as remote vs local.
                       (void) command;
                   });

    // Important
    peer->Register("CharacterID", [](Peer::Ptr peer, ZDOID characterID) {
        // Peer sends 0,0 on after death

        //TODO the player only sends this:
        //  on server join
        //  and on every after-death thereafter
        //if (peer->m_characterID)
        //AVL_DISPATCH_WEBHOOK(peer->m_name + " has died");

        // ZDOID.None while respawning; the peer keeps its session id (m_uid)
        peer->m_characterID = characterID;

        LOG_NOTICE(AVL_LOGGER, "Got CharacterID from {} ({})", peer->m_name, characterID);
    });

    // admin RPCs answer with vanilla texts (ZNet.RPC_Kick and others)
    peer->Register("Kick", [this](Peer::Ptr peer, std::string_view user) {
        if (!peer->IsAdmin())
            return RefuseNotAdmin(peer);
        LOG_NOTICE(AVL_LOGGER, "Admin {} kicks '{}'", peer->m_name, user);
        peer->ConsoleMessage("Kicking user " + std::string(user));
        Kick(user);
    });

    peer->Register("Ban", [this](Peer::Ptr peer, std::string_view user) {
        if (!peer->IsAdmin())
            return RefuseNotAdmin(peer);
        if (user.empty())
            return;
        auto &&banned = FindPeerByName(user);
        auto host     = banned ? banned->m_socket->get_host_name() : std::string(user);
        LOG_NOTICE(AVL_LOGGER, "Admin {} bans '{}'", peer->m_name, host);
        peer->ConsoleMessage("Banning user " + host);
        Ban(user);
    });

    peer->Register("Unban", [this](Peer::Ptr peer, std::string_view user) {
        if (!peer->IsAdmin())
            return RefuseNotAdmin(peer);
        if (user.empty())
            return;
        LOG_NOTICE(AVL_LOGGER, "Admin {} unbans '{}'", peer->m_name, user);
        peer->ConsoleMessage("Unbanning user " + std::string(user));
        Unban(user);
    });

    peer->Register("Save", [this](Peer::Ptr peer) {
        if (!peer->IsAdmin())
            return RefuseNotAdmin(peer);
        LOG_NOTICE(AVL_LOGGER, "Admin {} saves the world", peer->m_name);
        peer->ConsoleMessage("Saving..");
        SaveWorld();
    });

    peer->Register("PrintBanned", [](Peer::Ptr peer) {
        if (!peer->IsAdmin())
            return RefuseNotAdmin(peer);
        auto &&avl = Avledet::instance();
        peer->ConsoleMessage("Banned users");
        auto &&banned = avl.m_bannedList.GetList();
        if (banned.empty())
            peer->ConsoleMessage("-");
        for (std::size_t i = 0; i < banned.size(); i++)
            peer->ConsoleMessage(std::to_string(i) + ": " + banned[i]);
        peer->ConsoleMessage("");
        peer->ConsoleMessage("Permitted users");
        auto &&permitted = avl.m_permittedList.GetList();
        if (permitted.empty())
            peer->ConsoleMessage("All");
        for (std::size_t i = 0; i < permitted.size(); i++)
            peer->ConsoleMessage(std::to_string(i) + ": " + permitted[i]);
    });

    // vanilla order: PeerInfo, PlayerList, HistoricalPlayerList, AdminList, then ZDOs and routed RPCs
    SendPeerInfo(peer);
    m_onlinePeers.push_back(peer);
    SendPlayerList();
    SendHistoricalPlayerList();
    SendAdminList();

    ZdoManager::instance().OnNewPeer(peer);
    RouteManager::instance().OnNewPeer(peer);
    ZoneManager::instance().OnNewPeer(peer);

#if AVL_IS_ON(AVL_DISCORD_INTEGRATION)
    if (AVL_CONFIG.TEST_discordAccountLinking) {
        auto &&host_name = peer->m_socket->get_host_name();
        peer->SetGated(!DiscordManager::instance().m_linked_accounts.contains(host_name));
        if (peer->IsGated()) {
            DiscordManager::instance().m_temp_linking_keys[host_name]
                    = {VUtils::Random::GenerateAlphaNum(4), Avledet::instance().Nanos()};
        }
    }
#endif

    // TODO remove this for debug only
    peer->SetGated(AVL_CONFIG.m_discord_player_restrict);

    peer->m_joinTime = std::chrono::steady_clock::now();
    LOG_NOTICE(AVL_LOGGER, "{} joined ({}, uid {}), {} online", peer->m_name, peer->m_socket->get_host_name(),
               peer->GetUserID(), m_onlinePeers.size());
}

Peer::Ptr NetManager::FindPeer(std::string_view any)
{
    Peer::Ptr peer = FindPeerByHost(any);
    if (!peer)
        peer = FindPeerByName(any);
    if (!peer)
        peer = FindPeerByUserID(std::atoll(any.data()));
    return peer;
}

// Return the peer or nullptr
Peer::Ptr NetManager::FindPeerByName(std::string_view name)
{
    for (auto &&peer : m_onlinePeers) {
        if (peer->m_name == name)
            return peer;
    }
    return nullptr;
}

// Return the peer or nullptr
Peer::Ptr NetManager::FindPeerByUserID(avledet::util::UserID uuid)
{
    for (auto &&peer : m_onlinePeers) {
        if (peer->GetUserID() == uuid)
            return peer;
    }
    return nullptr;
}

Peer::Ptr NetManager::FindPeerByHost(std::string_view host)
{
    for (auto &&peer : m_onlinePeers) {
        if (peer->m_socket->get_host_name() == host)
            return peer;
    }
    return nullptr;
}

void NetManager::PostInit()
{
    LOG_NOTICE(AVL_LOGGER, "Initializing NetManager");

    //m_acceptor = std::make_unique<AcceptorSteam>();
    //m_acceptor->Listen();
    if (AVL_CONFIG.m_server_tcp) {
        m_acceptor
                = IAcceptor::tcp_dedicated(AVL_CONFIG.m_server_address + ":" + std::to_string(AVL_CONFIG.m_server_port));// m_acceptor
    } else {
        m_acceptor = IAcceptor::steam_dedicated(AVL_CONFIG.m_server_address + ":"
                                                + std::to_string(AVL_CONFIG.m_server_port));      // m_acceptor
    }

    m_acceptor->start();
    m_acceptor->on_connect([this](ISocket::Ptr socket) {
        try {
            auto peer = std::make_shared<Peer>(std::move(socket));
            if (AVL_SCRIPT_EVENT(ScriptManager::Events::Connect, peer)) {
                m_connectedPeers.insert(m_connectedPeers.end(), peer);

                if (AVL_CONFIG.m_replay_mode == ReplayMode::CAPTURE) {
                    // TODO implement the replay mode
                    //assert(false);
                    avledet::replay::ReplayManager::instance().on_new_peer(peer);
                }
            }
        } catch (std::exception const &e) {
            LOG_ERROR(AVL_LOGGER, "Failed to set up a new connection: {}", e.what());
        }
    });
}

void NetManager::Update()
{
    ZoneScoped;

    // Send periodic data (2s)
    if (VUtils::run_periodic<struct periodic_peer_nettime>(2s)) {
        SendNetTime();
    }

    if (AVL_CONFIG.playerListSmoothUpdating > 0s) {
        if (VUtils::run_periodic<struct periodic_peer_tablist>(AVL_CONFIG.playerListSmoothUpdating)) {
            SendPlayerList();
            if (m_historyUpdated)
                SendHistoricalPlayerList();
        }
    }

    if (VUtils::run_periodic<struct periodic_ban_list>(5s))
        KickBanned();

    // Send periodic pings (1s)
    if (VUtils::run_periodic<struct periodic_peer_keepalive>(1s)) {
        DataWriter writer;
        writer.write((avledet::util::Hash) 0);
        writer.write(true);

        for (auto &&peer : m_connectedPeers) {
            peer->Send(writer.get_buf());
        }
    }

    // Update peers
    for (auto &&peer : m_connectedPeers) {
        try {
            if (VUtils::run_periodic<struct log_trace_player_status>(3s)) {
                LOG_TRACE_L1(AVL_LOGGER, "{}, {}, {}ms", peer->m_socket->get_host_name(),
                             peer->m_socket->get_address(), peer->m_socket->get_ping());
            }

            peer->update();
        } catch (std::exception const &e) {
            // an error also writes out the breadcrumbs: the rpcs this peer sent before it
            LOG_ERROR(AVL_LOGGER, "{} ({}) dropped on error: {}", peer->m_name, peer->m_socket->get_host_name(),
                      e.what());
            peer->m_socket->close(false);
        }
    }

    // Pump steam callbacks
    m_acceptor->update();

    // Cleanup
    {
        for (auto &&itr = m_onlinePeers.begin(); itr != m_onlinePeers.end();) {
            Peer::Ptr &peer = *itr;

            if (peer->m_socket->get_status() == Status::Closed) {
                OnPeerQuit(peer);

                itr = m_onlinePeers.erase(itr);
            } else {
                ++itr;
            }
        }
    }

    {
        for (auto &&itr = m_connectedPeers.begin(); itr != m_connectedPeers.end();) {
            Peer::Ptr &peer = *itr;

            if (peer->m_socket->get_status() == Status::Closed) {
                OnPeerDisconnect(peer);

                itr = m_connectedPeers.erase(itr);
            } else {
                ++itr;
            }
        }
    }
}

void NetManager::OnPeerQuit(Peer::Ptr peer)
{
    LOG_NOTICE(AVL_LOGGER, "{} left after {}min, {} online", peer->m_name,
               std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::now() - peer->m_joinTime)
                       .count(),
               m_onlinePeers.size() - 1);
    AVL_DISPATCH_WEBHOOK(peer->m_name + " has quit");
    AVL_SCRIPT_EVENT(ScriptManager::Events::Quit, peer);

    ZdoManager::instance().OnPeerQuit(peer);

    if (AVL_CONFIG.m_replay_mode == ReplayMode::CAPTURE) {
        // TODO
        //assert(false);
        avledet::replay::ReplayManager::instance().on_peer_quit(peer);
    }

}

void NetManager::OnPeerDisconnect(Peer::Ptr peer)
{
    AVL_SCRIPT_EVENT(ScriptManager::Events::Disconnect, peer);

    peer->SendDisconnect();

    LOG_INFO(AVL_LOGGER, "{} has disconnected", peer->m_socket->get_host_name());
}

void NetManager::Uninit()
{
    SendDisconnect();

    //TODO ... dont like both of these...
    for (auto &&peer : m_onlinePeers) {
        OnPeerQuit(peer);
    }

    for (auto &&peer : m_connectedPeers) {
        OnPeerDisconnect(peer);
    }

    //m_acceptor.reset();
    m_acceptor->stop();
}

// C# Encoding.ASCII.GetBytes on a UTF-8 string: one '?' per non-ASCII character
static std::string AsciiChars(std::string_view utf8)
{
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        auto c = static_cast<std::uint8_t>(utf8[i]);
        if (c < 0x80) {
            out += char(c);
            i++;
            continue;
        }
        out += '?';
        i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    }
    return out;
}

void NetManager::OnConfigLoad(bool reloading)
{
    bool hasPassword = !AVL_CONFIG.m_server_password.empty();

    // vanilla refuses to start a public server with a weak password (FejdStartup.IsPublicPasswordValid)
    auto &&pw = AVL_CONFIG.m_server_password;
    if (!reloading && AVL_CONFIG.m_server_public
        && (pw.size() < 5 || AVL_CONFIG.m_world_name.contains(pw) || AVL_CONFIG.m_world_seed.contains(pw)))
        throw std::runtime_error("public server needs a password of 5+ characters, not part of the world name or seed");

    if (hasPassword) {
        m_passwordSalt = VUtils::Random::GenerateAlphaNum(16);

        // Hash a salted password
        //VUtils::md5(merge.c_str(), merge.size(), reinterpret_cast<std::uint8_t*>(m_passwordHash.data()));

        auto s         = avledet::crypto::md5(AsciiChars(AVL_CONFIG.m_server_password) + m_passwordSalt);
        m_passwordHash = avledet::lexicon::CSU::ascii(std::string_view(s));
    } else {
        m_passwordSalt.clear();
        m_passwordHash.clear();
    }
}
