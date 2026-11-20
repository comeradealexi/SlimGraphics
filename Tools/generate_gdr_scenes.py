"""Generates the GPU driven rendering test scenes in SlimGraphicsAssets/GDRAssets.

MixedAssetsTest.json exercises frustum, cone and HZB occlusion culling, deep hierarchies, instancing, multi part meshes,
non uniform scales, arbitrary rotations and per instance colours.

Usage: python Tools/generate_gdr_scenes.py
The output is deterministic (fixed random seed), so regenerating without changes gives identical files.

Transform model (gdrScene.cpp SceneGraphTraverser): node matrix = Scale * RotationRollPitchYaw(deg x, y, z) * Translation
(row vectors), world = local * parent world, colour multiplies down the tree. Models are imported with
aiProcess_ConvertToLeftHanded, which negates Z. Meshes are placed by their measured OBJ bounds, so placement does not
depend on where each file puts its origin.
"""
import json, math, os, random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(ROOT, "SlimGraphicsAssets")
OUT_DIR = os.path.join(ASSETS_DIR, "GDRAssets")
random.seed(1234)  # Deterministic output


def measure_obj(path):
	"""Bounds and triangle count of an OBJ file."""
	mn = [float("inf")] * 3
	mx = [float("-inf")] * 3
	tris = 0
	with open(path, "r", errors="ignore") as f:
		for line in f:
			if line.startswith("v "):
				parts = line.split()
				try:
					v = [float(parts[1]), float(parts[2]), float(parts[3])]
				except (ValueError, IndexError):
					continue
				for i in range(3):
					mn[i] = min(mn[i], v[i])
					mx[i] = max(mx[i], v[i])
			elif line.startswith("f "):
				tris += max(1, len(line.split()) - 3)
	return {"min": mn, "max": mx, "size": [mx[i] - mn[i] for i in range(3)], "tris": tris}


class Bounds(dict):
	"""Measures meshes on first use (keyed by path relative to SlimGraphicsAssets, forward slashes)."""
	def __missing__(self, key):
		value = measure_obj(os.path.join(ASSETS_DIR, key))
		self[key] = value
		return value

bounds = Bounds()

MODEL_PREFIX = "../SlimGraphicsAssets\\"
INVALID = "unknown"


# --- Row vector maths matching DirectXMath -------------------------------------------------------------------------
def mat_mul(a, b):
	return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]

def vec_mul(v, m):
	return [sum(v[k] * m[k][j] for k in range(3)) for j in range(3)]

def rotation(pitch_deg, yaw_deg, roll_deg):
	# XMMatrixRotationRollPitchYaw: roll (z), then pitch (x), then yaw (y)
	p, y, r = (math.radians(a) for a in (pitch_deg, yaw_deg, roll_deg))
	rz = [[math.cos(r), math.sin(r), 0], [-math.sin(r), math.cos(r), 0], [0, 0, 1]]
	rx = [[1, 0, 0], [0, math.cos(p), math.sin(p)], [0, -math.sin(p), math.cos(p)]]
	ry = [[math.cos(y), 0, -math.sin(y)], [0, 1, 0], [math.sin(y), 0, math.cos(y)]]
	return mat_mul(mat_mul(rz, rx), ry)

def scaling(s):
	return [[s[0], 0, 0], [0, s[1], 0], [0, 0, s[2]]]


# --- Scene nodes ------------------------------------------------------------------------------------------------------
def f3(v):
	return {"x": round(float(v[0]), 4), "y": round(float(v[1]), 4), "z": round(float(v[2]), 4)}

def node(model=INVALID, position=(0, 0, 0), rotation_deg=(0, 0, 0), scale=(1, 1, 1), colour=(1, 1, 1), children=None):
	return {"children": children or [], "colour": f3(colour), "model_name": model if model == INVALID else MODEL_PREFIX + model,
	        "position": f3(position), "rotation": f3(rotation_deg), "scale": f3(scale)}

