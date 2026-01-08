#include <vector>
#include <unordered_map>
#include <iostream>
#include <string>
#include <algorithm>
#include <thread>

#define ENET_IMPLEMENTATION
#include "libs/enet.h"

#ifdef _WIN32
#include <windows.h>
#endif


#define PORT 55555
#define MAX_PLAYERS 8


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
	PLAYER_CONNECTED,
	PLAYER_SYNC,
	PLAYER_READY,
	PLAYER_DISCONNECTED,

	// Server -> Client control packets
	CONTROL_GAME_START,
	CONTROL_GAME_END
};

// DEFINE PACKETS AND PACKET TYPES AROUND HERE ^ v

#pragma region PACKETS

// Server <-> Clients
#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::PLAYER_CONNECTED;
	enet_uint8 connected_player_id;
} PlayerConnectedPacketData;

// Server <-> Clients
#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::PLAYER_SYNC;
	enet_uint8 player_id;
	PlayerState player_state;
} PlayerSyncPacketData;

// Client -> Server
#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::PLAYER_READY;
} PlayerReadyPacketData;

// Server <-> Clients
#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::PLAYER_DISCONNECTED;
	enet_uint8 disconnected_player_id;
} PlayerDisconnectedPacketData;


// Server -> Clients control packets

#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::CONTROL_GAME_START;
} ControlGameStartPacketData;

#pragma pack(1)
typedef struct {
	PacketType packet_type = PacketType::CONTROL_GAME_END;
} ControlGameEndPacketData;

#pragma endregion PACKETS


ENetHost* server;

std::vector<enet_uint8> player_ids = []{
	std::vector<enet_uint8> v;
	v.reserve(MAX_PLAYERS);
	return v;
}();
std::unordered_map<enet_uint8, PlayerState> player_states;
std::unordered_map<enet_uint8, ServerPlayerData> serverside_player_data;

bool game_started = false;


static inline void HandleReceive(
	ENetPeer* peer,
	ENetPacket* packet
) {
	switch (*((PacketType*)(packet->data + 0))) {
		case PacketType::PLAYER_SYNC:
		{
			if (
				std::find(
					player_ids.begin(),
					player_ids.end(),
					peer->incomingPeerID
				) == player_ids.end()
			) {
				player_ids.push_back(peer->incomingPeerID);

				serverside_player_data[peer->incomingPeerID] = ServerPlayerData{};

				std::cout
				<< "Player "
				<< peer->incomingPeerID
				<< "connected"
				<< std::endl;

				PlayerConnectedPacketData pcp_data;
				pcp_data.connected_player_id = peer->incomingPeerID;
				ENetPacket* player_connected_packet = enet_packet_create(
					&pcp_data,
					sizeof(PlayerConnectedPacketData),
					ENET_PACKET_FLAG_RELIABLE
				);
				enet_host_broadcast(server, 0, player_connected_packet);
			}

			player_states[peer->incomingPeerID] = *((PlayerState*)(
				packet->data + offsetof(PlayerSyncPacketData, player_state)
			));

			enet_host_broadcast(server, 0, packet);
		}
		break;

		case PacketType::PLAYER_READY:
		{
			serverside_player_data[peer->incomingPeerID].ready = true;

			int ready_players = 0;
			for (auto const& [_, ss_player_data] : serverside_player_data) {
				if (ss_player_data.ready) ready_players++;
			}
			if (ready_players != player_ids.size()) return;

			// Everyone is ready; start game

			game_started = true;

			ControlGameStartPacketData cgsp_data;
			ENetPacket* game_start_packet = enet_packet_create(
				&cgsp_data,
				sizeof(ControlGameStartPacketData),
				ENET_PACKET_FLAG_RELIABLE
			);
			enet_host_broadcast(server, 0, game_start_packet);

			enet_packet_destroy(packet);
		}
		break;

		// HANDLE OTHER PACKET TYPES HERE

		default: break;
	}
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
		while (enet_host_service(server, &event, 0) > 0) {
			switch (event.type) {
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
					HandleReceive(
						event.peer,
						event.packet
					);
				}
				break;

				case ENET_EVENT_TYPE_DISCONNECT:
				case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
				{
					if (
						std::find(
							player_ids.begin(),
							player_ids.end(),
							event.peer->incomingPeerID
						) == player_ids.end()
					) continue;

					std::cout
					<< "Player "
					<< event.peer->incomingPeerID
					<< "disconnected"
					<< std::endl;

					player_states.erase(event.peer->incomingPeerID);
					player_ids.erase(
						std::remove(
							player_ids.begin(),
							player_ids.end(),
							event.peer->incomingPeerID
						),
						player_ids.end()
					);

					PlayerDisconnectedPacketData pdp_data;
					pdp_data.disconnected_player_id = event.peer->incomingPeerID;
					ENetPacket* player_disconnected_packet = enet_packet_create(
						&pdp_data,
						sizeof(PlayerDisconnectedPacketData),
						ENET_PACKET_FLAG_RELIABLE
					);
					enet_host_broadcast(server, 0, player_disconnected_packet);
				}
				break;

				case ENET_EVENT_TYPE_NONE: break;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return 0;
} catch (const std::exception& e) {
	std::cout << "ERROR: " << e.what() << std::endl;
	exit(1);
}
}
