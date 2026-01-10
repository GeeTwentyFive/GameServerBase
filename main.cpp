#include <limits>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <array>

#define ENET_IMPLEMENTATION
#include "libs/enet.h"

#ifdef _WIN32
#include <windows.h>
#endif


#define PORT 55555
#define MAX_PLAYERS 8


typedef uint16_t PlayerID;

PlayerID _player_GUID = 0;
static inline const PlayerID NewPlayerGUID() {
	if (_player_GUID == std::numeric_limits<PlayerID>::max()) throw std::runtime_error(
		"Player GUID counter overflow"
	);

	return _player_GUID++;
}


#pragma pack(1)
typedef struct {
	float x = 0.0;
	float y = 0.0;
	float z = 0.0;
} Vec3;

#pragma pack(1)
typedef struct {
	Vec3 position;
	// ADD PLAYER STATE HERE
} PlayerState;

#pragma pack(1)
typedef struct {
	bool ready = false;
	// ADD SERVERSIDE-ONLY PLAYER DATA HERE
} ServerPlayerData;


enum PacketType : char {
	PLAYER_SYNC, // Client -> Server -> *other* Clients
	PLAYER_READY, // Client -> Server
	PLAYER_DISCONNECTED, // Server -> Clients

	// Server -> Client(s) control packets
	CONTROL_GAME_START, // Server -> Clients
	CONTROL_SET_PLAYER_STATE, // Server -> Client
	CONTROL_GAME_END // Server -> Clients
};

// DEFINE PACKET TYPES AND ADDITIONAL PACKET DATA AROUND HERE ^ v

#pragma region PACKETS_DATA

#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::PLAYER_SYNC;
	PlayerID player_id;
	PlayerState player_state;
} PlayerSyncPacketData;

#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::PLAYER_DISCONNECTED;
	PlayerID disconnected_player_id;
} PlayerDisconnectedPacketData;


// Server -> Clients control packets

#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::CONTROL_SET_PLAYER_STATE;
	PlayerState state;
} ControlSetPlayerStatePacketData;

#pragma endregion PACKETS_DATA


ENetHost* server;

std::unordered_map<ENetPeer*, PlayerID> peer_to_player_id(MAX_PLAYERS);
std::unordered_map<PlayerID, ENetPeer*> player_id_to_peer(MAX_PLAYERS);

std::unordered_map<PlayerID, PlayerState> player_states(MAX_PLAYERS);
std::unordered_map<PlayerID, ServerPlayerData> serverside_player_data(MAX_PLAYERS);

bool game_started = false;


static inline void HandleReceive(
	ENetPeer* peer,
	ENetPacket* packet
) {
	if (packet->dataLength < sizeof(PacketType)) {enet_packet_destroy(packet); return;}

	switch (*((PacketType*)(packet->data + 0))) {
		default: break;

		case PacketType::PLAYER_SYNC:
		{
			if (packet->dataLength < sizeof(PlayerSyncPacketData)) break;

			if (peer_to_player_id.find(peer) == peer_to_player_id.end()) {
                                const PlayerID player_id = NewPlayerGUID();

                                peer_to_player_id[peer] = player_id;
                                player_id_to_peer[player_id] = peer;

                                serverside_player_data[player_id] = ServerPlayerData{};

                                std::cout
				<< "Player "
				<< player_id
				<< " connected"
				<< std::endl;
                        }

			const PlayerID player_id = peer_to_player_id[peer];

			player_states[player_id] = *((PlayerState*)(
				packet->data + offsetof(PlayerSyncPacketData, player_state)
			));

			((PlayerSyncPacketData*)packet->data)->player_id = player_id;
                        // Retransmit sync packet to all other peers except the one who sent it
                        for (auto const& [player_peer, _] : peer_to_player_id) {
                                if (player_peer == peer) continue;
                                enet_peer_send(
                                        player_peer,
                                        0,
                                        enet_packet_create(
                                                packet->data,
                                                packet->dataLength,
                                                0
                                        )
                                );
                        }
		}
		break;

		case PacketType::PLAYER_READY:
		{
			if (game_started) break;
			if (peer_to_player_id.find(peer) == peer_to_player_id.end()) break;
			if (serverside_player_data[peer_to_player_id[peer]].ready) break;

			const PlayerID player_id = peer_to_player_id[peer];

			serverside_player_data[player_id].ready = true;

			int ready_players = 0;
			for (auto const& [_, ss_player_data] : serverside_player_data) {
				if (ss_player_data.ready) ready_players++;
			}
			if (ready_players != peer_to_player_id.size()) break;

			// Everyone is ready; start game

			game_started = true;

			ENetPacket* game_start_packet = enet_packet_create(
                                std::array<char, 1>{PacketType::CONTROL_GAME_START}.data(),
				sizeof(PacketType),
				ENET_PACKET_FLAG_RELIABLE
			);
			enet_host_broadcast(server, 0, game_start_packet);
		}
		break;

		// HANDLE OTHER PACKET TYPES HERE
	}

	enet_packet_destroy(packet);
}


int main(int argc, char* argv[]) {
try {
	if (enet_initialize() != 0) throw std::runtime_error("Failed to initialize ENet");
	atexit(enet_deinitialize);

	ENetAddress address = {0};
	address.host = ENET_HOST_ANY;
	address.port = PORT;
	server = enet_host_create(&address, MAX_PLAYERS, 1, 0, 0);
	if (server == nullptr) throw std::runtime_error("Failed to create ENet server");
	atexit([]{enet_host_destroy(server);});

	std::cout << "Server started on port " << PORT << std::endl;

	#ifdef _WIN32
	timeBeginPeriod(1);
	atexit([]{timeEndPeriod(1);});
	#endif

	ENetEvent event;
	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		while (enet_host_service(server, &event, 0) > 0) {
			switch (event.type) {
				default: break;

				case ENET_EVENT_TYPE_CONNECT:
				{
					if (game_started) {
						enet_peer_disconnect(event.peer, 0);
						enet_host_flush(server);
						enet_peer_reset(event.peer);
						continue;
					}
				}
				break;

				case ENET_EVENT_TYPE_RECEIVE:
				{
					HandleReceive(event.peer, event.packet);
				}
				break;

				case ENET_EVENT_TYPE_DISCONNECT:
				case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
				{
					if (peer_to_player_id.find(event.peer) == peer_to_player_id.end()) continue;

                                        const PlayerID player_id = peer_to_player_id[event.peer];

                                        if (game_started) {
                                                std::cout
                                                << "Player "
                                                << player_id
                                                << " disconnected during started game"
                                                << ", shutting down..."
                                                << std::endl;

                                                exit(0);
                                        }

                                        std::cout
                                        << "Player "
                                        << player_id
                                        << " disconnected"
                                        << std::endl;

                                        player_states.erase(player_id);
                                        serverside_player_data.erase(player_id);

                                        player_id_to_peer.erase(player_id);
                                        peer_to_player_id.erase(event.peer);

                                        PlayerDisconnectedPacketData pdp_data{};
					pdp_data.disconnected_player_id = player_id;
					ENetPacket* player_disconnected_packet = enet_packet_create(
						&pdp_data,
						sizeof(PlayerDisconnectedPacketData),
						ENET_PACKET_FLAG_RELIABLE
					);
					enet_host_broadcast(server, 0, player_disconnected_packet);
                                }
				break;
			}
		}
	}

	return 0;
} catch (const std::exception& e) {
	std::cout << "ERROR: " << e.what() << std::endl;
	exit(1);
}
}