# Meshes: file, whether the file is Z up, and the size (largest extent after orientation) a scale of 1 gives
MESHES = {
	"cube": ("DebugModels\\cube.obj", False),
	"cone": ("DebugModels\\cone.obj", False),
	"icosphere": ("DebugModels\\icosphere.obj", False),
	"torus": ("DebugModels\\torus.obj", False),
	"suzanne": ("DebugModels\\suzanne.obj", False),
	"column": ("DebugModels\\column.obj", False),
	"teapot": ("DebugModels\\teapot.obj", False),
	"cow": ("DebugModels\\cow.obj", False),
	"hollowcube": ("DebugModels\\hollowcube.obj", False),
	"sphere_lo": ("Blender-LODSpheres\\Sphere16x16.obj", False),
	"sphere_hi": ("Blender-LODSpheres\\Sphere64x64.obj", False),
	"bunny": ("GL\\bunny.obj", False),
	"elephant": ("GL\\elephant.obj", False),
	"palm": ("Nvidia\\palm_tree.obj", False),
	"tree": ("DebugModels\\tree.obj", False),
	"lowpoly_tree": ("3DBuilder\\Tree.obj", True),
	"bulldozer": ("3DBuilder\\Bulldozer.obj", True),
	"dragon": ("Nvidia\\dragon.obj", False),
	"armadillo": ("Nvidia\\armadillo.obj", False),
	"head": ("CGArchive\\lpshead\\head.OBJ", False),
	"buddha": ("Nvidia\\buddha.obj", False),
	"tank": ("Nvidia\\T34-85.obj", False),
	"cat": ("3DBuilder\\Cat.obj", True),
	"pawn": ("GL\\ChessPawn.obj", False),
	"three_objects": ("GL\\three_objects.obj", False),
	"venus": ("GL\\venusm.obj", False),
	"hires_bunny": ("DebugModels\\stanford-bunny.obj", False),
}

def bounds_key(file):
	return file.replace("\\", "/")

def model_space_extent(name):
	"""Largest extent of the mesh once upright (in file units)."""
	file, z_up = MESHES[name]
	return max(bounds[bounds_key(file)]["size"])

def mesh(name, height=None, size=None, scale_xyz=(1, 1, 1), colour=(1, 1, 1), pivot="bottom"):
	"""A mesh node normalised so its pivot (bottom or centre) sits at the parent's origin, upright, scaled so its height (or
	largest extent with 'size') matches. scale_xyz adds a non uniform scale in the upright frame."""
	file, z_up = MESHES[name]
	b = bounds[bounds_key(file)]
	mn, mx = b["min"], b["max"]
	# Left handed import negates Z
	lh_min = [mn[0], mn[1], -mx[2]]
	lh_max = [mx[0], mx[1], -mn[2]]
	centre = [(lh_min[i] + lh_max[i]) / 2 for i in range(3)]
	# Upright orientation: Z up files have their up axis along -Z after the import
	fix = (90, 0, 0) if z_up else (0, 0, 0)
	up_axis = 2 if z_up else 1
	up_size = lh_max[up_axis] - lh_min[up_axis]
	pivot_lh = list(centre)
	if pivot == "bottom":
		pivot_lh[up_axis] = lh_max[up_axis] if z_up else lh_min[up_axis]  # Z up: -Z is up, so the bottom is the largest z
	if height is not None:
		uniform = height / up_size
	else:
		uniform = size / max(b["size"])
	# Non uniform scale is specified in the upright frame; map it to the model frame
	s_up = [scale_xyz[0], scale_xyz[1], scale_xyz[2]]
	s_model = [s_up[0], s_up[2], s_up[1]] if z_up else s_up
	s = [uniform * s_model[i] for i in range(3)]
	m = mat_mul(scaling(s), rotation(*fix))
	offset = vec_mul(pivot_lh, m)
	return node(file, position=[-o for o in offset], rotation_deg=fix, scale=s, colour=colour)

def place(child, position=(0, 0, 0), rotation_deg=(0, 0, 0), colour=(1, 1, 1), scale=(1, 1, 1), children=None):
	return node(position=position, rotation_deg=rotation_deg, colour=colour, scale=scale, children=[child] + (children or []))

def group(children, position=(0, 0, 0), rotation_deg=(0, 0, 0), colour=(1, 1, 1), scale=(1, 1, 1)):
	return node(position=position, rotation_deg=rotation_deg, colour=colour, scale=scale, children=children)

def hsv(h, s=0.65, v=0.95):
	h = (h % 1.0) * 6
	i = int(h); f = h - i
	p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
	return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i % 6]

def jitter(a):
	return random.uniform(-a, a)


zones = []

