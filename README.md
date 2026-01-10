A foundation for implementing your own game server via ENet

Suggestion for making your own game server:
- Define all player states which you wish to synchronize in the `PlayerState` struct
- Define all data which you want to keep in the server regarding players in the `ServerPlayerData` struct
- Define additional packet types in the `PacketTypes` enum, and add their associated packet structs below it
- Handle receiving various packet types in `HandleReceive()`


# Client-side protocol

1) Connect via [ENet](https://github.com/zpl-c/enet)
	- If game is already started then you will be disconnected

2) Send a `PLAYER_SYNC` packet with your player state to register yourself as a player on the server

3) Send a `PLAYER_READY` packet if you are ready to start the game

4) Send a `PLAYER_SYNC` packet with your player state every frame/physics/network tick, and handle receiving the following packets:
	- `PLAYER_SYNC` -> synchronize remote player's state (create player if doesn't exist)
	- `PLAYER_DISCONNECTED` -> remove player from local collection of players(/states)
	- `CONTROL_GAME_START` -> set up game/world and/or spawn players and start game
	- `CONTROL_SET_PLAYER_STATE` -> set your local client state to received state
	- `CONTROL_GAME_END` -> end game


# Example client

```cpp
#include <iostream>
#include <thread>
#include <array>

#define ENET_IMPLEMENTATION
#include "../libs/enet.h"


#define PORT 55555

#define CONNECT_TIMEOUT_MS 1000


typedef uint16_t PlayerID;


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



PlayerState local_state = {
	.position = {1.1, 2.2, 3.01}
};

int main() {
	if (enet_initialize() != 0) {
		std::cout << "Failed to initialize ENet" << std::endl;
		exit(1);
	}
	atexit(enet_deinitialize);

	ENetHost* client = enet_host_create(NULL, 1, 1, 0, 0);
	if (!client) {
		std::cout << "Failed to create ENet client" << std::endl;
		exit(1);
	}

	ENetAddress address = {0};
	enet_address_set_host(&address, "::1");
	address.port = PORT;
	ENetPeer* server_peer = enet_host_connect(client, &address, 1, 0);
	if (!server_peer) {
		std::cout << "Failed to create ENet peer" << std::endl;
		exit(1);
	}

	ENetEvent event;

	if (
		enet_host_service(client, &event, CONNECT_TIMEOUT_MS) > 0 &&
		event.type == ENET_EVENT_TYPE_CONNECT
	) std::cout << "Connected to server" << std::endl;
	else {
		std::cout << "Failed to connect to server" << std::endl;
		enet_peer_reset(server_peer);
		exit(1);
	}

	PlayerSyncPacketData psp_data{};
	psp_data.player_state = local_state;
	ENetPacket* initial_sync_packet = enet_packet_create(
		&psp_data,
		sizeof(PlayerSyncPacketData),
		ENET_PACKET_FLAG_RELIABLE
	);
	enet_peer_send(server_peer, 0, initial_sync_packet);

	ENetPacket* ready_packet = enet_packet_create(
		std::array<char, 1>{PacketType::PLAYER_READY}.data(),
		sizeof(PacketType),
		ENET_PACKET_FLAG_RELIABLE
	);
	enet_peer_send(server_peer, 0, ready_packet);

	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		PlayerSyncPacketData psp_data{};
		psp_data.player_state = local_state;
		ENetPacket* sync_packet = enet_packet_create(
			&psp_data,
			sizeof(PlayerSyncPacketData),
			0
		);
		enet_peer_send(server_peer, 0, sync_packet);

		while (enet_host_service(client, &event, 0) > 0) {
			if (event.type != ENET_EVENT_TYPE_RECEIVE) continue;

			switch (*((PacketType*)(event.packet->data + 0))) {
				default: break;

				case PacketType::PLAYER_SYNC:
				{
					std::cout
					<< "Received player "
					<< +*(
						event.packet->data +
						offsetof(PlayerSyncPacketData, player_id)
					)
					<< " state:"
					<< std::endl;

					PlayerState received_state = *((PlayerState*)(
						event.packet->data +
						offsetof(PlayerSyncPacketData, player_state)
					));

					std::cout << "position x: " << received_state.position.x << std::endl;
					std::cout << "position y: " << received_state.position.y << std::endl;
					std::cout << "position z: " << received_state.position.z << std::endl;
				}
				break;

				case PacketType::PLAYER_DISCONNECTED:
				{
					std::cout
					<< "Player "
					<< +*(
						event.packet->data +
						offsetof(PlayerDisconnectedPacketData, disconnected_player_id)
					)
					<< " disconnected"
					<< std::endl;
				}
				break;


				case PacketType::CONTROL_GAME_START:
				{
					std::cout << "Game start received" << std::endl;
				}
				break;

				case PacketType::CONTROL_SET_PLAYER_STATE:
				{
					std::cout << "Set state received:" << std::endl;

					PlayerState received_state = *((PlayerState*)(
						event.packet->data +
						offsetof(ControlSetPlayerStatePacketData, state)
					));

					std::cout << "position x: " << received_state.position.x << std::endl;
					std::cout << "position y: " << received_state.position.y << std::endl;
					std::cout << "position z: " << received_state.position.z << std::endl;

					local_state.position.x = received_state.position.x;
					local_state.position.y = received_state.position.y;
					local_state.position.z = received_state.position.z;
				}
				break;

				case PacketType::CONTROL_GAME_END:
				{
					std::cout << "Game end received" << std::endl;
					exit(0);
				}
				break;
			}

			enet_packet_destroy(event.packet);
		}
	}

	return 0;
}
```