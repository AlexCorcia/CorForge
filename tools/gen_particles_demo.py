#!/usr/bin/env python3
"""Generate a DENSE particle showcase scene: lots of dungeon props each with their
own particle effect + light, spread across the floor, plus global weather + fog."""
import json, math, random

random.seed(7)
objs = []

def add(name, pos, comps, rot=(0,0,0), scale=(1,1,1)):
    objs.append({"name": name,
                 "transform": {"position": list(pos), "rotation": list(rot), "scale": list(scale)},
                 "components": comps})

def renderer(mesh):
    return {"type": "Renderer", "mesh": mesh, "modelSource": mesh}

def light(color, intensity, rng=9.0, t=1):
    return {"type": "Light", "lightType": t, "color": list(color),
            "intensity": intensity, "range": rng, "inner": 18.0, "outer": 26.0}

def parts(**kw):
    base = {"type": "Particles", "mode": 0, "enabled": True, "shape": 1,
            "emitDir": [0,1,0], "emitRadius": 0.2, "spread": 16.0, "rate": 120.0,
            "lifetime": 1.2, "lifetimeVar": 0.3, "startSpeed": 1.8, "speedVar": 0.5,
            "gravity": 1.0, "drag": 0.4, "swirl": 0.0, "attraction": 0.0,
            "startSize": 0.4, "endSize": 0.05, "startColor": [3,1.4,0.4,0.9],
            "endColor": [1.2,0.2,0.05,0.0], "blend": 1, "soft": True, "maxParticles": 1500}
    base.update(kw)
    return base

# --- camera / environment -------------------------------------------------
add("MainCamera", (16, 9, 20), [
    {"type": "Camera", "fov": 62.0, "near": 0.1, "far": 200.0},
    {"type": "FlyController", "moveSpeed": 8.0, "sensitivity": 0.1}], rot=(-18, -38, 0))
add("Night Sky", (0,0,0), [{"type": "Sky", "zenith": [0.02,0.03,0.08],
    "horizon": [0.14,0.12,0.16], "ground": [0.04,0.03,0.04], "intensity": 0.5, "image": "None"}])
add("Moonlight", (0,12,0), [light((0.35,0.42,0.6), 0.45, 40.0, 0)], rot=(-55,25,0))
# Floor bigger than the camera far plane (200) so its edge is never rasterised --
# the ground fills the whole lower frustum and just fades into the fog/horizon.
add("Floor", (0,-0.1,0), [{"type":"Renderer","mesh":"Cube","modelSource":""},
    {"type":"Material","albedo":[0.06,0.06,0.075],"ambient":0.08,"metallic":0.0,
     "roughness":0.7,"uvScale":[80,80]}], scale=(600,0.4,600))

# --- effect factories (each returns nothing; appends prop+fx+light) --------
def torch(x, z, i):
    add(f"Torch {i}", (x, 0.9, z), [renderer("column")], scale=(0.45,0.9,0.45))
    add(f"Torch {i} Flame", (x, 1.9, z), [parts(rate=55, lifetime=0.9, lifetimeVar=0.25,
        startSpeed=1.4, speedVar=0.4, spread=14, emitRadius=0.08, gravity=1.4, drag=0.4,
        startSize=0.3, endSize=0.03, startColor=[3,1.5,0.5,0.9], endColor=[1.3,0.3,0.06,0],
        maxParticles=600)])
    add(f"Torch {i} Light", (x, 2.0, z), [light((1.0,0.55,0.25), 5.0, 8.0)])

