# Project Reboot 3.0 — Bot Overhaul

A community fork of [Project Reboot 3.0](https://github.com/Milxnor/Project-Reboot-3.0) focused on turning its experimental Fortnite bots into stable, useful, and eventually capable AI opponents.

> This fork is an independent development project. Credit for the original server implementation belongs to the Project Reboot contributors.

## Goal

Project Reboot already contains two incomplete bot foundations:

- **Early-version player bots**, which create server-controlled player pawns but currently have almost no behavior.
- **Chapter 2 native AI**, including guard, henchman, bot-manager, and customization scaffolding for supported later builds.

This fork will stabilize those systems, make them easier to test, and build a proper AI layer around them without breaking normal server behavior.

## Current status

The original bot-spawn command works through Reboot's cheat-script interface:

1. Start and join the server.
2. Press **F2** or **Fn + F2** to enable the cheat scripts.
3. Use `spawnbot` after entering the match.

The bot foundation now tracks every successful player-bot spawn through a stable process-lifetime ID and safely validated Unreal object references. It supports two types:

- **Participant** — behaves like the historical `spawnbot` fake player and counts in `PlayersLeft`/`AlivePlayers`, so its death retains normal Battle Royale victory behavior.
- **Practice** — a training target that is deliberately excluded from `PlayersLeft`, `AlivePlayers`, and the death-time `RemoveFromAlivePlayers` call. Killing or removing it therefore cannot decrement the meaningful participant count.

Available cheat-script commands:

```text
spawnbot [count=1] [participant|practice]
botlist
botstressspawn [count=10]
botstresskill
botstresscleanup
botstressstatus
botstressflags
botstressflag <name> <on|off>
botinfo <id>
despawnbot <id>
despawnallbots
```

`spawnbot` and numeric forms such as `spawnbot 3` remain backward compatible and default to Participant. `spawnbot practice` and `spawnbot participant` select a type explicitly.

Stress testing is split into isolated operations. `botstressspawn` only creates and records Practice bot IDs. `botstresskill` applies lethal damage and lets the current Practice death hook run without explicit cleanup. `botstresscleanup` exercises explicit destruction/registry cleanup without applying damage. `botstressstatus` reports every recorded ID, safe reference validity, the active phase, and the last entered/completed lifecycle stage.

While a stress session is active, a game-thread heartbeat logs once per second using only cached primitive diagnostic state. It does not walk UObject arrays, resolve bot pointers, or validate registry entries. Object validation happens only when `botstressstatus` is explicitly requested. `botstressflag <name> <on|off>` can isolate `originalhandler`, `unpossess`, `pawndestroy`, `controllerdestroy`, `playerstatecleanup`, `registryremoval`, and `invalidsweep`. Death and cleanup overrides apply only to recorded stress bots; `invalidsweep` controls the diagnostic scan itself while testing.

For isolation, first run `botstressspawn 10` and wait. Then choose exactly one follow-up: use `botstresskill` to test the death hook, or use `botstresscleanup` to test explicit destruction without death. The final heartbeat line identifies the last stage entered and completed if the game thread stops.

The current bots still do not navigate, search, fight, loot, build, or make meaningful decisions. Practice-bot participation handling is initially targeted at **Fortnite 4.5**; later engine versions and native Chapter 2 bot managers may maintain additional match counters and remain outside this foundation PR.

## Development priorities

### 1. Bot foundation

- Safe bot registry and stable IDs
- Correct spawn, death, despawn, reset, shutdown, and stale-pointer cleanup
- Practice bots that do not accidentally end the match
- Participant bots that count toward normal victory conditions
- Bot inspection and debugging commands
- Clear development build identifiers and logging

### 2. Movement and state machine

- Budgeted bot tick scheduler
- Idle, wander, investigate, engage, dead, and respawn states
- Server-authoritative movement and rotation
- Obstacle checks, jumping, stuck detection, and recovery

### 3. Perception and combat

- Team-aware target detection
- Field of view, line of sight, reaction delay, and target memory
- Weapon selection, aiming, firing, reloading, and disengaging

### 4. Looting and survival

- Pickup discovery and collection
- Inventory and ammunition management
- Healing and distance-based weapon choices
- Storm and safe-zone movement

### 5. Full match behavior

- Warmup and Battle Bus flow
- Jumping, gliding, landing, and rotations
- Encounters, survival priorities, harvesting, and eventually building

### 6. Chapter 2 native guards

The guard and henchman work will remain version-specific and separate from the early-version player-bot implementation. The first target will be one known-good Chapter 2 build before expanding support.

See [ROADMAP.md](ROADMAP.md) for the staged implementation plan.

## Supported scope

Project Reboot itself supports a broad range of legacy Fortnite versions, but bot behavior differs greatly by build. Development in this fork will use explicit version targets rather than claiming that every bot feature works everywhere.

The initial custom player-bot work targets **Fortnite 4.5**. Native guard and henchman work will target a selected Chapter 2 version separately.

## Building

Requirements:

- Visual Studio 2022
- MSVC v143 toolset
- Windows 10 or Windows 11 SDK
- `Release | x64`

Build the solution and load the resulting server DLL through Reboot Launcher. Development builds should display a unique build name or commit identifier so they cannot be confused with the stock `reboot.dll`.

## Branch strategy

- `master` — stable project baseline and documentation
- `bot-foundation` — lifecycle, debugging, and practice-bot work
- `bot-ai-4.5` — early-version custom player AI
- `chapter2-guards` — native Chapter 2 AI experiments

Major changes should be developed through draft pull requests rather than committed directly to `master`.

## Contributing

Bug reports, reverse-engineering notes, version-specific findings, testing results, and focused pull requests are welcome. Reports should include the Fortnite build, launcher/server build, reproduction steps, and relevant logs.

## License

This project retains the original BSD-3-Clause license. Existing third-party code and assets remain subject to their respective licenses and attribution requirements.
