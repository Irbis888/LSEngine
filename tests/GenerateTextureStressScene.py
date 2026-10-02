"""Generate 1000 distinct DDS files and a scene. Run from any working directory."""
from pathlib import Path
import json
import struct

repo = Path(__file__).resolve().parent.parent
assets = repo / 'Textures' / 'StreamingStress'
scene = repo / 'Chapter 9 Texturing' / 'Try2' / 'Scenes' / 'TextureStreaming1000.json'
assets.mkdir(parents=True, exist_ok=True)
entities = []
for index in range(1000):
    width = height = 64
    header = [0] * 37
    header[0:6] = [0x20534444, 124, 0x100f, height, width, width * 4]
    header[7] = 1
    header[19:22] = [32, 4, 0x30315844]
    header[27] = 0x1000
    header[32:37] = [28, 3, 0, 1, 0]  # DXGI R8G8B8A8_UNORM, Texture2D.
    pixels = bytearray()
    color = [48 + (index * factor) % 208 for factor in (37, 73, 127)]
    for y in range(height):
        for x in range(width):
            shade = 1.0 if ((x // 8) + (y // 8)) % 2 else 0.55
            pixels.extend([*(int(channel * shade) for channel in color), 255])
    name = f'texture_{index:04d}.dds'
    (assets / name).write_bytes(struct.pack('<37I', *header) + pixels)
    entities.append({
        'tag': f'Texture_{index:04d}',
        'transform': {'position': [(index % 40 - 19.5) * 1.3, (index // 40 - 12) * 1.3, 0],
                      'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'source': 'primitive', 'primitive': 'cube',
                 'material': {'name': f'Material_{index:04d}', 'albedo': f'StreamingStress/{name}',
                              'color': [1, 1, 1], 'roughness': 0.6}}
    })
entities.extend([
    {'tag': 'Sun', 'directionalLight': {'color': [1, 1, 1], 'intensity': 1.3,
                                      'direction': [-0.3, -0.4, 1], 'enabled': True}},
    {'tag': 'Camera', 'transform': {'position': [0, 0, -38], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
     'camera': {'fov': 1.3, 'nearZ': 0.1, 'farZ': 1500, 'aspectRatio': 1.7777778}}
])
scene.write_text(json.dumps({'version': 1, 'name': 'TextureStreaming1000', 'entities': entities},
                           ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
print(f'Generated 1000 unique 64x64 DDS textures in {assets}')
print(f'Scene: {scene}')
