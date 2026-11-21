"""Generates the GPU driven rendering test scenes in SlimGraphicsAssets/GDRAssets.

Each scene exercises frustum, cone and HZB occlusion culling, hierarchies, instancing, multi part meshes, non uniform
scales, arbitrary rotations and per instance colours, at growing scale:
  MixedAssetsTest  ~260 instances   a gallery of every asset type
  VillageTest      ~1.1k instances  houses, market, church, windmill (nested blade group), fields, meadow
  ForestTest       ~4.2k instances  forest, river, ruins, herds, fireflies
  FractalTest      ~16k instances   chessboard, depth 7 fractal tree, level 3 Menger sponge, asteroid spiral, crystals
  MaterialsTest    a showcase of texture, alpha test, normal and bump mapped materials
Every scene (and HeadsTest / SpheresTest) gets a terrain (flat under its content and camera track, hills, lakes and snow
beyond), a sun and sky fill light plus themed point lights (street lamps, fireflies, glowing crystals), an "environment"
(ambient, fog, exposure), and a closed "camera_track" (gdrJSON.h CameraTrack: a centripetal Catmull-Rom
spline through position / target pairs, played at constant speed by CameraTrackPlayer).

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
# Only OBJ files are measured; the glTF helmet is centred and about 1.9 units across
bounds["Khronos/DamagedHelmet/glTF/DamagedHelmet.gltf"] = {"min": [-0.95] * 3, "max": [0.95] * 3, "size": [1.9] * 3, "tris": 46356}

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
	"house": ("3DBuilder\\Residential House.obj", True),
	"pumpkin": ("3DBuilder\\Carved pumpkin.obj", True),
	"sunflower": ("3DBuilder\\Sunflower - low poly.obj", True),
	"trophy": ("3DBuilder\\Trophy cup.obj", True),
	"grass": ("3DBuilder\\Tuft of grass.obj", True),
	"tree1b_lod0_a": ("DebugModels\\tree1b_lod0_1.obj", False),
	"tree1b_lod0_b": ("DebugModels\\tree1b_lod0_2.obj", False),
	"tree1b_lod2_a": ("DebugModels\\tree1b_lod2_1.obj", False),
	"tree1b_lod2_b": ("DebugModels\\tree1b_lod2_2.obj", False),
	"king": ("GL\\ChessKing.obj", False),
	"orb": ("DebugModels\\orb.obj", False),
	"cgtree": ("CGArchive\\CGTree\\cgaxis_models_115_37_obj.obj", False),
	"helmet": ("Khronos\\DamagedHelmet\\glTF\\DamagedHelmet.gltf", False),
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


# --- Scene 1 geometry: mixed assets ---------------------------------------------------------------------------------
def build_mixed():
	random.seed(1234)
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
	return node(children=zones)

# --- Single node placement and camera tracks --------------------------------------------------------------------------
def euler_from_matrix(m):
	"""Pitch, yaw, roll (degrees) with rotation(pitch, yaw, roll) == m: the inverse of XMMatrixRotationRollPitchYaw."""
	sp = max(-1.0, min(1.0, -m[2][1]))
	pitch = math.asin(sp)
	if abs(sp) < 1.0 - 1e-12:  # atan2 stays accurate as cos(pitch) gets small
		yaw = math.atan2(m[2][0], m[2][2])
		roll = math.atan2(m[0][1], m[1][1])
	else:
		yaw = 0.0
		roll = math.atan2(-m[1][0], m[0][0])
	return [math.degrees(pitch), math.degrees(yaw), math.degrees(roll)]

def _check_euler_round_trip():
	rng = random.Random(7)
	for _ in range(200):
		e = [rng.uniform(-180, 180) for _ in range(3)]
		m = rotation(*e)
		m2 = rotation(*euler_from_matrix(m))
		assert all(abs(m[i][j] - m2[i][j]) < 1e-7 for i in range(3) for j in range(3)), e
_check_euler_round_trip()

def obj(name, position=(0, 0, 0), rotation_deg=(0, 0, 0), height=None, size=None, scale_xyz=(1, 1, 1), colour=(1, 1, 1), pivot="bottom", children=None):
	"""place(mesh(...), position, rotation_deg) folded into a single node: S * (R_fix * R_place) * T(offset * R_place + position)."""
	m = mesh(name, height=height, size=size, scale_xyz=scale_xyz, colour=colour, pivot=pivot)
	r_fix = rotation(*[m["rotation"][k] for k in "xyz"])
	r_place = rotation(*rotation_deg)
	offset = vec_mul([m["position"][k] for k in "xyz"], r_place)
	m["rotation"] = f3(euler_from_matrix(mat_mul(r_fix, r_place)))
	m["position"] = f3([offset[i] + position[i] for i in range(3)])
	if children:
		m["children"] = children
	return m

def track(duration, points, near=0.1, far=100.0):
	"""A looping camera track: points are (position, look at target) pairs."""
	return {"duration": duration, "near_plane": near, "far_plane": far, "points": [{"position": f3(p), "target": f3(t)} for p, t in points]}

# Lights, terrain and environment ---------------------------------------------------------------------------------------

def light(kind="point", colour=(1, 1, 1), intensity=1.0, range_=10.0, direction=(0, -1, 0)):
	"""A node "light" component (gdrJSON.h LightDesc)."""
	component = {"type": kind, "colour": f3(colour), "intensity": intensity}
	if kind == "point":
		component["range"] = range_
	else:
		length = math.sqrt(sum(d * d for d in direction))
		component["direction"] = f3([d / length for d in direction])
	return component

def light_node(position=(0, 0, 0), **kwargs):
	n = node(position=position)
	n["light"] = light(**kwargs)
	return n

def content_bounds(root, camera_track=None):
	"""World space x / z bounds of every model node origin (and camera track point)."""
	identity = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
	lo, hi = [math.inf, math.inf], [-math.inf, -math.inf]
	def add(p):
		lo[0] = min(lo[0], p[0]); lo[1] = min(lo[1], p[2])
		hi[0] = max(hi[0], p[0]); hi[1] = max(hi[1], p[2])
	def walk(n, parent_m, parent_t):
		v = lambda k: [n[k]["x"], n[k]["y"], n[k]["z"]]
		t = [a + b for a, b in zip(vec_mul(v("position"), parent_m), parent_t)]
		m = mat_mul(mat_mul(scaling(v("scale")), rotation(*v("rotation"))), parent_m)
		if n["model_name"] != INVALID:
			add(t)
		for c in n["children"]:
			walk(c, m, t)
	walk(root, identity, [0, 0, 0])
	for point in (camera_track or {}).get("points", []):
		add([point["position"]["x"], 0, point["position"]["z"]])
	return lo, hi

def decorate(root, camera_track, environment, lights, margin=6.0, falloff=40.0, hills=120.0, max_quads=504, **terrain):
	"""Adds a terrain under the scene (flat where the content and camera track are, hills beyond), the given scene level
	lights and the environment. Re-running replaces what an earlier run added."""
	# The terrain and scene lights live in one "scenery" group (identified by its terrain child)
	root["children"] = [c for c in root["children"] if not any("terrain" in g for g in c["children"])]
	lo, hi = content_bounds(root, camera_track)
	cx, cz = (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5
	half_x, half_z = (hi[0] - lo[0]) * 0.5 + margin, (hi[1] - lo[1]) * 0.5 + margin
	size = 2.0 * (max(half_x, half_z) + falloff + hills)
	desc = {"size": round(size, 2), "resolution": min(max_quads, int(size / 1.5)), "flat_x_min": round(-half_x, 2), "flat_z_min": round(-half_z, 2),
	        "flat_x_max": round(half_x, 2), "flat_z_max": round(half_z, 2), "flat_falloff": falloff, "flat_height": 0.02}
	desc.update(terrain)
	terrain_node = node(position=(cx, 0, cz))
	terrain_node["terrain"] = desc
	root["children"].append(group([terrain_node] + lights))
	root["environment"] = environment
	return root

def sun(direction, colour=(1.0, 0.95, 0.85), intensity=2.0):
	return light_node(kind="directional", direction=direction, colour=colour, intensity=intensity)

def environment(sky, ground, ambient, fog, fog_density, fog_start=0.0, exposure=1.0):
	return {"sky_colour": f3(sky), "ground_colour": f3(ground), "ambient_intensity": ambient, "fog_colour": f3(fog),
	        "fog_density": fog_density, "fog_start": fog_start, "exposure": exposure}

def lerp3(a, b, t):
	return [a[i] + (b[i] - a[i]) * t for i in range(3)]


# --- Scene 1: mixed assets (~265 instances) ---------------------------------------------------------------------------
MIXED_TRACK = track(48.0, [
	((0.0, 1.5, -3.0), (0.0, 1.0, 10.0)),
	((-5.0, 2.0, 5.0), (0.0, 1.0, 12.0)),
	((-10.0, 4.0, 16.0), (-6.0, 2.5, 15.0)),
	((-4.0, 7.0, 23.0), (0.0, 5.0, 20.0)),
	((4.0, 5.5, 25.0), (0.0, 1.5, 32.0)),
	((11.0, 3.0, 34.0), (0.0, 1.0, 30.0)),
	((13.0, 2.5, 18.0), (6.0, 1.0, 13.0)),
	((6.0, 1.2, 4.0), (0.0, 0.8, 10.0)),
], near=0.1, far=120.0)


# --- Scene 2: village (~1k instances) ---------------------------------------------------------------------------------
def build_village():
	random.seed(2024)
	items = []
	stone = (0.62, 0.6, 0.57)

	def cottage(position, yaw, tint):
		# Primitive built house: body, conical roof, chimney, door
		body = obj("cube", height=3.2, scale_xyz=(6.0 / 3.2, 1, 5.0 / 3.2), colour=tint)
		roof = obj("cone", position=(0, 3.2, 0), height=2.6, scale_xyz=(2.4, 1, 2.0), colour=(0.55, 0.22, 0.18))
		chimney = obj("cube", position=(1.6, 4.0, 0.8), height=1.8, scale_xyz=(0.3, 1, 0.3), colour=(0.45, 0.4, 0.38))
		door = obj("cube", position=(0, 0, 2.52), height=1.9, scale_xyz=(0.5, 1, 0.04), colour=(0.35, 0.22, 0.12))
		return group([body, roof, chimney, door], position=position, rotation_deg=(0, yaw, 0))

	def fence(x0, z0, x1, z1, gap=None):
		length = math.hypot(x1 - x0, z1 - z0)
		yaw = math.degrees(math.atan2(x1 - x0, z1 - z0))
		parts = []
		posts = max(2, int(length / 2.0) + 1)
		for i in range(posts):
			t = i / (posts - 1)
			if gap and gap[0] < t < gap[1]:
				continue
			parts.append(obj("cube", position=(0, 0, t * length), height=1.0, scale_xyz=(0.12, 1, 0.12), colour=(0.75, 0.68, 0.55)))
		for h in (0.4, 0.8):
			parts.append(obj("cube", position=(0, h, length / 2), height=0.08, scale_xyz=(0.6, 1, length / 0.08), colour=(0.8, 0.72, 0.58)))
		return group(parts, position=(x0, 0, z0), rotation_deg=(0, yaw, 0))

	def two_part_tree(position, height, yaw, lod="lod0"):
		# tree1b is split into two files, foliage (_1) and trunk (_2): both parts share the placement
		a = obj("tree1b_" + lod + "_a", height=height, colour=(0.35 + jitter(0.08), 0.65 + jitter(0.12), 0.3))
		b = obj("tree1b_" + lod + "_b", height=height, colour=(0.5, 0.36, 0.25))
		return group([a, b], position=position, rotation_deg=(0, yaw, 0))

	def lamp(position):
		return group([
			obj("column", height=4.0, scale_xyz=(0.18, 1, 0.18), colour=(0.25, 0.25, 0.28)),
			obj("icosphere", position=(0, 4.15, 0), size=0.45, pivot="centre", colour=(1.0, 0.92, 0.55)),
			light_node((0, 3.9, 0), colour=(1.0, 0.72, 0.4), intensity=7.0, range_=10.0),
		], position=position)

	# Blocks of houses around the market square (blocks are 18 m, streets 6 m)
	for bx in (-24, 0, 24):
		for bz in (-24, 0, 24):
			if bx == 0 and bz == 0:
				continue
			block = []
			for i, (cx, cz, yaw) in enumerate([(-5, -5, 180), (5, -5, 180), (-5, 5, 0), (5, 5, 0)]):
				tint = hsv(random.random(), 0.25, 0.9)
				if (i + bx // 24 + bz // 24) % 2 == 0:
					block.append(obj("house", position=(cx, 0, cz), rotation_deg=(0, yaw + jitter(4), 0), height=6.5 + jitter(1.0), colour=tint))
				else:
					block.append(cottage((cx, 0, cz), yaw + jitter(4), tint))
			# Garden in the middle of the block
			block.append(two_part_tree((jitter(1.0), 0, jitter(1.0)), random.uniform(6.0, 9.0), random.uniform(0, 360)))
			for i in range(4):
				block.append(obj("sunflower", position=(-2.5 + i * 1.7, 0, -1.5 + jitter(0.3)), rotation_deg=(0, random.uniform(0, 360), jitter(8)), height=random.uniform(1.4, 2.0)))
			for i in range(3):
				block.append(obj("pumpkin", position=(jitter(2.5), 0, 1.6 + jitter(0.6)), rotation_deg=(0, random.uniform(0, 360), 0), height=random.uniform(0.35, 0.6), colour=(1.0, 0.55, 0.15)))
			block.append(obj("grass", position=(2.2, 0, 2.2), height=0.5, rotation_deg=(0, random.uniform(0, 360), 0), colour=(0.4, 0.7, 0.3)))
			# Picket fence around the block, with a gate on each side
			for (x0, z0, x1, z1) in [(-9, -9, 9, -9), (9, -9, 9, 9), (9, 9, -9, 9), (-9, 9, -9, -9)]:
				block.append(fence(x0, z0, x1, z1, gap=(0.42, 0.58)))
			items.append(group(block, position=(bx, 0, bz)))

	# Street lamps along every street
	for line in (-36, -12, 12, 36):
		for s in range(-36, 37, 8):
			items.append(lamp((line - 2.6, 0, s + 1)))
			items.append(lamp((s - 1, 0, line + 2.6)))

	# Market square: fountain, stalls with canopies and produce, a statue, benches
	fountain = group([
		obj("torus", size=7.0, scale_xyz=(1, 1.6, 1), colour=stone),
		obj("column", height=2.5, scale_xyz=(0.5, 1, 0.5), colour=stone),
		obj("torus", position=(0, 2.5, 0), size=3.0, colour=stone),
		obj("sphere_hi", position=(0, 3.1, 0), size=1.0, pivot="centre", colour=(0.4, 0.6, 0.95)),
		obj("sphere_lo", position=(0, 0.05, 0), height=0.3, scale_xyz=(9.0, 1, 9.0), colour=(0.3, 0.5, 0.9)),
	])
	market = [fountain]
	for i in range(8):
		a = math.radians(i * 45 + 22.5)
		r = 6.5
		awning = hsv(i / 8, 0.7)
		stall = [obj("cube", position=(0, 0, 0), height=0.9, scale_xyz=(2.0 / 0.9, 1, 1.2 / 0.9), colour=(0.55, 0.4, 0.25))]
		for px, pz in ((-0.9, -0.5), (0.9, -0.5), (-0.9, 0.5), (0.9, 0.5)):
			stall.append(obj("column", position=(px, 0, pz), height=2.4, scale_xyz=(0.12, 1, 0.12), colour=(0.6, 0.5, 0.35)))
		stall.append(obj("cone", position=(0, 2.4, 0), height=1.0, scale_xyz=(1.6, 1, 1.1), colour=awning))
		for k in range(3):
			stall.append(obj(["pumpkin", "icosphere", "sphere_lo"][k], position=(-0.6 + k * 0.6, 0.9, 0), height=0.3, colour=hsv(random.random(), 0.8)))
		market.append(group(stall, position=(math.sin(a) * r, 0, math.cos(a) * r), rotation_deg=(0, i * 45 + 22.5, 0)))
	market.append(group([obj("cube", height=1.2, scale_xyz=(1.2, 1, 1.2), colour=stone), obj("venus", position=(0, 1.2, 0), height=2.6, rotation_deg=(0, 180, 0), colour=(0.9, 0.88, 0.82))], position=(6.0, 0, -6.0)))
	for i in range(4):
		market.append(obj("cube", position=(-6.0 + i * 1.6, 0, 6.5), height=0.45, scale_xyz=(3.0, 1, 1.0), colour=(0.5, 0.35, 0.22), rotation_deg=(0, 45, 0)))
	items.append(group(market))

	# Church with a tower of stacked hollow cubes, a spire and a golden trophy on top
	church = [obj("house", height=10.0, colour=(0.85, 0.82, 0.75))]
	tower = []
	for i in range(4):
		tower.append(obj("hollowcube", position=(0, i * 3.0, 0), height=3.0, colour=(0.8, 0.78, 0.72)))
	tower.append(obj("cone", position=(0, 12.0, 0), height=6.0, scale_xyz=(0.6, 1, 0.6), colour=(0.3, 0.32, 0.4)))
	tower.append(obj("trophy", position=(0, 18.0, 0), height=1.2, colour=(1.0, 0.8, 0.2)))
	church.append(group(tower, position=(0, 0, -9.0)))
	items.append(group(church, position=(0, 0, 52), rotation_deg=(0, 180, 0)))

	# Farm fields: sunflower rows (east), pumpkin patch (south), haystacks, a windmill with nested rotating blades (west)
	for row in range(10):
		for i in range(14):
			items.append(obj("sunflower", position=(46 + row * 1.6 + jitter(0.2), 0, -20 + i * 3.0 + jitter(0.3)), rotation_deg=(0, -90 + jitter(20), jitter(6)), height=random.uniform(1.6, 2.3)))
	for row in range(8):
		for i in range(10):
			items.append(obj("pumpkin", position=(-18 + i * 4.0 + jitter(0.6), 0, -46 - row * 2.2 + jitter(0.4)), rotation_deg=(0, random.uniform(0, 360), 0), height=random.uniform(0.4, 0.9), colour=(1.0, 0.5 + jitter(0.08), 0.12)))
	for i in range(12):
		items.append(obj("sphere_lo", position=(30 + jitter(10), 0, -52 + jitter(8)), height=1.4, scale_xyz=(1.6, 1, 1.6), colour=(0.9, 0.78, 0.4)))
	blades = [obj("cube", position=(0, 0, 0), height=9.0, scale_xyz=(0.08, 1, 0.012), colour=(0.9, 0.88, 0.85), rotation_deg=(0, 0, angle)) for angle in (0, 90, 180, 270)]
	rotor = group(blades, position=(0, 0, -1.6), rotation_deg=(0, 0, 17))
	windmill = group([
		obj("cone", height=12.0, scale_xyz=(0.45, 1, 0.45), colour=(0.85, 0.83, 0.78)),
		group([obj("icosphere", size=1.4, pivot="centre", colour=(0.4, 0.3, 0.25)), rotor], position=(0, 10.5, 0)),
	], position=(-52, 0, 4), rotation_deg=(0, 70, 0))
	items.append(windmill)
	for i in range(12):
		items.append(obj("cow", position=(-50 + jitter(12), 0, -26 + jitter(10)), rotation_deg=(0, random.uniform(0, 360), 0), size=random.uniform(1.6, 2.2), colour=hsv(0.08, random.uniform(0.0, 0.5), random.uniform(0.4, 0.95))))

	# A meadow of dense (29k triangle) grass tufts around the pasture and the windmill
	for i in range(64):
		items.append(obj("grass", position=(-48 + jitter(16), 0, -8 + jitter(28)), rotation_deg=(0, random.uniform(0, 360), 0), height=random.uniform(0.5, 1.1), colour=(0.4 + jitter(0.1), 0.7 + jitter(0.1), 0.3)))

	# A ring of trees around the village
	for i in range(40):
		a = math.radians(i * 9 + jitter(3))
		r = 70 + jitter(6)
		items.append(two_part_tree((math.sin(a) * r, 0, math.cos(a) * r), random.uniform(7.0, 12.0), random.uniform(0, 360), lod="lod2"))

	camera = track(70.0, [
		((0.0, 3.0, -62.0), (0.0, 2.0, -30.0)),
		((-12.0, 2.5, -40.0), (-12.0, 2.0, -10.0)),
		((-14.0, 3.0, -8.0), (-6.0, 2.0, 2.0)),
		((-8.0, 6.0, 8.0), (0.0, 1.5, 0.0)),
		((8.0, 9.0, 12.0), (0.0, 1.0, 0.0)),
		((18.0, 6.0, 30.0), (0.0, 8.0, 52.0)),
		((-18.0, 22.0, 72.0), (0.0, 6.0, 40.0)),
		((-44.0, 9.0, 30.0), (-52.0, 9.0, 4.0)),
		((-40.0, 6.0, -18.0), (-50.0, 1.0, -26.0)),
		((-34.0, 14.0, -60.0), (5.0, 0.5, -50.0)),
		((40.0, 8.0, -40.0), (52.0, 1.5, -2.0)),
		((62.0, 5.0, 12.0), (50.0, 1.5, 0.0)),
		((40.0, 10.0, 30.0), (0.0, 2.0, 0.0)),
		((20.0, 6.0, -50.0), (0.0, 2.0, -30.0)),
	], near=0.1, far=220.0)
	return node(children=items), camera


# --- Scene 3: forest with ruins (~4k instances) -----------------------------------------------------------------------
def build_forest():
	random.seed(4048)
	items = []
	extent = 115.0
	clearings = [((30.0, 40.0), 14.0), ((-50.0, -20.0), 13.0), ((60.0, -60.0), 15.0), ((-30.0, 70.0), 10.0)]

	def river_x(z):
		return 18.0 * math.sin(z / 28.0) - 10.0

	camera = track(90.0, [
		((-100.0, 3.0, -100.0), (-80.0, 3.0, -80.0)),
		((-75.0, 2.5, -55.0), (-50.0, 2.0, -20.0)),
		((-58.0, 4.0, -8.0), (-50.0, 1.5, -20.0)),
		((-35.0, 3.0, 15.0), (river_x(30.0), 1.0, 40.0)),
		((river_x(45.0) + 5.0, 2.0, 45.0), (30.0, 2.0, 40.0)),
		((22.0, 6.0, 55.0), (30.0, 1.5, 40.0)),
		((-20.0, 30.0, 85.0), (0.0, 0.0, 0.0)),
		((-40.0, 4.0, 62.0), (-30.0, 1.0, 70.0)),
		((10.0, 3.0, 10.0), (60.0, 2.0, -60.0)),
		((44.0, 7.0, -62.0), (60.0, 1.5, -60.0)),
		((78.0, 10.0, -74.0), (60.0, 2.0, -60.0)),
		((95.0, 40.0, 10.0), (0.0, 0.0, 0.0)),
		((40.0, 6.0, 90.0), (0.0, 3.0, 40.0)),
		((-95.0, 18.0, 40.0), (-40.0, 2.0, -40.0)),
	], near=0.1, far=320.0)

	# Keep a corridor along the camera path clear of trees
	path_samples = []
	pts = camera["points"]
	for i in range(len(pts)):
		a = [pts[i]["position"][k] for k in "xyz"]
		b = [pts[(i + 1) % len(pts)]["position"][k] for k in "xyz"]
		for s in range(20):
			path_samples.append(lerp3(a, b, s / 20))

	def free(x, z, radius):
		if abs(x) > extent or abs(z) > extent:
			return False
		for (cx, cz), r in clearings:
			if math.hypot(x - cx, z - cz) < r + radius:
				return False
		if abs(x - river_x(z)) < 4.0 + radius:
			return False
		for p in path_samples:
			if p[1] < 8.0 and math.hypot(x - p[0], z - p[2]) < 4.5 + radius:
				return False
		return True

	# Trees on a jittered grid (two part tree1b, the detailed tree and the low poly tree), heavy grass tufts between them
	cell = 5.2
	n = int(extent * 2 / cell)
	tree_count = 0
	for gx in range(n):
		for gz in range(n):
			x = -extent + (gx + 0.5) * cell + jitter(cell * 0.35)
			z = -extent + (gz + 0.5) * cell + jitter(cell * 0.35)
			if not free(x, z, 1.5) or random.random() < 0.18:
				continue
			kind = random.random()
			yaw = random.uniform(0, 360)
			height = random.uniform(6.0, 14.0)
			if kind < 0.6:
				lod = "lod0" if math.hypot(x, z) < 70 else "lod2"
				items.append(group([obj("tree1b_" + lod + "_a", height=height, colour=(0.25 + jitter(0.08), 0.55 + jitter(0.15), 0.22)),
				                    obj("tree1b_" + lod + "_b", height=height, colour=(0.45, 0.33, 0.24))],
				                   position=(x, 0, z), rotation_deg=(jitter(3), yaw, jitter(3))))
			elif kind < 0.85:
				items.append(obj("tree", position=(x, 0, z), rotation_deg=(0, yaw, 0), height=height * 0.8, colour=(0.35 + jitter(0.08), 0.6 + jitter(0.15), 0.3)))
			else:
				items.append(obj("lowpoly_tree", position=(x, 0, z), rotation_deg=(0, yaw, 0), height=height * 0.7, colour=(0.3, 0.55 + jitter(0.15), 0.3)))
			tree_count += 1

	for i in range(260):
		(cx, cz), r = random.choice(clearings) if random.random() < 0.6 else ((random.uniform(-extent, extent), random.uniform(-extent, extent)), 4.0)
		a = random.uniform(0, math.tau)
		d = random.uniform(0, r)
		items.append(obj("grass", position=(cx + math.cos(a) * d, 0, cz + math.sin(a) * d), rotation_deg=(0, random.uniform(0, 360), 0), height=random.uniform(0.4, 0.9), colour=(0.35 + jitter(0.1), 0.65 + jitter(0.1), 0.25)))

	# Rocks: icospheres with random non uniform scales and rotations
	for i in range(520):
		x, z = random.uniform(-extent, extent), random.uniform(-extent, extent)
		size = random.uniform(0.3, 2.4)
		grey = random.uniform(0.4, 0.7)
		items.append(obj("icosphere" if i % 3 else "sphere_lo", position=(x, -size * 0.25, z), rotation_deg=(random.uniform(0, 360), random.uniform(0, 360), random.uniform(0, 360)),
			size=size, pivot="centre", scale_xyz=(random.uniform(0.6, 1.4), random.uniform(0.4, 0.9), random.uniform(0.6, 1.4)), colour=(grey, grey * 0.97, grey * 0.92)))

	# Mushrooms: stem and cap pairs
	for i in range(300):
		x, z = random.uniform(-extent, extent), random.uniform(-extent, extent)
		h = random.uniform(0.15, 0.6)
		items.append(group([obj("column", height=h, scale_xyz=(0.5, 1, 0.5), colour=(0.92, 0.9, 0.82)),
		                    obj("sphere_lo", position=(0, h * 0.9, 0), height=h * 0.6, scale_xyz=(2.2, 1, 2.2), colour=hsv(random.choice((0.0, 0.03, 0.08, 0.6)), 0.85, 0.9))],
		                   position=(x, 0, z), rotation_deg=(jitter(10), random.uniform(0, 360), jitter(10))))

	# River: a winding chain of flattened water patches with stepping stones
	for i in range(130):
		z = -extent + i * (extent * 2 / 130)
		items.append(obj("sphere_lo", position=(river_x(z), -0.1, z), height=0.3, scale_xyz=(7.0, 1, 4.0), rotation_deg=(0, math.degrees(math.atan(18.0 / 28.0 * math.cos(z / 28.0))), 0), colour=(0.25, 0.45, 0.85)))
		if i % 6 == 0:
			items.append(obj("icosphere", position=(river_x(z) + jitter(1.5), 0, z), size=0.9, scale_xyz=(1.2, 0.5, 1.0), pivot="centre", colour=(0.55, 0.55, 0.5)))

	# Temple ruins in the clearings: rings of standing, broken and fallen columns, an altar with a trophy, statues, blocks
	for ci, ((cx, cz), r) in enumerate(clearings[:3]):
		ruin = []
		ring_r = r * 0.6
		for i in range(12):
			a = math.radians(i * 30)
			x, z = math.sin(a) * ring_r, math.cos(a) * ring_r
			state = random.random()
			if state < 0.55:
				ruin.append(obj("column", position=(x, 0, z), rotation_deg=(jitter(2), random.uniform(0, 90), jitter(2)), height=5.0, colour=(0.85, 0.83, 0.76)))
			elif state < 0.8:
				ruin.append(obj("column", position=(x, 0, z), rotation_deg=(0, random.uniform(0, 90), 0), height=5.0, scale_xyz=(1, random.uniform(0.25, 0.6), 1), colour=(0.8, 0.78, 0.7)))
			else:
				ruin.append(obj("column", position=(x * 1.25, 0.75, z * 1.25), rotation_deg=(90, random.uniform(0, 360), 0), height=5.0, pivot="centre", colour=(0.75, 0.73, 0.66)))
		ruin.append(obj("cube", height=1.0, scale_xyz=(2.0, 1, 1.4), colour=(0.6, 0.58, 0.55)))
		ruin.append(obj("trophy", position=(0, 1.0, 0), height=0.9, colour=(1.0, 0.8, 0.25)))
		ruin.append(obj(["venus", "buddha", "armadillo"][ci], position=(0, 0, -ring_r - 2.5), rotation_deg=(0, 0, 0), height=4.0, colour=(0.82, 0.8, 0.74)))
		for i in range(12):
			ruin.append(obj("cube", position=(jitter(r * 0.8), 0, jitter(r * 0.8)), rotation_deg=(jitter(25), random.uniform(0, 360), jitter(25)), size=random.uniform(0.5, 1.2), colour=(0.7, 0.68, 0.62)))
		items.append(group(ruin, position=(cx, 0, cz), rotation_deg=(0, random.uniform(0, 360), 0)))

	# Animal herds
	herds = [("cow", 8, (1.8, 2.2), (-60.0, 50.0)), ("elephant", 5, (2.8, 3.5), (70.0, 20.0)), ("armadillo", 6, (0.9, 1.3), (-80.0, -70.0)),
	         ("bunny", 24, (0.3, 0.5), (10.0, -30.0)), ("cat", 4, (1.0, 1.4), (-10.0, 90.0)), ("hires_bunny", 6, (0.4, 0.6), (80.0, 80.0))]
	for name, count, (lo, hi), (hx, hz) in herds:
		for i in range(count):
			kwargs = {"size": random.uniform(lo, hi)} if name in ("cow",) else {"height": random.uniform(lo, hi)}
			items.append(obj(name, position=(hx + jitter(8), 0, hz + jitter(8)), rotation_deg=(0, random.uniform(0, 360), 0), colour=hsv(random.uniform(0.05, 0.12), random.uniform(0.0, 0.5), random.uniform(0.5, 0.95)), **kwargs))

	# Fireflies: tiny glowing spheres between the trees and over the clearings
	for i in range(420):
		(cx, cz), r = random.choice(clearings)
		a = random.uniform(0, math.tau)
		d = random.uniform(0, r * 1.6)
		firefly = obj("icosphere", position=(cx + math.cos(a) * d, random.uniform(0.4, 4.0), cz + math.sin(a) * d), size=random.uniform(0.05, 0.12), pivot="centre", colour=(0.85, 1.0, 0.35))
		if i % 14 == 0:
			firefly["light"] = light(colour=(0.75, 1.0, 0.35), intensity=2.5, range_=5.0)
		items.append(firefly)

	return node(children=items), camera


# --- Scene 4: fractal structures (~16k instances) ---------------------------------------------------------------------
def build_fractal():
	random.seed(8096)
	items = []

	# Giant chessboard of 64 tiles (its top 5 cm above the terrain's flat area) with high polygon kings and pawns (46k / 14k triangles)
	board = []
	tile = 4.0
	for i in range(8):
		for j in range(8):
			dark = (i + j) % 2 == 0
			board.append(obj("cube", position=((i - 3.5) * tile, -0.35, (j - 3.5) * tile), height=0.4, scale_xyz=(tile / 0.4, 1, tile / 0.4), colour=(0.15, 0.13, 0.12) if dark else (0.92, 0.9, 0.85)))
	for side, row_pawn, row_back, colour in ((0, 1, 0, (0.95, 0.92, 0.85)), (1, 6, 7, (0.25, 0.22, 0.24))):
		for i in range(8):
			board.append(obj("pawn", position=((i - 3.5) * tile, 0.05, (row_pawn - 3.5) * tile), height=3.0, colour=colour, rotation_deg=(0, random.uniform(0, 360), 0)))
			board.append(obj("king", position=((i - 3.5) * tile, 0.05, (row_back - 3.5) * tile), height=5.5 if i in (3, 4) else 4.2, colour=colour, rotation_deg=(0, 180 * side, 0)))
	items.append(group(board))

	# Fractal tree: every branch carries three smaller branches (deep hierarchy, compounding scale and rotation), bunny leaves
	def branch(depth, max_depth):
		length = 14.0
		if depth == max_depth:
			return [obj("bunny", height=3.0, rotation_deg=(0, random.uniform(0, 360), 0), colour=hsv(0.22 + jitter(0.12), 0.7, 0.85))]
		children = [obj("column", height=length, scale_xyz=(0.3 + 0.05 * (max_depth - depth), 1, 0.3 + 0.05 * (max_depth - depth)), colour=lerp3((0.45, 0.3, 0.2), (0.4, 0.7, 0.3), depth / max_depth))]
		for k in range(3):
			children.append(group(branch(depth + 1, max_depth), position=(0, length, 0), rotation_deg=(35 + jitter(6), k * 120 + depth * 17, 0), scale=(0.68, 0.68, 0.68)))
		return children
	items.append(group(branch(0, 7), position=(0, 0, 90)))

	# Menger sponge (level 3): 20 sub cubes per level, 8000 unit cubes in a three level hierarchy
	def sponge(level, size):
		if level == 0:
			return []
		children = []
		step = size / 3.0
		for x in (-1, 0, 1):
			for y in (-1, 0, 1):
				for z in (-1, 0, 1):
					if abs(x) + abs(y) + abs(z) <= 1:
						continue
					if level == 1:
						children.append(obj("cube", position=(x * step, y * step, z * step), size=step * 0.98, pivot="centre",
							colour=hsv(0.55 + 0.1 * (x + y + z) / 3 + jitter(0.03), 0.5, 0.85)))
					else:
						children.append(group(sponge(level - 1, step), position=(x * step, y * step, z * step)))
		return children
	items.append(group(sponge(3, 27.0), position=(-95, 13.5, 30), rotation_deg=(0, 30, 0)))

	# Asteroid spiral around an 81k triangle orb: two arms of mixed meshes with random tumbles and non uniform scales
	spiral = [obj("orb", size=14.0, pivot="centre", colour=(0.9, 0.6, 0.3)),
	          light_node((0, 0, 0), colour=(1.0, 0.65, 0.3), intensity=60.0, range_=90.0),
	          obj("torus", size=40.0, scale_xyz=(1, 0.3, 1), pivot="centre", rotation_deg=(12, 0, 0), colour=(0.7, 0.6, 0.85))]
	asteroid_meshes = ["icosphere", "sphere_lo", "suzanne", "teapot", "armadillo", "cone"]
	for i in range(3200):
		arm = i % 2
		t = (i // 2) / 1600.0
		angle = arm * math.pi + t * 4.5 * math.pi + jitter(0.25)
		radius = 12.0 + t * 48.0 + jitter(3.0)
		y = jitter(3.0 + t * 4.0)
		spiral.append(obj(asteroid_meshes[i % len(asteroid_meshes)], position=(math.cos(angle) * radius, y, math.sin(angle) * radius),
			rotation_deg=(random.uniform(0, 360), random.uniform(0, 360), random.uniform(0, 360)), size=random.uniform(0.6, 2.2), pivot="centre",
			scale_xyz=(random.uniform(0.7, 1.3), random.uniform(0.7, 1.3), random.uniform(0.7, 1.3)), colour=hsv(0.08 + t * 0.6 + jitter(0.05), 0.55, 0.9)))
	items.append(group(spiral, position=(100, 25, 30), rotation_deg=(-15, 0, 8)))

	# Crystal spires: a forest of stretched, tilted cones around everything
	for i in range(1700):
		a = random.uniform(0, math.tau)
		r = random.uniform(130, 190)
		h = random.uniform(3, 16)
		crystal = obj("cone", position=(math.sin(a) * r, 0, 30 + math.cos(a) * r), rotation_deg=(jitter(12), random.uniform(0, 360), jitter(12)), height=h, scale_xyz=(0.15 + h * 0.01, 1, 0.15 + h * 0.01), colour=hsv(0.5 + jitter(0.15), 0.6, 0.95))
		items.append(crystal)
		if i % 40 == 0:
			c = crystal["colour"]
			items.append(light_node((math.sin(a) * r, h + 1.0, 30 + math.cos(a) * r), colour=(c["x"], c["y"], c["z"]), intensity=20.0, range_=24.0))

	camera = track(100.0, [
		((0.0, 8.0, -40.0), (0.0, 2.0, 0.0)),
		((-22.0, 5.0, -10.0), (0.0, 3.0, 0.0)),
		((-20.0, 14.0, 25.0), (0.0, 0.0, 0.0)),
		((-40.0, 20.0, 55.0), (0.0, 30.0, 90.0)),
		((-12.0, 45.0, 120.0), (0.0, 25.0, 90.0)),
		((30.0, 25.0, 115.0), (0.0, 20.0, 90.0)),
		((-50.0, 18.0, 70.0), (-95.0, 13.5, 30.0)),
		((-70.0, 22.0, 0.0), (-95.0, 13.5, 30.0)),
		((-130.0, 30.0, 25.0), (-95.0, 13.5, 30.0)),
		((-60.0, 60.0, -60.0), (0.0, 0.0, 30.0)),
		((60.0, 35.0, -40.0), (100.0, 25.0, 30.0)),
		((95.0, 28.0, -5.0), (100.0, 25.0, 30.0)),
		((140.0, 30.0, 40.0), (100.0, 25.0, 30.0)),
		((90.0, 40.0, 95.0), (100.0, 25.0, 30.0)),
		((40.0, 15.0, 40.0), (0.0, 3.0, 0.0)),
	], near=0.1, far=450.0)
	return node(children=items), camera


# --- Output -----------------------------------------------------------------------------------------------------------
def summarise(file_name, root):
	instances = {}
	stats = {"max_depth": 0, "nodes": 0, "lights": 0, "terrains": 0}
	def walk(n, depth):
		stats["nodes"] += 1
		stats["lights"] += "light" in n
		stats["terrains"] += "terrain" in n
		stats["max_depth"] = max(stats["max_depth"], depth)
		if n["model_name"] != INVALID:
			instances[n["model_name"]] = instances.get(n["model_name"], 0) + 1
		for c in n["children"]:
			walk(c, depth + 1)
	walk(root, 0)
	meshlets = sum(n * math.ceil(bounds[bounds_key(model[len(MODEL_PREFIX):])]["tris"] / 110) for model, n in instances.items())
	print(f"{file_name:24s} {sum(instances.values()):6d} instances of {len(instances):2d} meshes, {stats['nodes']:6d} nodes, depth {stats['max_depth']:2d}, ~{meshlets} meshlets, {stats['lights']} lights, {stats['terrains']} terrain")

def write_scene(file_name, root, camera_track):
	root["camera_track"] = camera_track
	with open(os.path.join(OUT_DIR, file_name), "w") as f:
		json.dump(root, f, separators=(",", ":"))
	summarise(file_name, root)

def build_materials():
	"""A showcase of every material feature: alpha tested foliage with opacity and bump maps (CGTree), normal mapped glTF
	(DamagedHelmet), bump mapped scans (head) and texture mapped models (3DBuilder), under coloured lights."""
	random.seed(77)
	items = []
	# A row of alpha tested trees behind everything
	for i in range(5):
		items.append(obj("cgtree", position=(-14 + i * 7 + jitter(1.0), 0, 14 + jitter(1.5)), rotation_deg=(0, random.uniform(0, 360), 0), height=random.uniform(6.5, 8.5)))
	# Helmets on plinths
	for i, yaw in enumerate((-30, 10, 50)):
		x = -5 + i * 5
		items.append(obj("cube", position=(x, 0, 4), height=1.0, scale_xyz=(1.4, 1, 1.4), colour=(0.55, 0.53, 0.5)))
		items.append(obj("helmet", position=(x, 1.75, 4), rotation_deg=(0, 180 + yaw, 0), size=1.4, pivot="centre"))
	# Scanned heads (bump mapped) in a semicircle
	for i in range(3):
		a = math.radians(-35 + i * 35)
		items.append(obj("head", position=(math.sin(a) * 4.0, 0, -1.5 + math.cos(a) * 1.5), rotation_deg=(0, 180 + math.degrees(a), 0), height=1.1,
		                 colour=[(1.0, 0.9, 0.85), (0.85, 0.9, 1.0), (1.0, 1.0, 0.85)][i]))
	# Textured 3DBuilder models along the front
	for i, (name, height) in enumerate((("house", 3.5), ("pumpkin", 0.6), ("sunflower", 1.8), ("trophy", 0.8), ("bulldozer", 1.6), ("cat", 0.9), ("lowpoly_tree", 3.0))):
		items.append(obj(name, position=(-12 + i * 4, 0, -6 - (i % 2) * 2), rotation_deg=(0, 160 + jitter(30), 0), height=height))
	for i in range(30):
		items.append(obj("grass", position=(jitter(16), 0, -9 + jitter(3)), rotation_deg=(0, random.uniform(0, 360), 0), height=random.uniform(0.3, 0.6)))
	# Coloured lights around the exhibits
	for x, z, colour in ((-7, 2, (1.0, 0.5, 0.3)), (0, 7, (0.4, 0.7, 1.0)), (7, 2, (0.8, 0.4, 1.0)), (0, -4, (1.0, 0.85, 0.6))):
		items.append(light_node((x, 3.0, z), colour=colour, intensity=10.0, range_=12.0))

	camera = track(36.0, [
		((0.0, 2.5, -14.0), (0.0, 1.5, 2.0)),
		((-12.0, 3.0, -6.0), (-3.0, 1.5, 3.0)),
		((-10.0, 4.5, 8.0), (0.0, 2.5, 6.0)),
		((0.0, 3.0, 9.0), (0.0, 1.6, 4.0)),
		((10.0, 4.5, 8.0), (0.0, 2.5, 6.0)),
		((12.0, 3.0, -6.0), (3.0, 1.5, 3.0)),
	], near=0.05, far=300.0)
	return node(children=items), camera

def decorate_materials(root, t):
	lights = [sun((-0.4, -0.7, 0.6), colour=(1.0, 0.95, 0.85), intensity=1.2), sun((0.6, -0.3, -0.7), colour=(0.5, 0.6, 0.95), intensity=0.3)]
	return decorate(root, t, environment((0.56, 0.66, 0.9), (0.3, 0.27, 0.23), 0.3, (0.4, 0.52, 0.8), 0.006, 25.0, exposure=0.95), lights,
	                margin=5.0, falloff=25.0, hills=80.0, height=16.0, feature_size=50.0, seed=77, water=True, water_level=-1.0, snow_height=13.0)

def accent_lights(root, camera_track, count, height, intensity, range_, saturation=0.55):
	"""Coloured point lights spread over the content on a jittered grid (deterministic, own random stream)."""
	rng = random.Random(count * 7919)
	lo, hi = content_bounds(root)
	side = max(1, int(math.ceil(math.sqrt(count))))
	lights = []
	for k in range(count):
		gx, gz = k % side, k // side
		x = lo[0] + (hi[0] - lo[0]) * (gx + 0.5 + rng.uniform(-0.3, 0.3)) / side
		z = lo[1] + (hi[1] - lo[1]) * (gz + 0.5 + rng.uniform(-0.3, 0.3)) / side
		lights.append(light_node((x, height, z), colour=hsv(rng.random(), saturation, 1.0), intensity=intensity, range_=range_))
	return lights

def decorate_heads(root, t):
	lo, hi = content_bounds(root)
	cx, cz, r = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, max(hi[0] - lo[0], hi[1] - lo[1], 2.0)
	lights = [sun((-0.5, -0.7, 0.5), intensity=1.0),
	          light_node((cx - r, 2.0, cz - r), colour=(1.0, 0.75, 0.5), intensity=6.0, range_=r * 4),
	          light_node((cx + r, 2.5, cz + r * 0.5), colour=(0.45, 0.6, 1.0), intensity=5.0, range_=r * 4),
	          light_node((cx, 0.6, cz - r * 1.5), colour=(1.0, 0.45, 0.7), intensity=3.0, range_=r * 3)]
	return decorate(root, t, environment((0.55, 0.62, 0.85), (0.32, 0.27, 0.22), 0.3, (0.32, 0.45, 0.78), 0.012, 8.0, exposure=0.9), lights,
	                margin=4.0, falloff=20.0, hills=60.0, height=9.0, feature_size=35.0, seed=11, water=True, water_level=-1.0)

def decorate_spheres(root, t):
	lights = [sun((0.4, -0.55, 0.7), colour=(1.0, 0.93, 0.8), intensity=1.5), sun((-0.6, -0.3, -0.7), colour=(0.5, 0.6, 0.9), intensity=0.35)]
	lights += accent_lights(root, t, 9, 8.0, 40.0, 60.0)
	return decorate(root, t, environment((0.55, 0.68, 0.92), (0.3, 0.28, 0.24), 0.32, (0.4, 0.55, 0.85), 0.0022, 100.0, exposure=0.9), lights,
	                margin=15.0, falloff=60.0, hills=220.0, height=55.0, feature_size=180.0, seed=5, water=True, water_level=-4.0, snow_height=40.0)

def decorate_mixed(root, t):
	lights = [sun((-0.45, -0.75, 0.5), intensity=1.4), sun((0.6, -0.35, -0.6), colour=(0.55, 0.65, 0.95), intensity=0.3)]
	lights += accent_lights(root, t, 12, 3.0, 6.0, 12.0)
	return decorate(root, t, environment((0.58, 0.68, 0.9), (0.32, 0.28, 0.24), 0.32, (0.38, 0.52, 0.84), 0.006, 30.0, exposure=0.9), lights,
	                margin=6.0, falloff=30.0, hills=90.0, height=20.0, feature_size=60.0, seed=3, water=True, water_level=-1.5, snow_height=16.0)

def decorate_village(root, t):
	# Late afternoon: a low warm sun, cool sky fill and the street lamps lit
	lights = [sun((-0.7, -0.25, 0.65), colour=(1.0, 0.62, 0.38), intensity=0.9), sun((0.5, -0.4, -0.75), colour=(0.4, 0.5, 0.9), intensity=0.2)]
	return decorate(root, t, environment((0.42, 0.45, 0.75), (0.3, 0.24, 0.2), 0.22, (0.45, 0.4, 0.6), 0.005, 40.0, exposure=1.0), lights,
	                margin=8.0, falloff=45.0, hills=130.0, height=26.0, feature_size=85.0, seed=2024, water=True, water_level=-1.2, snow_height=22.0)

def decorate_forest(root, t):
	# Hazy morning light through the trees; fireflies and lakes between the hills
	lights = [sun((0.35, -0.6, 0.72), colour=(1.0, 0.92, 0.75), intensity=1.3), sun((-0.5, -0.4, -0.7), colour=(0.45, 0.6, 0.85), intensity=0.35)]
	return decorate(root, t, environment((0.52, 0.66, 0.8), (0.24, 0.27, 0.18), 0.3, (0.5, 0.6, 0.68), 0.0045, 30.0, exposure=0.85), lights,
	                margin=8.0, falloff=50.0, hills=160.0, height=45.0, feature_size=110.0, ridges=0.6, seed=4048, water=True, water_level=-2.0, snow_height=30.0,
	                grass_colour=f3((0.2, 0.36, 0.13)))

def decorate_fractal(root, t):
	# An alien world: violet sun, purple ground, teal rock, magenta lakes
	lights = [sun((-0.3, -0.65, 0.7), colour=(0.85, 0.7, 1.0), intensity=1.1), sun((0.6, -0.35, -0.6), colour=(0.3, 0.8, 0.9), intensity=0.4)]
	return decorate(root, t, environment((0.55, 0.45, 0.85), (0.3, 0.2, 0.32), 0.32, (0.36, 0.26, 0.58), 0.003, 60.0, exposure=0.9), lights,
	                margin=12.0, falloff=70.0, hills=180.0, height=60.0, feature_size=150.0, ridges=0.75, seed=8096, water=True, water_level=-3.0,
	                snow_height=45.0, grass_colour=f3((0.34, 0.24, 0.42)), rock_colour=f3((0.2, 0.5, 0.52)), sand_colour=f3((0.8, 0.55, 0.7)),
	                snow_colour=f3((0.85, 0.95, 1.0)), water_colour=f3((0.55, 0.12, 0.45)))

def update_existing(file_name, camera_track, decoration):
	"""Hand made scenes keep their nodes; their camera track, terrain, lights and environment are (re)written."""
	path = os.path.join(OUT_DIR, file_name)
	with open(path) as f:
		root = json.load(f)
	root.pop("camera_track", None)
	decoration(root, camera_track)
	root["camera_track"] = camera_track
	with open(path, "w") as f:
		json.dump(root, f, separators=(",", ":"))
	summarise(file_name, root)

if __name__ == "__main__":
	update_existing("HeadsTest.json", track(20.0, [
		((0.0, 1.0, -3.0), (0.0, 0.6, 0.0)),
		((-2.5, 2.5, -1.5), (0.0, 0.8, 0.0)),
		((-2.5, 0.5, 2.0), (0.0, 0.5, 0.0)),
		((1.5, 3.0, 2.5), (0.0, 1.0, 0.0)),
		((3.0, 0.8, -1.0), (0.0, 0.6, 0.0)),
	]), decorate_heads)
	update_existing("SpheresTest.json", track(40.0, [
		((0.0, 30.0, -120.0), (0.0, 10.0, 0.0)),
		((-110.0, 50.0, -40.0), (-20.0, 10.0, 0.0)),
		((-80.0, 15.0, 90.0), (0.0, 20.0, 20.0)),
		((40.0, 90.0, 80.0), (0.0, 20.0, 10.0)),
		((120.0, 25.0, -30.0), (20.0, 10.0, 0.0)),
	], near=0.5, far=400.0), decorate_spheres)
	write_scene("MixedAssetsTest.json", decorate_mixed(build_mixed(), MIXED_TRACK), MIXED_TRACK)
	for file_name, builder, decoration in (("MaterialsTest.json", build_materials, decorate_materials), ("VillageTest.json", build_village, decorate_village), ("ForestTest.json", build_forest, decorate_forest),
	                                       ("FractalTest.json", build_fractal, decorate_fractal)):
		root, camera_track = builder()
		write_scene(file_name, decoration(root, camera_track), camera_track)
