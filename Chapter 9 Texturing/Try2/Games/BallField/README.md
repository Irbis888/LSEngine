# Ball Field

Double-click **Play.cmd**. It runs the built Release x64 engine (Debug fallback) with
this game's manifest, without editor panels or an editor camera. Play starts automatically
only after the complete scene is published. No Play snapshot is written or restored.

Two players, first to 5 goals. **Blue**: **WASD** move, **Shift** runs.
**Red**: **arrows** move, **Enter** runs. **R** restarts from kickoff (score is kept),
**Escape** exits. Walk into the ball to push it; get it fully over the opponent's goal line.
Blue defends the west (left) goal, red the east (right) one. After a goal both players freeze
for 1.5 s while confetti in the scorer's colour bursts out of the net, then a new ball is
spawned in the centre; after a win the score resets. The score is shown on the board behind
the north border and printed to the console.

The ball and the confetti are not scene entities: `FootballMatch.lua` spawns them with
`Entity.Spawn` from `Prefabs/Football/*.json`, whose script parameters (ball radius,
confetti lifetime/speed/gravity) are the balance data.

**Changing the rules live (editor).** Run Try2.exe without `--game`, choose
**Scenes → Ball Field (2 players)**, select `Match` in the hierarchy and edit its parameters
(`winScore`, `goalPause`, `confettiCount`, `goalHalfWidth`, ...) or the Lua/prefab files,
then press **Play**. Every Play re-reads scripts and prefabs; no restart or rebuild. After
Stop, **File → Save Scene** stores the edited parameters back into `BallField.json`. In Play
the free camera moves only with the left mouse button held, so WASD drives the blue player.

The character has a dynamic Rigidbody and a full-height AABB hitbox; its head/limbs follow
the physical body in Lua. Movement sets horizontal velocity before each fixed physics step,
preserving gravity and collision response. The dynamic sphere receives contact impulses
from the player. The field, borders and goal boxes (back and side walls) have static bodies and box colliders;
posts and crossbars are visual only.

The current solver supports linear dynamics; BallRoll.lua adds visual rolling based on
physical displacement, without claiming angular momentum/friction simulation.

Game files:
- `Games/BallField/game.json`: title, control hint, startup scene.
- `Scenes/Games/BallField.json`: field, two humanoids, ball mark, borders, goals, scoreboard, camera and light.
- `Scripts/Games/FieldPlayer.lua`: input (`controls` = wasd/arrows, `runKey`), body velocity,
  visual limbs, Kickoff/Freeze events.
- `Scripts/Games/FootballMatch.lua`: goal detection, score, kickoff, seven-segment scoreboard, spawning.
- `Scripts/Games/Confetti.lua`: one confetti piece: flight, bounce, self-destruction.
- `Prefabs/Football/Ball.json`, `ConfettiBlue.json`, `ConfettiRed.json`: spawn templates.
- `Scripts/Games/BallRoll.lua`: ball's visual orientation/mark.

To run another authored game, create a manifest with `title`, optional `controls`, and
`scene` (relative to the engine asset root), then run:
`Try2.exe --game Games/MyGame/game.json`. Windows Unicode manifest paths are accepted.
Scenes/scripts use the existing serializer and Lua API. Ordinary launch still opens the
editor engine; `--game` disables editor initialization and the free camera for the session.

Build Debug/Release x64 using Try2.sln before first launch. No separate Lua installation
is needed. `tests/RunBallFieldTests.ps1` runs the actual application/renderer with scripted
InputState, exercises pushing, a goal, kickoff, confetti spawn/removal and containment, then exits; it does not inject desktop keys.
