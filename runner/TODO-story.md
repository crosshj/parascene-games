# UUUVVV Runner — From Prototype to Complete Game

## Objective

Transform the existing PS1 endless-runner prototype into a minimally complete arcade game with a beginning, progression, ending, and replayable scoring system.

**Primary goal:** A player can start UUUVVV Runner, choose a character, progress through distinct worlds, complete the campaign, and share a final score.

This is an incremental development effort, NOT a one-shot implementation.

Prioritize reusing existing systems, minimizing new mechanics, and delivering a playable result at the end of each phase.

## Current State

The game currently includes:

- PS1 and browser builds.
- Title screen and character/world selection.
- Two playable characters: Blazer and Runner.
- Seven environments: Lumenwild, City Run, Flat Oval, Hill, Bank, Zipper, Maxi-P.
- Lane movement, jumping, obstacles, collision, and death.
- Procedural/repeating road geometry, including curves, hills, narrowing, banking, and branching.
- Distance tracking and session-best distance.
- Pause, restart, and game-over screens.
- Background music and sound effects.

Currently, all gameplay is endless. There are no objectives, finish lines, campaign progression, permanent scores, or narrative sequences.

Preserve existing functionality wherever possible.

## Target Game Structure

Title → Character Selection → Story Introduction → World Map → Stage → Results → Next Stage → World Completion → Final Ending → Campaign Score

Keep existing endless gameplay available as **Arcade Mode**, separate from the new Campaign Mode.

Initial campaign target: **7 stages, one per story world.** The places, in order, are Lumenwild, Vesper City, Shatterglass, Vesper Reach, The Sineworks, Pale Meridian, and Null Cathedral. Their identities and signature track mechanics are in `docs/story-worlds.md`. The geometric tracks the game runs today stay in Arcade Mode.

Potential expansion: 3 stages per world (21 total), after the initial campaign works end-to-end.

Each stage should have a clear goal and a reasonable completion time. Target an initial full campaign lasting approximately 15–30 minutes.

---

## Phase 0 — Codebase Audit and Implementation Plan

**Do not modify code during this phase.**

Inspect the existing implementation and identify:

- Game states, transitions, and menu architecture.
- World and road generation architecture.
- Distance and lap tracking.
- Obstacle spawning and collision handling.
- Character selection and persistence of selected character.
- Rendering and UI infrastructure.
- PS1 memory, performance, and save-system constraints.
- Differences between PS1 and browser implementations.

Propose the smallest architectural changes needed to support finite stages, objectives, progression, and scoring.

Identify files/modules requiring changes.

**Deliverable:** Concise technical implementation plan and recommended execution order.

Stop and request approval before Phase 1.

---

## Phase 1 — Finite Stage Gameplay

**Goal:** Make one existing world completable.

Use Lumenwild as the initial stage.

Implement:

- Configurable stage start and finish.
- Stage distance target.
- Visible progress indicator.
- Finish-line detection.
- Stage completion state.
- Basic success/results screen.
- Existing death behavior as stage failure.
- Restart stage functionality.

The existing endless runner must remain functional.

Introduce a data-driven stage configuration instead of hardcoding Lumenwild-specific conditions.

Example conceptual configuration:

```js
{
	id: "lumenwild-1",
	world: "lumenwild",
	distance: 3000,
	objective: "reach-finish",
	timeLimit: null
}
```

Adapt this to the project's actual language and architecture.

**Acceptance criteria:**

A player can start Lumenwild, dodge obstacles, reach a defined endpoint, see a completion screen, and restart or exit.

Stop and validate before Phase 2.

---

## Phase 2 — Scoring and Objectives

**Goal:** Make stage performance measurable and comparable.

Introduce a basic scoring system supporting:

- Stage completion bonus.
- Distance/progress.
- Completion time.
- Optional collectible bonus.
- Optional time remaining bonus.
- Penalties where appropriate.

Introduce stage objective types:

1. Reach the finish.
2. Finish before a time limit.
3. Collect a required number of items.

Only implement the mechanics necessary for the initial campaign. Avoid introducing complex power-up systems.

Add one simple collectible type if needed.

Scoring should be deterministic, transparent, and consistent across platforms.

Prevent obvious exploits such as farming infinite points by repeating sections.

The results screen should display relevant performance statistics and a final stage score.

**Acceptance criteria:**

A player can complete a stage, receive a meaningful score, and replay to improve it.

Stop and validate before Phase 3.

---

## Phase 3 — Campaign Progression

**Goal:** Connect stages into a playable sequence.

Implement a campaign definition containing seven ordered stages, one for each story world in `docs/story-worlds.md`:

1. Lumenwild
2. Vesper City
3. Shatterglass
4. Vesper Reach
5. The Sineworks
6. Pale Meridian
7. Null Cathedral

Each stage should have:

- Stage ID and display name.
- Associated world/track.
- Completion requirements.
- Difficulty configuration.
- Unlock requirements.
- Optional narrative metadata.

Build each stage on the track framework. A world's signature mechanics are the ones named in `docs/story-worlds.md`. Early stages can reuse a shape the catalog already runs, with that world's light, scenery, and backdrop.

Initially, a campaign stage can reuse an existing track configuration with adjusted distance, obstacles, and difficulty.

Implement transitions from successful stage completion to the next unlocked stage.

Track completion status and campaign score.

**Acceptance criteria:**

A player can complete Lumenwild, advance into Vesper City, and continue through all seven stages without returning to the title screen.

Stop and validate before Phase 4.

---

## Phase 4 — World Map and Game Flow

**Goal:** Give campaign progression a visible structure.

Introduce a simple world map or stage-selection interface.

Initially, this can be a stylized list, connected nodes, or schematic map. No elaborate artwork is required.

