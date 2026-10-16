# Physics scheduling measurements — 2026-10-09

`PhysicsSystem` uses the engine's JobSystem. Only integration and initial AABB calculation
are dispatched; grid construction, pair traversal, correction, grid updates and re-queries
remain sequential. Reusable arrays are prepared by the main thread before dispatch.

## Reproduction

Run `tests/RunPhysicsParallelTests.ps1`. It builds the actual PhysicsSystem, JobSystem,
ResourceManager and SceneSerializer with MSVC Release `/O2`, then compares both modes
in the same executable. Results: `tests/.build/physics-parallel/results.csv`;
log: `tests/.build/physics-parallel-run.log`.

CPU: Intel Core i5-12500H; four enkiTS workers; owner thread assists only High tasks.
Three repeats per scene/mode/loading case; mode order alternates between repeats.
Fixed dt = 1/60; 20 warmup steps, then restore the identical scene; 600 measured steps
on the supplied 256/512 scenes and 120 on isolated 8192/32768-body scenes.
No compiler ran during the measurements. Files were warmed; OS caches were not cleared.

Loading cases use the real CPU decoder/importer: 1000 DDS files from StreamingStress,
plus continuous imports of a deterministic OBJ with 20,000 triangles. Admission is
capped at three consumers, leaving a worker available. Publication and consumption of
results happen outside the timed physics step; unused imported arrays are discarded by
the harness to bound its memory. This is a CPU physics benchmark without rendering or
GPU uploads. The stage timers also run in the normal engine and are visible in Statistics
and Tracy; these results do not measure whole rendered frames.

## Results

Values below are medians across the three runs, in milliseconds. Dispatch is forced
in the parallel cases to expose its overhead, independently of the default threshold.
Each run records every stage, median/p95/max full step and the number of steps overlapping
an active decoder in CSV. Large isolated scenes have no overlapping bodies; they exercise
the grid and indexed processing rather than a dense contact pile.

| Scene | Background imports | Full step serial | Full step parallel | Integration + AABB serial | Integration + AABB parallel |
|---|---|---:|---:|---:|---:|
| 256 bodies | off | 0.50 | 0.59 | 0.003 | 0.010 |
| 256 bodies | on | 0.59 | 0.62 | 0.004 | 0.010 |
| 512 bodies | off | 0.88 | 0.91 | 0.005 | 0.015 |
| 512 bodies | on | 0.99 | 1.00 | 0.007 | 0.014 |
| 8192 bodies | off | 12.32 | 12.67 | 0.106 | 0.100 |
| 8192 bodies | on | 19.80 | 18.14 | 0.154 | 0.141 |
| 32768 bodies | off | 132.76 | 120.06 | 0.52 | 0.29 |
| 32768 bodies | on | 162.59 | 162.91 | 0.66 | 0.46 |

The repeated loading cases overlap decoding in all 600 small-scene steps, all 120
8192-body steps, and 328–334 of the combined 360 steps for each 32768-body mode.
Maximum observed active resource decoders: three. Serial/parallel step results match
exactly for transforms, velocities, collision counts and query counts across 600 steps
per supplied scene, including registry mutations between steps. Existing correction-created
contact, trigger, sphere and floor tests also pass with forced dispatch.

The 1024/2048/4096 crossover checks showed integration + AABB costs of
0.014/0.029/0.061 ms serial versus 0.031/0.051/0.071 ms parallel. Therefore the default
threshold is 8192, rather than the provisional 128. The range remains 64; a 128/256/512
range sweep on 8192 bodies did not establish a consistent improvement. Both values and
the serial/parallel switch are adjustable in Statistics or via
`Engine::GetPhysics().SetSchedulingSettings(...)`.

Most full-step cost is still grid construction and solver, and concurrent imports
also compete for CPU/cache bandwidth. The stage gain does not imply the same gain for
the entire physics step; the measurements above show that explicitly. No frame-rate
guarantee or preemption of an already running importer is claimed.

## 10,000-body scene

`Scenes/PhysicsStress10000.json` contains exactly 10,000 dynamic cubes in a 100×100 grid,
one static floor, camera and directional light. Cube geometry is shared by ResourceManager.
Open **Scenes → Physics Stress (10,000 Bodies)**, then press **Play**. At the default
threshold both integration and AABB use dispatch. The Statistics panel displays the
actual mode and timings; disable Parallel physics to compare after reopening the scene.

Regenerate it with `python tests/GeneratePhysicsStress10000.py`.
`PhysicsBroadPhaseTests.exe Scenes/PhysicsStress10000.json` validates 600 physics steps:
finite positions, floor contacts, and grid queries against exhaustive initial AABB checks.