# A. Colonnade: two rows of the same mesh (instancing), alternating marble tints, slight random lean
colonnade = []
for side in (-1, 1):
	for i in range(11):
		z = 4 + i * 3
		tint = (0.95, 0.93, 0.88) if i % 2 == 0 else (0.80, 0.82, 0.86)
		colonnade.append(place(mesh("column", height=1.8, colour=tint), position=(side * 2.6, 0, z), rotation_deg=(jitter(1.5), random.uniform(0, 90), jitter(1.5))))
zones.append(group(colonnade))

# B. Plinth with a golden dragon, a rotating ring of bunnies facing outwards and tiny Suzannes (sub pixel meshlets)
ring = []
for i in range(8):
	angle = i * 45
	a = math.radians(angle)
	ring.append(place(mesh("bunny", height=0.8, colour=hsv(i / 8)), position=(math.sin(a) * 2.2, 0, math.cos(a) * 2.2), rotation_deg=(0, angle + 180, 0)))
tiny = []
for i in range(16):
	tiny.append(place(mesh("suzanne", size=random.uniform(0.04, 0.09), colour=hsv(random.random(), 0.8)), position=(jitter(1.4), 0.61, jitter(1.4)), rotation_deg=(jitter(30), random.uniform(0, 360), jitter(30))))
plinth = group([
	place(mesh("cube", height=0.6, scale_xyz=(4.0, 1, 4.0), colour=(0.35, 0.33, 0.32))),
	place(mesh("dragon", size=1.8, colour=(1.0, 0.78, 0.25)), position=(0, 0.6, 0), rotation_deg=(0, 35, 0)),
	group(tiny),
	group(ring, rotation_deg=(0, 15, 0)),
], position=(0, 0, 10))
zones.append(plinth)

# C. Solar system: deep hierarchy (system > orbit > planet > moon orbit > moon) with tilted orbits, colour multiplication and
# a ringed planet with a non uniformly scaled torus
def planet(distance, radius, colour, orbit_angle, tilt, moons, ring_colour=None):
	children = [place(mesh("sphere_hi" if radius > 0.45 else "sphere_lo", size=radius * 2, pivot="centre"), colour=colour)]
	if ring_colour:
		children.append(place(mesh("torus", size=radius * 4.2, scale_xyz=(1, 0.25, 1), pivot="centre", colour=ring_colour), rotation_deg=(25, 0, 10)))
	for m, (m_distance, m_radius, m_angle, m_tilt) in enumerate(moons):
		moon = place(mesh("icosphere", size=m_radius * 2, pivot="centre"), position=(m_distance, 0, 0), colour=(0.85, 0.85, 0.9))
		children.append(group([moon], rotation_deg=(m_tilt, m_angle, 0)))
	body = group(children, position=(distance, 0, 0))
	return group([body], rotation_deg=(tilt, orbit_angle, 0))

solar = group([
	place(mesh("sphere_hi", size=2.0, pivot="centre"), colour=(1.0, 0.85, 0.35)),
	planet(2.0, 0.25, (0.7, 0.6, 0.5), 30, 4, []),
	planet(3.2, 0.4, (0.3, 0.5, 1.0), 140, -6, [(0.75, 0.1, 40, 20)]),
	planet(4.6, 0.6, (0.9, 0.55, 0.3), 250, 8, [(1.0, 0.12, 0, 0), (1.3, 0.09, 120, 30), (1.6, 0.15, 230, -25)]),
	planet(6.2, 0.55, (0.95, 0.85, 0.6), 320, -10, [(1.6, 0.12, 300, 15)], ring_colour=(0.85, 0.8, 0.65)),
], position=(0, 5.0, 20), rotation_deg=(-12, 20, 0))
zones.append(solar)

# D. Spiral staircase: a 20 level parent chain (each step rotates and rises relative to the previous one) with a Suzanne on top
def staircase(levels):
	top = place(mesh("suzanne", height=0.5, colour=(0.95, 0.4, 0.4)), position=(0.6, 0.12, 0), rotation_deg=(20, 90, 0))
	chain = None
	for level in reversed(range(levels)):
		step_children = [place(mesh("cube", height=0.1, scale_xyz=(9.0, 1, 3.5), colour=hsv(level / levels, 0.5, 0.9)), position=(0.55, 0, 0))]
		if chain is not None:
			step_children.append(chain)
		elif top is not None:
			step_children.append(top)
		chain = group(step_children, position=(0, 0.22 if level > 0 else 0, 0), rotation_deg=(0, 22.5 if level > 0 else 0, 0))
	return chain