def campfire(x, z, i):
    add(f"Fire Pit {i}", (x, 0.3, z), [renderer("rocks")], scale=(0.9,0.6,0.9))
    add(f"Campfire {i}", (x, 0.45, z), [parts(rate=150, lifetime=1.1, lifetimeVar=0.3,
        startSpeed=1.8, speedVar=0.5, spread=16, emitRadius=0.24, gravity=1.6, drag=0.4,
        startSize=0.52, endSize=0.06, startColor=[3,1.4,0.4,0.9], endColor=[1.4,0.25,0.05,0],
        maxParticles=2000)])
    add(f"Campfire {i} Smoke", (x, 1.1, z), [parts(blend=0, rate=30, lifetime=3.0, lifetimeVar=0.6,
        startSpeed=1.2, speedVar=0.4, spread=22, emitRadius=0.25, gravity=0.6, drag=0.5,
        startSize=0.55, endSize=1.8, startColor=[0.22,0.22,0.25,0.42], endColor=[0.07,0.07,0.09,0],
        maxParticles=1200)])
    add(f"Campfire {i} Embers", (x, 0.5, z), [parts(rate=35, lifetime=2.4, lifetimeVar=0.6,
        startSpeed=1.6, speedVar=0.7, spread=35, emitRadius=0.4, gravity=0.8, drag=0.25, swirl=1.5,
        startSize=0.05, endSize=0.01, soft=False, startColor=[4,1.6,0.4,1], endColor=[1.5,0.3,0.05,0],
        maxParticles=700)])
    add(f"Campfire {i} Light", (x, 1.0, z), [light((1.0,0.5,0.2), 9.0, 12.0)])

def toxic(x, z, i):
    add(f"Barrel {i}", (x, 0.6, z), [renderer("barrel")], scale=(0.6,0.6,0.6))
    add(f"Toxic Smoke {i}", (x, 1.25, z), [parts(blend=0, rate=40, lifetime=2.6, lifetimeVar=0.5,
        startSpeed=1.0, speedVar=0.4, spread=18, emitRadius=0.16, gravity=0.5, drag=0.5,
        startSize=0.32, endSize=1.3, startColor=[0.32,0.75,0.2,0.5], endColor=[0.08,0.2,0.06,0],
        maxParticles=1000)])
    add(f"Toxic Light {i}", (x, 1.0, z), [light((0.4,1.0,0.3), 4.0, 8.0)])

def chest(x, z, i):
    add(f"Chest {i}", (x, 0.55, z), [renderer("chest")], rot=(0, random.uniform(-60,60), 0), scale=(0.6,0.6,0.6))
    add(f"Chest {i} Magic", (x, 1.0, z), [parts(shape=2, rate=70, lifetime=1.8, lifetimeVar=0.5,
        startSpeed=0.6, speedVar=0.3, spread=60, emitRadius=0.5, gravity=-0.3, drag=0.4, swirl=3.0,
        attraction=1.5, startSize=0.13, endSize=0.01, startColor=[0.5,2.0,2.6,0.9],
        endColor=[1.8,0.4,2.4,0], maxParticles=1200)])
    add(f"Chest {i} Glow", (x, 1.0, z), [light((0.3,0.7,1.0), 5.0, 7.0)])

def portal(x, z, i):
    add(f"Portal {i}", (x, 2.3, z), [parts(shape=4, emitDir=[0,0,1], emitRadius=1.1, spread=8,
        rate=320, lifetime=1.5, lifetimeVar=0.3, startSpeed=2.2, speedVar=0.4, gravity=0.0,
        drag=0.7, swirl=12.0, attraction=3.5, startSize=0.18, endSize=0.02,
        startColor=[1.8,0.5,3.2,0.9], endColor=[0.3,0.9,2.4,0], maxParticles=3000)])
    add(f"Portal {i} Glow", (x, 2.3, z), [light((0.6,0.3,1.0), 6.0, 9.0)])

def bolt(x, z, i):
    add(f"Lightning {i}", (x, 6.0, z), [parts(mode=1, emitDir=[0,-1,0], boltLength=5.4,
        boltSegments=16, boltJitter=0.55, flickerHz=12.0+random.uniform(-3,3), startSize=0.12,
        startColor=[2.2,2.6,5.0,1.0], endColor=[1.0,1.4,3.0,0], soft=False, maxParticles=600)])
    add(f"Lightning {i} Glow", (x, 3.0, z), [light((0.5,0.6,1.0), 4.0, 11.0)])

