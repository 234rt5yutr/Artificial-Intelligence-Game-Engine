#!/usr/bin/env python3
"""Renderer verification sweep, driven over MCP against a running engine.

Start the engine, then run this. It places the camera, builds a scene with
several objects, and checks each renderer feature end to end from captured
frames rather than from the fact that a pass reported itself active.

Every check frames at least two objects on purpose. An instance-layout bug -
every instance after the first reading its transform 16 bytes off - survived
eight rounds of hand checks because each one had a single visible object, and
with one object there is nothing for a wrong transform to disagree with.
"""

import json, subprocess, time, struct, zlib, os

URL = "http://127.0.0.1:3000"
ROOT = r"C:\Users\david\Desktop\Projects\Artificial-Intelligence-Game-Engine"
results = []


def call(name, args, cid=[0]):
    cid[0] += 1
    payload = json.dumps({"jsonrpc": "2.0", "id": cid[0], "method": "tools/call",
                          "params": {"name": name, "arguments": args}})
    out = subprocess.run(["curl", "-s", "-X", "POST", URL,
                          "-H", "Content-Type: application/json", "-d", payload],
                         capture_output=True, text=True).stdout
    d = json.loads(out)
    if "result" not in d:
        return {"error": d.get("error")}
    for c in d["result"]["content"]:
        try:
            return json.loads(c["text"])
        except Exception:
            continue
    return {}


def read_png(path):
    d = open(os.path.join(ROOT, path), "rb").read()
    pos, idat, w, h = 8, b"", None, None
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        t = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if t == b"IHDR":
            w, h = struct.unpack(">II", data[:8])
        elif t == b"IDAT":
            idat += data
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * 3
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i + stride]); i += stride
        if f:
            for x in range(stride):
                a = line[x - 3] if x >= 3 else 0
                b = prev[x]
                cc = prev[x - 3] if x >= 3 else 0
                if f == 1: line[x] = (line[x] + a) & 255
                elif f == 2: line[x] = (line[x] + b) & 255
                elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
                elif f == 4:
                    p = a + b - cc
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - cc)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else cc)
                    line[x] = (line[x] + pr) & 255
        out += line
        prev = line
    return w, h, bytes(out)


def capture(tag):
    call("CaptureFrame", {"path": "captures/sw_%s.png" % tag})
    return read_png("captures/sw_%s.png" % tag)


def lit(pix):
    return sum(1 for i in range(0, len(pix), 3) if max(pix[i:i + 3]) > 8)


def changed(a, b, threshold=4):
    return sum(1 for i in range(0, len(a), 3)
               if max(abs(a[i + k] - b[i + k]) for k in range(3)) > threshold)


def blobs(pix, w, h):
    """Count separated lit column runs - a cheap stand-in for 'distinct objects'
    that does not need a full connected-components pass."""
    cols = [any(max(pix[(y * w + x) * 3:(y * w + x) * 3 + 3]) > 8 for y in range(0, h, 4))
            for x in range(w)]
    runs, inside = 0, False
    for c in cols:
        if c and not inside:
            runs += 1
        inside = c
    return runs


def record(name, ok, detail):
    results.append((name, ok, detail))
    print("%-26s %s  %s" % (name, "PASS" if ok else "FAIL", detail))


def graph(name, colour, metallic=0.0, roughness=0.4, opacity=1.0):
    return {"name": name,
            "nodes": [{"id": 1, "type": "ConstantColor", "name": "BaseColor",
                       "value": [colour[0], colour[1], colour[2], 1.0]},
                      {"id": 2, "type": "ConstantScalar", "name": "Roughness",
                       "value": [roughness, 0, 0, 0]},
                      {"id": 3, "type": "ConstantScalar", "name": "Metallic",
                       "value": [metallic, 0, 0, 0]},
                      {"id": 4, "type": "ConstantScalar", "name": "Opacity",
                       "value": [opacity, 0, 0, 0]},
                      {"id": 5, "type": "Output", "name": "Output", "value": [0, 0, 0, 0]}],
            "links": [{"from": 1, "to": 5, "slot": 0}, {"from": 3, "to": 5, "slot": 1},
                      {"from": 2, "to": 5, "slot": 2}, {"from": 4, "to": 5, "slot": 5}]}