zones.append(group([staircase(20), place(mesh("column", height=4.6, scale_xyz=(0.25, 1, 0.25), colour=(0.5, 0.5, 0.55)))], position=(-6.0, 0, 15)))

# E. Occlusion wall with a dense crowd behind it (mostly hidden: HZB occlusion), a few tall statues peeking over
wall = place(mesh("cube", height=3.0, scale_xyz=(14.0 / 3.0, 1, 0.3 / 3.0), colour=(0.55, 0.45, 0.4)), position=(0, 0, 26))
crowd = []
crowd_meshes = ["head", "armadillo", "buddha", "hires_bunny", "teapot", "elephant"]
for row in range(4):
	for col in range(7):
		name = crowd_meshes[(row * 7 + col) % len(crowd_meshes)]
		height = {"head": 1.1, "armadillo": 1.4, "buddha": 1.6, "hires_bunny": 1.1, "teapot": 0.9, "elephant": 1.8}[name]
		crowd.append(place(mesh(name, height=height, colour=hsv((row * 7 + col) / 28.0, 0.55)), position=(-5.4 + col * 1.8 + jitter(0.3), 0, 28 + row * 2.0 + jitter(0.3)), rotation_deg=(0, random.uniform(0, 360), 0)))
statues = [
	# Statues standing on top of the wall
	place(mesh("venus", height=2.6, colour=(0.92, 0.9, 0.85)), position=(-5.2, 3.0, 26), rotation_deg=(0, 160, 0)),
	place(mesh("venus", height=2.2, colour=(0.75, 0.82, 0.92)), position=(5.2, 3.0, 26), rotation_deg=(0, 200, 0)),
	place(mesh("buddha", height=4.5, colour=(0.9, 0.75, 0.4)), position=(-3.5, 0, 30), rotation_deg=(0, 200, 0)),
	place(mesh("armadillo", height=4.0, colour=(0.45, 0.8, 0.5)), position=(3.8, 0, 31), rotation_deg=(0, 160, 0)),
]
zones.append(group([wall] + crowd + statues))

# F. Vehicle yard: multi part meshes (tank, bulldozer) and animals at varied yaw
zones.append(group([
	place(mesh("tank", size=3.6, colour=(0.45, 0.55, 0.35)), position=(1.5, 0, 6), rotation_deg=(0, -35, 0)),
	place(mesh("bulldozer", size=2.6, colour=(1.0, 0.75, 0.2)), position=(-0.5, 0, 1), rotation_deg=(0, 120, 0)),
	place(mesh("cow", size=1.6, colour=(0.9, 0.9, 0.85)), position=(2.8, 0, 1.5), rotation_deg=(0, 70, 0)),
	place(mesh("cow", size=1.4, colour=(0.55, 0.4, 0.3)), position=(3.2, 0, 3.0), rotation_deg=(0, 250, 0)),
	place(mesh("elephant", height=2.4, colour=(0.6, 0.6, 0.65)), position=(0.0, 0, 10), rotation_deg=(0, -100, 0)),
], position=(6.5, 0, 9)))

# G. Garden: three kinds of tree (one a Z up file) at random yaw and size, hedges of low poly spheres
garden = []
for i in range(12):
	kind = ["tree", "palm", "lowpoly_tree"][i % 3]
	height = random.uniform(2.2, 3.8)
	green = (0.35 + jitter(0.1), 0.75 + jitter(0.15), 0.35 + jitter(0.1))
	garden.append(place(mesh(kind, height=height, colour=green), position=(-8.5 + jitter(2.0), 0, 5 + i * 1.6 + jitter(0.5)), rotation_deg=(0, random.uniform(0, 360), 0)))
for i in range(10):
	garden.append(place(mesh("sphere_lo", height=0.45, scale_xyz=(1.5, 1, 1.0), colour=(0.25, 0.55, 0.25)), position=(-5.4, 0, 5 + i * 1.0)))
zones.append(group(garden))

