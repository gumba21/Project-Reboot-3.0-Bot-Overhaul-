# Bot Overhaul Roadmap

This roadmap separates infrastructure, custom player-bot AI, and native Chapter 2 AI so that version-specific experiments do not destabilize the entire server.

## Phase 0 — Reproducible development baseline

- Confirm the untouched fork builds in `Release | x64`.
- Add a visible development build identifier.
- Record the tested Fortnite version, launcher version, and DLL commit in logs.
- Document how the F2 cheat-script interface is enabled and used.
- Keep stock DLL backups and avoid testing ambiguous builds.

**Exit condition:** a tester can identify exactly which DLL and commit are running.

## Phase 1 — Bot lifecycle foundation

- Replace loose bot storage with a safe registry.
- Assign stable bot IDs.
- Reject and clean up partial or failed spawns.
- Remove dead or invalid controllers, pawns, and player states safely.
- Add explicit despawn and despawn-all operations.
- Prevent stale pointers from remaining in the bot tick list.
- Add useful spawn, death, cleanup, and failure logs.

Planned commands:

```text
spawnbot [count=1] [participant|practice]
botlist
botstresstest [count=10]
botinfo <id>
despawnbot <id>
despawnallbots
```

**Exit condition:** bots can repeatedly spawn, die, and despawn without restarting or corrupting the match.

Foundation implementation notes:

- Bot IDs are monotonic for the lifetime of the loaded DLL and are not reset between matches.
- Registry references pair the UObject address with its global object-array index and serial number so recycled slots are rejected.
- Dead entries remain inspectable until explicit despawn, match/world reset, or shutdown.
- Practice bots are excluded at both Fortnite 4.5 participation boundaries: spawn does not increment `PlayersLeft` or add to `AlivePlayers`, and death does not call `RemoveFromAlivePlayers`.
- Participant bots retain the original engine removal path on death.
- DLL detach invalidates registry references without invoking Unreal functions under the Windows loader lock.

## Phase 2 — Practice and participant modes

### Practice bots

- Do not affect normal last-player-alive victory checks.
- May optionally respawn after death.
- Intended for combat, weapon, and AI testing.

### Participant bots

- Count toward `PlayersLeft` and `AlivePlayers`.
- Win and lose through normal match rules.
- Are cleaned up through the same lifecycle registry.

**Exit condition:** killing a practice bot does not unexpectedly end the match, while participant bots retain normal match semantics.

## Phase 3 — Scheduler and state machine

- Restore bot updates through a controlled scheduler rather than tying all decisions directly to every network flush.
- Use a configurable update budget and frequency.
- Introduce explicit states:
  - Idle
  - Wander
  - Investigate
  - Engage
  - Dead
  - Respawning
- Separate high-frequency movement updates from lower-frequency decisions.

**Exit condition:** one bot can update continuously without excessive server cost or unsafe re-entry.

## Phase 4 — Basic movement

- Apply server-authoritative movement input.
- Rotate toward selected destinations.
- Select short-range wander destinations.
- Check basic obstacles with traces.
- Jump when appropriate.
- Detect being stuck.
- Recover without using teleportation except as a final fallback.

**Exit condition:** one bot can move around a controlled test area for several minutes without freezing or repeatedly colliding with the same obstacle.

## Phase 5 — Perception

- Enumerate valid enemy pawns.
- Filter teammates, spectators, dead actors, and invalid targets.
- Add configurable field of view and sight distance.
- Use line-of-sight checks.
- Add reaction delay.
- Remember the last known target location for a limited period.
- Drop targets when evidence becomes stale.

**Exit condition:** a bot notices valid enemies, ignores invalid ones, and does not use perfect world knowledge.

## Phase 6 — Combat

- Equip an appropriate weapon.
- Turn and aim toward targets.
- Fire in controlled bursts.
- Stop firing when line of sight is lost.
- Reload when appropriate.
- Change weapons by distance and ammunition state.
- Stop targeting eliminated players.
- Add accuracy and reaction variation rather than artificial perfect aim.

**Exit condition:** a practice bot can participate in a stable one-on-one fight.

## Phase 7 — Looting and inventory

- Find nearby pickups.
- Score loot by current needs.
- Move toward and collect items.
- Track ammunition and healing supplies.
- Avoid repeatedly pursuing unreachable pickups.
- Use healing items when safe.

**Exit condition:** a bot can acquire a weapon, ammunition, and healing without manual inventory injection.

## Phase 8 — Battle Royale behavior

- Warmup behavior.
- Battle Bus acknowledgement and jump timing.
- Glider deployment and landing selection.
- Storm awareness and safe-zone movement.
- Rotation priorities.
- Encounter, retreat, and survival decisions.
- Harvesting and building only after movement and combat are stable.

**Exit condition:** bots can complete a basic match loop without scripted teleports or manual intervention.

## Phase 9 — Native Chapter 2 guards

This work must remain isolated from the Fortnite 4.5 custom player-bot path.

- Select one known-good Chapter 2 version.
- Verify `UFortServerBotManagerAthena` and `AFortAthenaMutator_Bots` setup.
- Repair customization-data and spawner-data paths.
- Validate AI controller, behavior tree, navigation, inventory, names, and cosmetics.
- Add version gates around all native AI hooks.
- Expand version coverage only after the first target is reproducible.

**Exit condition:** a native guard or henchman can spawn and run its intended game-provided behavior reliably on the selected build.

## Testing policy

Every phase should include:

- Exact Fortnite build tested
- Exact commit SHA tested
- Spawn count and test duration
- Expected and observed victory behavior
- Relevant server logs
- Crash or cleanup reproduction steps

A feature is not considered broadly supported merely because it works on one undocumented version.