os.chdir(ROOT)
call("SetPostProcess", {"taa": False, "bloom": False})
call("SetEditorViewport", {"cameraPosition": [0, 0, 12], "cameraTarget": [0, 0, 0]})
call("SpawnEntity", {"name": "Sun", "template": "light", "transform": {"rotation": [0, 0, 0]},
                     "components": {"light": {"type": "directional", "intensity": 3.0}}})

# --- 1. several instances draw at their own positions -----------------------
for i, x in enumerate((-4.0, 0.0, 4.0)):
    call("SpawnEntity", {"name": "Row%d" % i, "template": "mesh",
                         "transform": {"position": [x, 0, 0]},
                         "components": {"mesh": {"primitive": "sphere", "subdivisions": 32}}})
time.sleep(4)
w, h, rowShot = capture("rows")
n = blobs(rowShot, w, h)
record("multi-instance placement", n == 3, "%d separated blobs, expected 3" % n)

# --- 2. level of detail ------------------------------------------------------
call("SetGPUCulling", {"lod": True, "lodThresholds": [220, 70]})
time.sleep(2)
withLod = call("GetRenderStats", {})["gpuScene"]
call("SetGPUCulling", {"lod": False})
time.sleep(2)
without = call("GetRenderStats", {})["gpuScene"]
record("lod reduces triangles",
       withLod["frameTriangles"] <= without["frameTriangles"],
       "%d with lod vs %d without (lod0 total %d)"
       % (withLod["frameTriangles"], without["frameTriangles"], without["frameTrianglesAtLod0"]))
call("SetGPUCulling", {"lod": True})

# --- 3. per-submesh materials ------------------------------------------------
before = call("GetRenderStats", {})["gpuScene"]["materialBatches"]
call("LoadMesh", {"path": "assets/meshes/two_materials.gltf", "name": "Quad",
                  "position": [0, 3, -2], "scale": 2.0})
time.sleep(3)
after = call("GetRenderStats", {})["gpuScene"]
record("per-submesh materials", after["materialBatches"] >= before + 2,
       "%d batches, was %d before the two-material mesh" % (after["materialBatches"], before))

# --- 4. rigged import and clip playback --------------------------------------
info = call("LoadMesh", {"path": "assets/meshes/rigged_strip.gltf", "name": "Rig",
                         "position": [-2, 0, 4], "scale": 2.0})
time.sleep(3)
skin = call("GetRenderStats", {})["skinning"]
_, _, a = capture("rig_a")
time.sleep(0.5)
_, _, b = capture("rig_b")
moved = changed(a, b, 6)
record("skinning drives geometry",
       skin["instances"] >= 1 and skin["vertices"] > 0 and moved > 200,
       "%d instances, %d vertices, %d pixels move between frames"
       % (skin["instances"], skin["vertices"], moved))

# --- 5. blended transparency --------------------------------------------------
# Indirect light accumulates over frames, and this check compares three
# captures taken minutes apart. Any drift between them lands inside the
# betweenness bound and reads as a blending failure, so it is held still here.
call("SetGlobalIllumination", {"enabled": False})
pane = call("SetMaterialGraph", {"name": "Pane", "graph": graph("Pane", [0.2, 0.4, 1.0], 0.0, 0.3, 0.5),
                                 "alphaMode": "blend", "doubleSided": True})
call("SpawnEntity", {"name": "Pane", "template": "mesh",
                     "transform": {"position": [4.0, 0, 5], "scale": [1.5, 1.5, 1.5]},
                     "components": {"mesh": {"primitive": "sphere", "subdivisions": 24,
                                             "materialIndex": pane["index"]}}})
time.sleep(3)
scene = call("GetRenderStats", {})["gpuScene"]
_, _, withPane = capture("pane_on")
call("SetMaterialGraph", {"name": "Pane", "graph": graph("Pane", [0.2, 0.4, 1.0], 0.0, 0.3, 0.0),
                          "alphaMode": "blend", "doubleSided": True})
time.sleep(3)
_, _, withoutPane = capture("pane_off")
call("SetMaterialGraph", {"name": "Pane", "graph": graph("Pane", [0.2, 0.4, 1.0], 0.0, 0.3, 1.0),
                          "alphaMode": "opaque", "doubleSided": True})
time.sleep(3)
_, _, opaquePane = capture("pane_opaque")
between = total = 0
for i in range(0, len(withPane), 3):
    if max(abs(opaquePane[i + k] - withoutPane[i + k]) for k in range(3)) < 24:
        continue
    total += 1
    if all(min(opaquePane[i + k], withoutPane[i + k]) - 3 <= withPane[i + k]
           <= max(opaquePane[i + k], withoutPane[i + k]) + 3 for k in range(3)):
        between += 1
