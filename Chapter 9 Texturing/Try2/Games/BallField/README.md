# Ball Field

Double-click **Play.cmd**. It runs the built Release x64 engine (Debug fallback) with
this game's manifest, without editor panels or an editor camera. Play starts automatically
only after the complete scene is published. No Play snapshot is written or restored.

Controls: **WASD / arrows** move, **Shift** runs, **R** resets the player and ball,
**Escape** exits. Walk into the ball to push it. The fixed elevated camera shows the field.

The character has a dynamic Rigidbody and a full-height AABB hitbox; its head/limbs follow
the physical body in Lua. Movement sets horizontal velocity before each fixed physics step,
preserving gravity and collision response. The dynamic sphere receives contact impulses
from the player. The field and all four borders have static bodies and box colliders.

The current solver supports linear dynamics; BallRoll.lua adds visual rolling based on
physical displacement, without claiming angular momentum/friction simulation.

Game files:
- `Games/BallField/game.json`: title, control hint, startup scene.
- `Scenes/Games/BallField.json`: field, humanoid, ball, borders, camera and light.
- `Scripts/Games/FieldPlayer.lua`: input, body velocity, visual limbs, reset.
- `Scripts/Games/BallRoll.lua`: ball's visual orientation/mark.

To run another authored game, create a manifest with `title`, optional `controls`, and
`scene` (relative to the engine asset root), then run:
`Try2.exe --game Games/MyGame/game.json`. Windows Unicode manifest paths are accepted.
Scenes/scripts use the existing serializer and Lua API. Ordinary launch still opens the
editor engine; `--game` disables editor initialization and the free camera for the session.

Build Debug/Release x64 using Try2.sln before first launch. No separate Lua installation
is needed. `tests/RunBallFieldTests.ps1` runs the actual application/renderer with scripted
InputState, exercises pushing and containment, then exits; it does not inject desktop keys.