def brazier(x, z, i):
    add(f"Brazier {i}", (x, 0.6, z), [renderer("rocks")], scale=(0.5,0.4,0.5))
    add(f"Brazier {i} Embers", (x, 0.9, z), [parts(rate=50, lifetime=2.2, lifetimeVar=0.6,
        startSpeed=2.0, speedVar=0.8, spread=30, emitRadius=0.25, gravity=1.0, drag=0.25, swirl=2.0,
        startSize=0.06, endSize=0.01, soft=False, startColor=[4,2.0,0.5,1], endColor=[2,0.5,0.1,0],
        maxParticles=900)])
    add(f"Brazier {i} Light", (x, 1.0, z), [light((1.0,0.6,0.25), 6.0, 9.0)])

# --- perimeter ring of torches -------------------------------------------
ring = 22
for i, ang in enumerate(range(0, 360, 30)):
    a = math.radians(ang)
    torch(round(ring*math.cos(a),2), round(ring*math.sin(a),2), i)

# --- inner grid of varied stations ---------------------------------------
makers = [campfire, toxic, chest, portal, brazier, campfire, toxic, brazier]
idx = 0
for gx in range(-2, 3):
    for gz in range(-2, 3):
        if gx == 0 and gz == 0:
            campfire(0, 0, 99)  # big central campfire
            continue
        x, z = gx*7.0 + random.uniform(-1.2,1.2), gz*7.0 + random.uniform(-1.2,1.2)
        random.choice(makers)(round(x,2), round(z,2), idx)
        idx += 1

# --- a few portals + lightning at fixed dramatic spots -------------------
portal(0, -10, 100); portal(-10, 6, 101); portal(11, -4, 102)
bolt(-8, 8, 200); bolt(9, 9, 201); bolt(-12, -8, 202); bolt(13, 3, 203); bolt(2, 13, 204)

# --- global weather ------------------------------------------------------
add("Snowfall", (0, 12, 0), [parts(blend=0, shape=1, emitDir=[0,-1,0], emitRadius=24, spread=8,
    rate=900, lifetime=7.0, lifetimeVar=1.0, startSpeed=1.2, speedVar=0.5, gravity=-0.6, drag=0.3,
    swirl=1.0, startSize=0.09, endSize=0.07, startColor=[1,1,1,0.9], endColor=[0.9,0.95,1,0.4],
    maxParticles=8000)])
for k, (mx, mz) in enumerate([(-10,-10),(10,-10),(-10,10),(10,10),(0,0)]):
    add(f"Mist {k}", (mx, 0.4, mz), [parts(blend=0, shape=1, emitDir=[0,1,0], emitRadius=9,
        spread=80, rate=26, lifetime=7.0, lifetimeVar=1.5, startSpeed=0.3, speedVar=0.2, gravity=0.0,
        drag=0.8, swirl=0.4, startSize=2.5, endSize=4.0, startColor=[0.5,0.55,0.62,0.13],
        endColor=[0.45,0.5,0.58,0], maxParticles=700)])

root = {"objects": objs, "post": {
    "enabled": True, "exposure": 1.0, "vignette": 0.34,
    "bloom": True, "bloomThreshold": 0.85, "bloomIntensity": 0.8,
    "ssao": True, "ssaoRadius": 0.6, "ssaoStrength": 1.2,
    "fog": True, "fogColor": [0.05, 0.07, 0.13], "fogDensity": 0.035}}

import os
out = os.path.join(os.path.dirname(__file__), "..", "assets", "scenes", "ParticlesDemo.json")
with open(out, "w") as f:
    json.dump(root, f, indent=2)
emitters = sum(1 for o in objs for c in o["components"] if c["type"] == "Particles")
print(f"wrote {len(objs)} objects, {emitters} particle emitters -> {os.path.abspath(out)}")