record("blended transparency",
       scene["transparentInstances"] >= 1 and total > 100 and between > total * 0.9,
       "%d/%d covered pixels lie between opaque and background" % (between, total))

call("SetGlobalIllumination", {"enabled": True})

# --- 6. screen-space reflections ---------------------------------------------
chrome = call("SetMaterialGraph", {"name": "Chrome", "graph": graph("Chrome", [0.9, 0.9, 0.9], 1.0, 0.05)})
for i, pos in enumerate(([-1.5, -1.5, 6], [1.5, -1.5, 6], [0, -2.5, 4])):
    call("SpawnEntity", {"name": "Mirror%d" % i, "template": "mesh",
                         "transform": {"position": pos},
                         "components": {"mesh": {"primitive": "sphere", "subdivisions": 32,
                                                 "materialIndex": chrome["index"]}}})
call("SetGlobalIllumination", {"enabled": False})
call("SetPostProcess", {"ssr": True, "ssrIntensity": 1.0, "ssrSteps": 48})
time.sleep(4)
_, _, ssrOn = capture("ssr_on")
call("SetPostProcess", {"ssr": False})
time.sleep(3)
_, _, ssrOff = capture("ssr_off")
delta = changed(ssrOn, ssrOff, 3)
brighter = sum(1 for i in range(0, len(ssrOn), 3)
               if max(abs(ssrOn[i + k] - ssrOff[i + k]) for k in range(3)) > 3
               and sum(ssrOn[i:i + 3]) > sum(ssrOff[i:i + 3]))
# Reflections replace the environment specular term rather than adding to it,
# so reflecting something darker than the sky legitimately darkens a surface.
# "Changed" is the claim; "brighter" stopped being one when that landed.
record("screen-space reflections", delta > 500,
       "%d pixels change, %d brighter and %d darker" % (delta, brighter, delta - brighter))

# --- 7. environment specular --------------------------------------------------
call("SetGlobalIllumination", {"skyColor": [0.05, 0.05, 0.05], "skyIntensity": 1.0})
time.sleep(3)
_, _, dark = capture("sky_dark")
call("SetGlobalIllumination", {"skyColor": [0.2, 0.5, 1.0], "skyIntensity": 3.0})
time.sleep(3)
_, _, bright = capture("sky_bright")
skyDelta = changed(bright, dark, 4)
bluer = sum(1 for i in range(0, len(dark), 3)
            if max(abs(bright[i + k] - dark[i + k]) for k in range(3)) > 4
            and (bright[i + 2] - dark[i + 2]) > (bright[i] - dark[i]))
record("environment specular", skyDelta > 500 and bluer > skyDelta * 0.8,
       "%d pixels track sky colour, %d gain more blue than red" % (skyDelta, bluer))

# --- 8. depth of field ---------------------------------------------------------
call("SetGlobalIllumination", {"enabled": True})
call("SetPostProcess", {"dof": False})
# Global illumination accumulates over frames. Capturing before it settles makes
# the reference frame darker than the one it is compared against.
time.sleep(8)
w, h, sharp = capture("dof_sharp")
call("SetPostProcess", {"dof": True, "dofFocalDistance": 6.0, "dofFocalRange": 4.0,
                        "dofMaxBlur": 2.5})
time.sleep(3)
_, _, blurred = capture("dof_blur")


def edge_strength(pix):
    """The strongest horizontal gradients in the frame. Defocus flattens
    silhouettes; a mean over every pixel mostly measures temporal noise."""
    grads = []
    for y in range(0, h, 2):
        row = y * w * 3
        for x in range(w - 1):
            i = row + x * 3
            grads.append(abs(pix[i] - pix[i + 3]))
    grads.sort()
    top = grads[int(len(grads) * 0.999):]
    return sum(top) / max(len(top), 1)


sharpDetail, blurDetail = edge_strength(sharp), edge_strength(blurred)
record("depth of field blurs", blurDetail < sharpDetail * 0.95,
       "edge strength %.2f sharp vs %.2f defocused" % (sharpDetail, blurDetail))

print()
failed = [n for n, ok, _ in results if not ok]
print("%d/%d checks passed" % (len(results) - len(failed), len(results)))
if failed:
    print("FAILED:", ", ".join(failed))
