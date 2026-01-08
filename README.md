A foundation for implementing your own game server via ENet

The source code is filled with a bunch of uppercase comments like "`// ADD PLAYER STATE HERE`" suggesting a way to modify the source code


# Client-side protocol

1) Connect via [ENet](https://github.com/zpl-c/enet)
	- If game is already started then you will be disconnected

2) Send a `PLAYER_SYNC` packet with your player state to register yourself as a player on the server

3) Send a `PLAYER_READY` packet if you are ready to start the game

4) Send a `PLAYER_SYNC` packet with your player state every frame/physics/network tick, and handle receiving the following packets:
	- `PLAYER_CONNECTED` -> allocate memory for connected player's state
	- `PLAYER_SYNC` -> synchronize player's state
	- `PLAYER_DISCONNECTED` -> remove player from local collection of players(/states)
	- `CONTROL_GAME_START` -> set up game/world and/or spawn players and start game
	- `CONTROL_GAME_END` -> end game