Show:

- Seven stages/worlds.
- Current stage.
- Completed stages.
- Locked stages.
- Available stages.
- Current campaign progress.

Update the menus to distinguish:

- New Campaign / Continue Campaign
- Arcade Mode
- Character Selection

Reuse the existing title and selection screens wherever possible.

Determine how character choice integrates into the campaign without unnecessary additional menu screens.

**Acceptance criteria:**

A player can navigate naturally from title screen through character selection, campaign map, stage gameplay, and results.

The game should feel like one continuous experience rather than disconnected demonstrations.

Stop and validate before Phase 5.

---

## Phase 5 — Narrative Introduction, Transitions, and Ending

**Goal:** Give the game a recognizable beginning, middle, and end.

Implement a lightweight narrative presentation system.

The fiction should establish that the runners are traversing different realms toward a destination or objective.

Do not commit to elaborate lore prematurely.

Support:

- Opening narrative sequence after starting a campaign.
- Short world introductions.
- Optional narrative text between worlds.
- Final completion sequence.
- Credits.
- Final campaign results.

Use simple text overlays, existing backgrounds, sprites, fades, and music.

Narrative events should be data-driven where practical.

Allow players to skip narrative sequences.

Avoid full-motion video, voice acting, or substantial new asset requirements.

**Acceptance criteria:**

A new player can start the game, receive basic narrative context, complete the seven-world journey, and reach a recognizable ending.

Stop and validate before Phase 6.

---

## Phase 6 — Persistence and Replayability

**Goal:** Make progress meaningful across sessions.

Unlocked Lumenwild stages are the first field on the card. The file is `bu00:UUUVVV-RUNNER` in slot 1. `SAVE_VERSION` in `src/save.h` is the layout: an older version is replaced on the next save, and a newer version is left untouched. The browser keeps that same card as the emulator save for the `runner` id (`docs/web.md`).

Implement persistence of:

- Campaign progression.
- Unlocked stages.
- Completed stages.
- Personal-best stage scores.
- Best completed campaign score.
- Selected character, where appropriate.

For PlayStation, investigate and implement memory-card support compatible with the current PS1 toolchain.

For browser playback, use an appropriate browser persistence mechanism.

Separate campaign progress from individual campaign attempts. Define explicit rules for whether restarting a campaign resets its score, stage unlocks, or both.

Provide a safe fallback when no save storage is available.

Existing Arcade Mode records can be persisted separately.

**Acceptance criteria:**

A player can stop playing, restart the game, resume their campaign, and retain previous achievements.

Stop and validate before Phase 7.

---

## Phase 7 — Campaign Balancing and Completion

**Goal:** Turn the technically complete campaign into an enjoyable short game.

Review and adjust:

- Stage lengths.
- Obstacle spacing and frequency.
- Difficulty progression.
- Time limits.
- Collectible placement.
- Scoring balance.
- Stage-to-stage pacing.
- Failure and retry behavior.

Avoid arbitrary difficulty spikes.

The first stage should teach basic survival naturally. Later stages should emphasize the distinguishing geometry and features of their respective worlds.

For example:

- Lumenwild: Twisting trails, rolling hills, narrowing passages.
- Vesper City: Tight turns, traffic, banked interchanges.
- Shatterglass: Switchbacks and split roads.
- Vesper Reach: Climbs, descents, and steeply banked curves.
- The Sineworks: Collapsing lanes and rhythmic obstacles.
- Pale Meridian: Sweeping S-curves and splitting paths.
- Null Cathedral: Banking, elevation, gaps, and branching routes.

These are provisional design directions, not requirements to invent new mechanics.

Campaign completion should be achievable by a reasonably competent player. High scores should require considerably more skill.

Add a final campaign score summary suitable for players sharing their results.

Consider displaying character, score, time, and completion statistics in a compact format.

**Acceptance criteria:**

A player can reasonably finish the game, understand their performance, and feel motivated to attempt a better run.

Stop and validate before expanding the campaign.

---

## Future Expansion — Not Part of Initial Scope

After the complete seven-stage campaign exists, consider:

- Three stages per world.
- Additional characters.
- Character-specific narrative elements.
- More obstacles and collectibles.
- Power-ups and special abilities.
- Boss encounters.
- Alternate paths.
- Multiple endings.
- Ranked challenges.
- Online leaderboards or asynchronous ghost runs.
- Expanded environments and new worlds.

Do not implement these prematurely.

## Architectural Principles

1. Reuse existing rendering, input, collision, camera, track, and audio systems.
2. Prefer data-driven configuration over world-specific conditional logic.
3. Preserve PS1 hardware compatibility and performance.
4. Maintain browser compatibility.
5. Avoid large rewrites or unnecessary abstractions.
6. Favor small, testable, independently verifiable changes.
7. Preserve existing Arcade Mode behavior.
8. Use placeholders rather than allowing visual asset creation to block gameplay development.
9. Keep development focused on reaching a complete playable game.
10. Complete and validate each phase before advancing.

## Overall Definition of Done

UUUVVV Runner is considered minimally complete when a player can:

- Start a new campaign.
- Select a character.
- Experience an introduction.
- Progress through seven distinct stages.
- Encounter objectives and challenges.
- Receive scores for their performance.
- See their progression on a map.
- Save and resume progress.
- Reach a final ending.
- Receive a total campaign score.
- Replay the game to improve their performance.

**The first major milestone is Phase 1: make Lumenwild finishable.**

**The ultimate milestone is a complete run from pressing Start to seeing the final campaign score.**

Begin with Phase 0 only. Inspect the repository, identify integration points, and present the implementation plan. Do not start implementing subsequent phases without approval.