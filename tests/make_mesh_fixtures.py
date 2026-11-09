import copy
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1] / "build" / "mesh-fixtures"
root.mkdir(parents=True, exist_ok=True)
vertices = struct.pack("<9f", -0.75, -0.75, 0, 0.75, -0.75, 0, 0, 0.75, 0)
binary = vertices + struct.pack("<3H", 0, 1, 2) + bytes(2)
base = {
    "asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
    "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36},
                    {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
         "min": [-0.75, -0.75, 0], "max": [0.75, 0.75, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
    "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
}

def save(name, data, blob=binary):
    encoded = json.dumps(data, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    blob += bytes(-len(blob) % 4)
    body = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    body += struct.pack("<II", len(blob), 0x004E4942) + blob
    (root / name).write_bytes(struct.pack("<III", 0x46546C67, 2, len(body) + 12) + body)

save("triangle.glb", base)
multi = copy.deepcopy(base)
multi["meshes"][0]["primitives"].append({"attributes": {"POSITION": 0}})
multi["nodes"] = [{"translation": [0.1, 0.2, 0], "children": [1, 2]},
                  {"mesh": 0, "translation": [0.3, 0, 0]},
                  {"mesh": 0, "translation": [-0.3, 0, 0]}]
save("multi.glb", multi)
bad = copy.deepcopy(base)
bad["accessors"][0]["count"] = 18446744073709551615
save("overflow.glb", bad)
bad = copy.deepcopy(base)
bad["bufferViews"][1]["byteOffset"] = 1000
save("range.glb", bad)
save("index.glb", base, vertices + struct.pack("<3H", 0, 1, 99) + bytes(2))
save("nan.glb", base, struct.pack("<f", float("nan")) + binary[4:])
bad = copy.deepcopy(base)
bad["extensionsRequired"] = ["KHR_draco_mesh_compression"]
save("extension.glb", bad)
bad = copy.deepcopy(base)
bad["meshes"][0]["primitives"][0]["mode"] = 1
save("lines.glb", bad)
(root / "broken.glb").write_bytes(b"broken")
print(root)
