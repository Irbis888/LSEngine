"""Regenerate the deterministic 10,000-body editor stress scene."""
import copy
import json
from pathlib import Path

scenes = Path(__file__).resolve().parents[1] / "Chapter 9 Texturing/Try2/Scenes"
source = json.loads((scenes / "PhysicsMixed512.json").read_text(encoding="utf-8"))
floor = copy.deepcopy(source["entities"][0])
floor["tag"] = "Physics10000Floor"
floor["transform"]["scale"] = [270.0, 1.0, 270.0]
entities = [floor]
template = source["entities"][1]
for row in range(100):
    for column in range(100):
        index = row * 100 + column
        body = copy.deepcopy(template)
        body["tag"] = f"PhysicsCube_{index:05d}"
        body["transform"]["position"] = [
            (column - 49.5) * 2.5, 8.0 + (row % 5) * 0.25, (row - 49.5) * 2.5]
        body["rigidbody"]["velocity"] = [0.0, 0.0, 0.0]
        body["collider"]["restitution"] = 0.0
        body["mesh"]["material"]["name"] = "Physics10000CubeMaterial"
        body["mesh"]["material"]["color"] = [0.93, 0.37, 0.12]
        entities.append(body)
sun = copy.deepcopy(next(e for e in source["entities"] if "directionalLight" in e))
sun["tag"] = "Physics10000Sun"
camera = copy.deepcopy(next(e for e in source["entities"] if "camera" in e))
camera["tag"] = "Physics10000Camera"
camera["transform"]["position"] = [0.0, 130.0, -230.0]
camera["transform"]["rotation"] = [0.5, 0.0, 0.0]
entities.extend([sun, camera])
destination = scenes / "PhysicsStress10000.json"
with destination.open("w", encoding="utf-8", newline="\n") as output:
    output.write('{"version":1,"name":"PhysicsStress10000","entities":[\n')
    output.write(",\n".join(json.dumps(e, separators=(",", ":")) for e in entities))
    output.write("\n]}\n")
assert sum(e.get("rigidbody", {}).get("type") == "dynamic" for e in entities) == 10000
print(f"Generated {destination.name}: 10000 dynamic cubes, floor, camera, sun")
