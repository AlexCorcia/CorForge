#!/usr/bin/env python3
"""Generate DonutDemo.json: a donut/atoll terrain (ring of land, water in the
middle) with 50 balls of random size + colour raining down onto it."""
import colorsys
import json
import os
import random

random.seed(2026)

WATER_Y = 3.0
HALF = 40.0  # terrain half-extent (size 80)


def v3(x, y, z):
    return [x, y, z]


def renderer(mesh):
    return {"type": "Renderer", "mesh": mesh, "modelPart": -1, "modelSource": ""}


def material(col, rough=0.45):
    return {
        "type": "Material", "albedo": list(col), "albedoMap": "None", "normalMap": "None",
        "ambient": 0.12, "metallic": 0.0, "roughness": rough, "opacity": 1.0,
        "envSpecular": 1.0, "reflective": False, "reflectBox": False, "reflectPlanar": False,
        "reflectivity": 0.6, "transparent": False, "uvScale": [1.0, 1.0],
    }


def obj(name, comps, pos, rot=(0, 0, 0), scale=(1, 1, 1)):
    return {
        "name": name, "components": comps,
        "transform": {"position": v3(*pos), "rotation": v3(*rot), "scale": v3(*scale)},
    }


objects = []

# --- Camera (high, looking down at the whole atoll) -------------------------
objects.append(obj("MainCamera", [
    {"type": "Camera", "fov": 60.0, "near": 0.1, "far": 500.0},
    {"type": "FlyController", "moveSpeed": 20.0, "sensitivity": 0.1},
], pos=(0.0, 72.0, 82.0), rot=(-38.0, 0.0, 0.0)))

# --- Sky --------------------------------------------------------------------
objects.append(obj("Sky", [{
    "type": "Sky", "image": "",
    "zenith": [0.23, 0.42, 0.71], "horizon": [0.73, 0.82, 0.91],
    "ground": [0.2, 0.23, 0.27], "intensity": 1.1,
}], pos=(0, 0, 0)))

# --- Sun --------------------------------------------------------------------
objects.append(obj("Sun", [{
    "type": "Light", "lightType": 0, "color": [1.0, 0.96, 0.87],
    "intensity": 2.4, "range": 50.0, "inner": 18.0, "outer": 26.0,
}], pos=(0.0, 40.0, 0.0), rot=(-52.0, 35.0, 0.0)))

# --- Donut terrain ----------------------------------------------------------
objects.append(obj("Terrain", [
    renderer("Terrain"),
    material([0.27, 0.54, 0.23], rough=0.5),
    {
        "type": "Terrain", "size": 2 * HALF, "resolution": 130, "heightScale": 15.0,
        "frequency": 0.05, "octaves": 5, "lacunarity": 2.0, "gain": 0.5, "seed": 7,
        "island": False, "edgeFalloff": 0.3,
        "donut": True, "ringRadius": 0.56, "ringWidth": 0.3,
        "lowPoly": True, "slab": True, "baseDepth": 9.0,
        "grassColor": [0.27, 0.54, 0.23],
        "scatter": True, "treeCount": 60, "rockCount": 18, "propScale": 1.1,
        "forestScale": 10.0, "treeMinHeight": WATER_Y + 1.5, "waterLevel": WATER_Y,
        "treeColor": [0.18, 0.42, 0.18], "collider": True,
    },
], pos=(0.0, 0.0, 0.0)))

# --- Water (fills the lagoon in the middle AND the sea outside) --------------
objects.append(obj("Water", [
    renderer("Plane"),
    {**material([0.08, 0.36, 0.63], rough=0.3), "opacity": 0.85, "transparent": True},
    {"type": "Water", "color": [0.08, 0.36, 0.63], "opacity": 0.85, "round": False,
     "calm": 0.25, "splash": True, "splashThreshold": 1.4, "splashAmount": 1.0},
], pos=(0.0, WATER_Y, 0.0), scale=(220.0, 1.0, 220.0)))

# --- 50 balls raining down, random size + colour ----------------------------
for i in range(50):
    s = round(random.uniform(0.5, 2.3), 2)            # random size
    h, sat, val = random.random(), random.uniform(0.6, 0.95), random.uniform(0.75, 1.0)
    col = [round(c, 3) for c in colorsys.hsv_to_rgb(h, sat, val)]  # vibrant random colour
    px = round(random.uniform(-HALF + 4, HALF - 4), 2)
    pz = round(random.uniform(-HALF + 4, HALF - 4), 2)
    py = round(random.uniform(18.0, 46.0), 2)         # staggered heights in the air
    mass = round(max(0.4, s * s), 2)
    objects.append(obj(f"Ball {i+1}", [
        renderer("Sphere"),
        material(col, rough=round(random.uniform(0.25, 0.6), 2)),
        {"type": "Rigidbody", "mass": mass, "gravity": True, "static": False,
         "restitution": round(random.uniform(0.25, 0.6), 2),
         "friction": 0.5, "freezeRotation": False},
        {"type": "Collider", "shape": 1, "radius": 0.6,
         "halfExtents": [0.5, 0.5, 0.5], "center": [0.0, 0.0, 0.0], "planeNormal": [0.0, 1.0, 0.0]},
        {"type": "Buoyancy", "strength": 22.0, "drag": 2.5},
    ], pos=(px, py, pz), rot=(0, round(random.uniform(0, 360), 1), 0), scale=(s, s, s)))

scene = {
    "objects": objects,
    "post": {
        "enabled": True, "exposure": 1.0, "vignette": 0.25,
        "bloom": True, "bloomThreshold": 1.0, "bloomIntensity": 0.55,
        "ssao": True, "ssaoRadius": 0.7, "ssaoStrength": 1.0,
        "fog": False, "fogColor": [0.66, 0.74, 0.84], "fogDensity": 0.005,
    },
}

out = os.path.join(os.path.dirname(__file__), "..", "assets", "scenes", "DonutDemo.json")
with open(os.path.abspath(out), "w") as f:
    json.dump(scene, f, indent=2)
print("wrote", os.path.abspath(out), "with", len(objects), "objects")
