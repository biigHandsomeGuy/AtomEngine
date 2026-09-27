"""Rebuild the deterministic, self-contained material lab assets and scene (stdlib only)."""
import base64
import json
import math
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "Assets/Models/MaterialLab"
SCENE = ROOT / "Samples/Editor/Scenes/MaterialLab.atomscene"


def png(rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes([0, *rgb]))) + chunk(b"IEND", b""))


def sphere():
    slices, rings = 32, 16
    positions, normals, uvs, tangents, indices = [], [], [], [], []
    for row in range(rings + 1):
        theta = math.pi * row / rings
        for col in range(slices + 1):
            phi = 2 * math.pi * col / slices
            normal = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            positions.extend(normal)
            normals.extend(normal)
            uvs.extend((col / slices, row / rings))
            tangents.extend((-math.sin(phi), 0, math.cos(phi), 1))
    for row in range(rings):
        for col in range(slices):
            a = row * (slices + 1) + col
            b = a + slices + 1
            if row != 0:
                indices.extend((a, a + 1, b))
            if row != rings - 1:
                indices.extend((a + 1, b + 1, b))
    data = bytearray()
    views, accessors = [], []
    for values, components, kind, scalar in [(positions, 3, "VEC3", "f"), (normals, 3, "VEC3", "f"),
            (uvs, 2, "VEC2", "f"), (tangents, 4, "VEC4", "f"), (indices, 1, "SCALAR", "I")]:
        block = struct.pack("<" + scalar * len(values), *values)
        views.append({"buffer": 0, "byteOffset": len(data), "byteLength": len(block)})
        data.extend(block)
        accessors.append({"bufferView": len(views)-1, "componentType": 5126 if scalar == "f" else 5125,
                          "count": len(values)//components, "type": kind})
    accessors[0].update(min=[-1, -1, -1], max=[1, 1, 1])
    return {"asset": {"version": "2.0", "generator": "AtomEngine Material Lab"},
            "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
            "bufferViews": views, "accessors": accessors,
            "materials": [{"name": "Lab Surface", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.25, 0.08, 1]}}],
            "meshes": [{"name": "UV Sphere", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3}, "indices": 4, "material": 0}]}],
            "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}


def entity(identity, name, position):
    return {"id": identity, "name": name, "translation": position, "rotation": [0, 0, 0, 1], "scale": 1}


def parameters(color, metallic, roughness, emission=(0, 0, 0), flags=15):
    return {"material": 0, "baseColorFactor": [*color, 1], "metallicFactor": metallic,
            "roughnessFactor": roughness, "normalScale": 1, "emissiveFactor": list(emission), "textureFlags": flags}


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    SCENE.parent.mkdir(parents=True, exist_ok=True)
    source = sphere()
    (ASSETS / "Sphere.gltf").write_text(json.dumps(source, separators=(",", ":")) + "\n", encoding="utf-8")
    textured = json.loads(json.dumps(source))
    textured["images"] = [{"uri": "data:image/png;base64," + base64.b64encode(png(rgb)).decode()}
                           for rgb in [(128, 128, 128), (192, 128, 224), (0, 128, 64), (128, 64, 32)]]
    textured["textures"] = [{"source": i} for i in range(4)]
    textured["materials"] = [{"name": "Factor Probe", "pbrMetallicRoughness": {
        "baseColorFactor": [0.2, 0.4, 0.8, 1], "metallicFactor": 0.6, "roughnessFactor": 0.7,
        "baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 2}},
        "normalTexture": {"index": 1, "scale": 0.25}, "emissiveTexture": {"index": 3}, "emissiveFactor": [0.5, 0.25, 0.125]}]
    (ASSETS / "TexturedSphere.gltf").write_text(json.dumps(textured, separators=(",", ":")) + "\n", encoding="utf-8")
    models = []
    for row in range(5):
        for col in range(5):
            item = entity(len(models)+1, f"Metallic {row/4:.2f} / Roughness {col/4:.2f}", [(2-col)*2.5, (2-row)*2.5, 0])
            item.update(asset="Assets/Models/MaterialLab/Sphere.gltf", materialOverrides=[parameters((0.8, 0.25, 0.08), row/4, col/4)])
            models.append(item)
    for col, color in enumerate([(0.8, 0.08, 0.05), (0.05, 0.8, 0.08), (0.08, 0.15, 0.8)]):
        item = entity(len(models)+1, "Pure color " + "RGB"[col], [(1-col)*2.5, -7.5, 0])
        item.update(asset="Assets/Models/MaterialLab/Sphere.gltf", materialOverrides=[parameters(color, 0, 0.6)])
        models.append(item)
    item = entity(29, "Textured factors and normal", [5, -7.5, 0])
    item.update(asset="Assets/Models/MaterialLab/TexturedSphere.gltf", materialOverrides=[])
    models.append(item)
    item = entity(30, "Emission without textures", [-5, -7.5, 0])
    item.update(asset="Assets/Models/MaterialLab/Sphere.gltf", materialOverrides=[parameters((0, 0, 0), 0, 1, (3, 0.8, 0.1), 0)])
    models.append(item)
    camera = entity(31, "Material Lab Camera", [0, -1.25, -27])
    camera.update(rotation=[0, 1, 0, 0], verticalFovDegrees=45, nearClip=0.1, farClip=100)
    sun = entity(32, "Material Lab Sun", [0, 0, 0])
    sun.update(rotation=[-0.1159169, 0.8804763, 0.27984816, 0.3647052], type="directional", color=[1, 1, 1],
               intensity=2, enabled=True, castsShadows=True, shadowCenter=[0, -1.25, 0], shadowBounds=[30, 30, 30])
    scene = {"format": "AtomEngineScene", "version": 4, "nextEntityId": 33, "activeCamera": 31, "activeLight": 32,
             "models": models, "cameras": [camera], "lights": [sun]}
    SCENE.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(models)} material samples: {SCENE}")


if __name__ == "__main__":
    main()