# H. Materials row: the multi part Mori knob at different rotations / tints, nested hollow cubes and tumbling teapots
knobs = [place(mesh("pawn", height=0.5 + i * 0.15, colour=hsv(0.08 + i * 0.18, 0.5)), position=(i * 0.9, 0, 0), rotation_deg=(0, -30 + i * 30, 0)) for i in range(5)]
knobs.append(place(mesh("three_objects", size=1.6, colour=(0.9, 0.9, 0.95)), position=(5.2, 0, 1.0), rotation_deg=(0, 25, 0)))
nest = []
for i in range(4):
	nest.append(place(mesh("hollowcube", size=1.6 - i * 0.35, pivot="centre", colour=hsv(0.55 + i * 0.1, 0.4)), rotation_deg=(i * 20, i * 35, i * 15)))
tumble = []
for i in range(6):
	tumble.append(place(mesh("teapot", size=0.5, pivot="centre", scale_xyz=(1, 1.0 + (i % 3) * 0.6, 1), colour=hsv(i / 6, 0.8)),
		position=(math.cos(i) * 1.5, 1.2 + i * 0.35, math.sin(i) * 1.5), rotation_deg=(random.uniform(0, 360), random.uniform(0, 360), random.uniform(0, 360))))
zones.append(group(knobs, position=(3.4, 0, 3.5), rotation_deg=(0, -20, 0)))
zones.append(group(nest + tumble, position=(4.0, 1.2, 22)))

# I. Scale extremes far away: a giant cat (Z up file) and a giant armadillo
zones.append(place(mesh("cat", height=14.0, colour=(0.75, 0.6, 0.9)), position=(-15, 0, 60), rotation_deg=(0, 100, 0)))
zones.append(place(mesh("armadillo", height=7.0, colour=(0.95, 0.55, 0.45)), position=(-16, 0, 45), rotation_deg=(0, 150, 0)))

# J. Far field: a grid of spheres and cones (many small instances, largely behind the wall)
far_field = []
for gz in range(6):
	for gx in range(11):
		name = "sphere_lo" if (gx + gz) % 2 == 0 else "cone"
		far_field.append(place(mesh(name, height=random.uniform(0.6, 1.4), colour=hsv(random.random(), 0.7)), position=(-15 + gx * 3 + jitter(0.5), 0, 38 + gz * 5 + jitter(0.5)), rotation_deg=(0, random.uniform(0, 360), 0)))
zones.append(group(far_field))

# K. Frustum culling: a ring of objects around the camera (mostly outside the view) and a cluster behind it
camera = (0, 0.25, -2)
ring_objects = []
for i in range(32):
	a = math.radians(i * 360 / 32)
	name = ["icosphere", "cone", "torus", "suzanne"][i % 4]
	ring_objects.append(place(mesh(name, size=1.4, pivot="centre", colour=hsv(i / 32, 0.75)), position=(camera[0] + math.sin(a) * 34, 1.5, camera[2] + math.cos(a) * 34), rotation_deg=(0, i * 360 / 32, 30)))
behind = [place(mesh("head", height=1.0, colour=(1.0, 0.5, 0.5)), position=(jitter(6), 0, -10 - i * 2.5), rotation_deg=(0, random.uniform(0, 360), 0)) for i in range(8)]
zones.append(group(ring_objects + behind))

root = node(children=zones)
json.dump(root, open(os.path.join(OUT_DIR, "MixedAssetsTest.json"), "w"), separators=(",", ":"))

# Summary
instances = {}
def count(n, depth=0, stats=None):
	stats = stats if stats is not None else {"max_depth": 0, "nodes": 0}
	stats["nodes"] += 1
	stats["max_depth"] = max(stats["max_depth"], depth)
	if n["model_name"] != INVALID:
		instances[n["model_name"]] = instances.get(n["model_name"], 0) + 1
	for c in n["children"]:
		count(c, depth + 1, stats)
	return stats
stats = count(root)
total_meshlets = 0
for model, n in sorted(instances.items(), key=lambda kv: -kv[1]):
	tris = bounds[bounds_key(model[len(MODEL_PREFIX):])]["tris"]
	total_meshlets += n * math.ceil(tris / 110)
	print(f"{n:4d} x {model[len(MODEL_PREFIX):]:40s} {tris:7d} tris")
print(f"{sum(instances.values())} instances of {len(instances)} meshes, {stats['nodes']} nodes, depth {stats['max_depth']}, ~{total_meshlets} meshlets")
