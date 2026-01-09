# Pacman Game - Operating Systems Project

## Architecture Overview

This workspace contains two related Pacman implementations demonstrating OS concepts:

1. **SO-2526-sol-parte1/** - Single-player game using threads and process forking
2. **client-base-with-Makefile-v3/** - Client-server multiplayer with IPC via named pipes

Both share similar board/game logic but differ in concurrency and communication patterns.

## Key Components

### Shared Board Model (`include/board.h`)
- `board_t`: Main game state (width, height, pacmans, ghosts, tempo)
- `board_pos_t`: Per-cell state with `pthread_mutex_t lock` for safe concurrent access
- `pthread_rwlock_t state_lock`: Read-write lock on entire board for thread coordination
- Movement functions: `move_pacman()`, `move_ghost()` return `move_t` enum (VALID_MOVE, DEAD_PACMAN, REACHED_PORTAL, INVALID_MOVE)

### Client-Server Architecture (client-base-with-Makefile-v3)

**IPC via Named Pipes (FIFOs)**:
- Server creates registration pipe (e.g., `/tmp/server_pipe`)
- Each client creates two pipes: `/tmp/<client_id>_request` (server→client commands) and `/tmp/<client_id>_notification` (client→server updates)
- Protocol defined in `protocol.h`: OP_CODE_CONNECT, OP_CODE_DISCONNECT, OP_CODE_PLAY, OP_CODE_BOARD

**Concurrency Model**:
- **Producer-Consumer Buffer** (`request_buffer_t` in `server.h`): Uses semaphores (`sem_t empty/full`) + mutex for connection requests
- **Per-Client Game Threads**: Each connected client gets a dedicated `pthread` running the game loop
- **Client Receiver Thread**: Separate thread continuously reads board updates from notification pipe

**Session Management** (`client_session_t`):
- Tracks `client_id`, `req_fd`, `notif_fd`, `active` status, `points`, and `game_thread`
- Protected by `pthread_mutex_t sessions_mutex`

### Thread Model (SO-2526-sol-parte1)

- `ncurses_thread`: Refreshes display every `board->tempo` ms
- `pacman_thread`: Processes Pacman movements with timing based on `board->tempo * (1 + pacman->passo)`
- `ghost_thread`: Individual thread per ghost with similar timing logic
- All threads check `thread_shutdown` flag and use `pthread_rwlock` for board access

## Build & Run

### Single-Player Game
```bash
cd SO-2526-sol-parte1
make            # Builds bin/Pacmanist
make run ARGS="./levels"
```

### Client-Server
```bash
cd client-base-with-Makefile-v3
make            # Builds bin/Pacmanist (server) and bin/client

# Terminal 1: Start server
./bin/Pacmanist ./levels 4 /tmp/server_pipe

# Terminal 2+: Start clients
./bin/client 1 /tmp/server_pipe
./bin/client 2 /tmp/server_pipe commands.txt  # Optional command file
```

**Important**: Server requires levels directory, max concurrent games, and FIFO path. Clients need unique ID and server pipe path.

## Key Conventions

### Compilation Flags
- `-D_POSIX_C_SOURCE=200809L`: Required for POSIX features (pthreads, named pipes)
- `-std=c17 -Wall -Wextra`: Strict C17 with warnings
- `-lncurses`: Terminal UI library (both projects)

### Memory & Resource Management
- **Always** lock `board_pos_t->lock` before modifying cell content
- **Always** use `pthread_rwlock_rdlock/wrlock` when accessing board state across threads
- Named pipes created with `mkfifo()` must be manually cleaned up (not done automatically)
- Board cleanup: Call `unload_level()` to free dynamically allocated board/pacmans/ghosts

### File Naming Patterns
- Level files: Located in `./levels/` directory, parsed by `parser.c`
- Command files: Text files with movement commands (w/a/s/d), one per line
- Debug logs: Suffix `.log`, written via `debug()` function (see `debug.h`)

### Error Handling
- Pipe operations: Check for `-1` returns from `open()`, handle `errno == EINTR`
- Thread functions return `void*` with allocated `int*` for error codes (caller must free)
- Board movement returns `move_t` enum - check for `DEAD_PACMAN` or `REACHED_PORTAL` to trigger game state changes

## Cross-Project Notes

Both projects share identical `board.h`, `display.h`, `parser.h` interfaces but differ in:
- **client-base-with-Makefile-v3** adds `api.h` (client API), `protocol.h` (IPC opcodes), `server.h` (session management)
- **SO-2526-sol-parte1** uses process forking (`fork()`) for backup creation, not present in client version
- ncurses rendering is consistent: `draw_board()` for single-player, `draw_board_client()` for multiplayer

When modifying shared code (board logic, parsing, display), test changes in both projects.
