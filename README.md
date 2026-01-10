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