"""Casing logbook RFID: kotak elektronik menyatu di punggung kucing, miring 20° dari meja.

Badan kucing memakai model "Cute Cat Phone Holder - Support Free" karya ABoyNamedSue
(https://www.thingiverse.com/thing:7095121), lisensi Creative Commons Attribution-ShareAlike.
Turunan ini mengikuti lisensi yang sama (CC BY-SA) dan wajib mencantumkan kredit tersebut bila
dibagikan. Berkas aslinya ada di ../reference/.

Kucing diperbesar 1.5x, lalu bak kotak dipasang di punggungnya dengan kemiringan 20° (sisi depan
rendah, dekat pengguna). Bagian kucing yang menembus bak dipotong, jadi kucing dan bak menjadi satu
benda dan kepala kucing menyembul di depan kotak. Muka kotak adalah tutup terpisah yang disekrup:
area tap (RC522) di depan, OLED dan LED di belakang yang lebih tinggi. PCB di lantai bak, di bawah
area OLED, jadi tidak ada di belakang antena RC522.

Koordinat dunia: x ke kanan, y menjauhi pengguna, z ke atas (z=0 meja). Kucing menghadap -y.
Koordinat lokal bak: u = x, v sepanjang muka ke belakang-atas, t tegak lurus muka (t=0 permukaan
atas lantai bak).

Dua bagian:
  kucing-kotak.stl  kucing + bak, dicetak berdiri; sayap bak yang menggantung di kiri-kanan dan di
                    atas tengkuk perlu support
  tutup.stl         dicetak muka menghadap bed, tanpa support

Pemakaian: python casing_kucing_kotak_menyatu.py <folder_output> [rakitan.stl]
Butuh: pip install manifold3d numpy
"""
import sys
import math
import os
import numpy as np
from manifold3d import CrossSection, Manifold, Mesh, JoinType

OUT = sys.argv[1]
ASSEMBLY = sys.argv[2] if len(sys.argv) > 2 else None
HERE = os.path.dirname(os.path.abspath(__file__))
REF_STL = os.path.join(HERE, '..', 'reference', 'files', 'Cat_Phone_Holder_Small_Head.stl')
SEG = 96
K = dict(join_type=JoinType.Round, circular_segments=SEG)

WALL = 2.4       # tebal dinding bak
FLOOR = 2.4      # tebal lantai bak
LID = 2.4        # tebal tutup
ENGRAVE = 0.8    # kedalaman ukiran
CAT_SCALE = 1.5
TILT = 20.0      # kemiringan muka kotak dari meja

# ===== Utilitas 2D/3D =====
def circle(r, x=0, y=0):
    return CrossSection.circle(r, SEG).translate((x, y))

def rect(w, h, x=0, y=0):
    return CrossSection.square((w, h), True).translate((x, y))

def rrect(w, h, r, x=0, y=0):
    return rect(w - 2 * r, h - 2 * r, x, y).offset(r, **K)

def poly(pts):
    # CrossSection hanya mengisi poligon berlawanan arah jarum jam
    area = sum(x1 * y2 - x2 * y1 for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]))
    return CrossSection([pts if area > 0 else pts[::-1]])

def union(items):
    out = items[0]
    for it in items[1:]:
        out = out + it
    return out

def stroke(points, w):
    """Garis tebal w melewati titik-titik (ujung membulat), untuk huruf dan simbol."""
    return union([CrossSection.hull(circle(w / 2, *a) + circle(w / 2, *b))
                  for a, b in zip(points, points[1:])])

def ring(shape, w):
    return shape - shape.offset(-w, **K)

def prism(cs, z0, z1):
    return cs.extrude(z1 - z0).translate((0, 0, z0))

def rprism(cs, z0, z1, r_bot=0.0, r_top=0.0, step=0.3):
    """Prisma dengan tepi bawah/atas dibulatkan, dibangun dari irisan setebal ~step mm."""
    parts = [prism(cs, z0 + r_bot, z1 - r_top)]
    for r, sign in ((r_bot, -1), (r_top, 1)):
        n = max(1, int(np.ceil(r / step))) if r > 0 else 0
        for i in range(n):
            h = (i + 0.5) / n * r
            layer = cs.offset(-(r - math.sqrt(max(r * r - h * h, 0.0))), **K)
            if layer.is_empty():
                continue
            za = z1 - r + i * r / n if sign > 0 else z0 + r - (i + 1) * r / n
            parts.append(prism(layer, za, za + r / n + 0.001))
    return union(parts)

def letters_tap(x0, y0, h=7.0, w=1.6):
    T_ = stroke([(x0, y0 + h), (x0 + 5, y0 + h)], w) + stroke([(x0 + 2.5, y0 + h), (x0 + 2.5, y0)], w)
    A = (stroke([(x0 + 7, y0), (x0 + 9.5, y0 + h), (x0 + 12, y0)], w) +
         stroke([(x0 + 8, y0 + 2.8), (x0 + 11, y0 + 2.8)], w))
    Pp = stroke([(x0 + 14.5, y0), (x0 + 14.5, y0 + h), (x0 + 17.5, y0 + h), (x0 + 18.8, y0 + h - 1.4),
                 (x0 + 18.8, y0 + h - 2.2), (x0 + 17.5, y0 + h - 3.6), (x0 + 14.5, y0 + h - 3.6)], w)
    return T_ + A + Pp

def paw(cx, cy, s=1.0):
    pad = CrossSection.hull(circle(5.2 * s, cx - 2.2 * s, cy - 7 * s) + circle(5.2 * s, cx + 2.2 * s, cy - 7 * s) +
                            circle(4.0 * s, cx, cy - 11.5 * s))
    toes = [circle(r * s, cx + dx * s, cy + dy * s)
            for dx, dy, r in [(-9, 1, 2.9), (-3.3, 5, 3.1), (3.3, 5, 3.1), (9, 1, 2.9)]]
    return union([pad] + toes)

def load_stl(path):
    data = open(path, 'rb').read()
    n = int(np.frombuffer(data[80:84], np.uint32)[0])
    tri = np.frombuffer(data[84:84 + n * 50], dtype=[('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')])['v']
    verts, idx = np.unique(tri.reshape(-1, 3).round(4), axis=0, return_inverse=True)
    m = Manifold(Mesh(vert_properties=verts.astype(np.float32), tri_verts=idx.reshape(-1, 3).astype(np.uint32)))
    assert m.status().name == 'NoError', m.status()
    return m

# ===== Bak dan tutup (koordinat lokal u, v, t) =====
BOX_W, BOX_L = 76.0, 98.0
BOX_R = 10.0
INNER_H = 27.0                    # tinggi ruang dalam bak, dari lantai sampai bawah tutup
outline = rrect(BOX_W, BOX_L, BOX_R, 0, BOX_L / 2)
inner = outline.offset(-WALL, **K)

RFID_C = (0, 25)                  # RC522 60x40 melintang, di depan (dekat pengguna)
OLED_C = (0, 72)                  # modul OLED 27.3x27.8, header di sisi belakang (v+)
OLED_WIN = (25, 15)
OLED_WIN_C = (0, 70.5)
LEDS = [(-25, 72), (25, 72)]      # kiri hijau (✓), kanan merah (✗), dilihat dari depan
# PCB compact 46x48 di lantai bak (komponen menghadap atas), di bawah area OLED, diputar 90° sehingga
# tepi USB menghadap dinding kanan dan J1 rebah ke dinding kiri: u = 23 - y_pcb, v = 92.2 - x_pcb.
# Lubang PCB (2.2, 6) dan (36.95, 33) jatuh tepat di standoff ini (lihat PCB/buat_pcb.py).
PCB_HOLES = [(17, 90), (-10, 55.25)]
USB_V, USB_T = 70.6, 17.7         # sumbu konektor USB-C (socket female 8.5 mm + SuperMini)
LID_SCREWS = [(-33.5, 50), (33.5, 50), (-33.5, 92), (33.5, 92)]

tub = prism(outline, -FLOOR, INNER_H) - prism(inner, 0, INNER_H + 1)
for sx, sy in LID_SCREWS:
    tub += prism(circle(3.5, sx, sy), 0, INNER_H)
    tub -= prism(circle(1.25, sx, sy), INNER_H - 10, INNER_H + 1)
for hx, hy in PCB_HOLES:
    tub += prism(circle(3.0, hx, hy), -0.01, 5)                           # standoff PCB 5 mm
    tub -= prism(circle(1.25, hx, hy), -1, 5.1)
tub -= Manifold.cube((4 * WALL, 12, 16), True).translate((BOX_W / 2, USB_V, USB_T))   # kabel USB, dinding kanan

# tutup: muka kotak, t = INNER_H .. INNER_H + LID
lid = rprism(outline, 0, LID, r_top=2.0)
lid -= prism(rrect(*OLED_WIN, 1.5, *OLED_WIN_C), -1, LID + 1)
for lx, ly in LEDS:
    lid -= prism(circle(2.6, lx, ly), -1, LID + 1)
for sx, sy in LID_SCREWS:
    lid -= prism(circle(1.7, sx, sy), -1, LID + 1)
    lid -= prism(circle(3.2, sx, sy), LID - 1.2, LID + 1)                 # kepala sekrup tenggelam
tx, ty = RFID_C
deco = [ring(circle(19, tx, ty + 1), 1.4), paw(tx, ty + 7, 0.8), letters_tap(tx - 9.4, ty - 14),
        ring(rrect(OLED_WIN[0] + 5, OLED_WIN[1] + 5, 3, *OLED_WIN_C), 0.9)]
for rr in (5, 8):
    deco.append(ring(circle(rr, tx + 11, ty + 11), 1.2) ^
                poly([(tx + 11, ty + 11), (tx + 30, ty + 17), (tx + 17, ty + 30)]))
gx, gy = LEDS[0]
deco.append(stroke([(gx - 3, gy + 8.5), (gx - 1, gy + 6.5), (gx + 3, gy + 10.5)], 1.2))   # ✓
rx, ry = LEDS[1]
deco.append(stroke([(rx - 2.5, ry + 6), (rx + 2.5, ry + 11)], 1.2) +
            stroke([(rx - 2.5, ry + 11), (rx + 2.5, ry + 6)], 1.2))                        # ✗
for lx, ly in LEDS:
    deco.append(ring(circle(4.6, lx, ly), 0.9))
for d in deco:
    lid -= prism(d, LID - ENGRAVE, LID + 1)
# dudukan di balik tutup
f = ring(rect(60.6 + 2.4, 40.6 + 2.4, *RFID_C), 1.2) - rect(8, 30, RFID_C[0] + 31, RFID_C[1])
lid += prism(f, -4, 0.01)
oled_frame = ring(rect(27.7 + 2.4, 28.2 + 2.4, *OLED_C), 1.2) - rect(14, 6, OLED_C[0], OLED_C[1] + 14.5)
lid += prism(oled_frame, -4, 0.01)
for lx, ly in LEDS:
    lid += prism(ring(circle(4.2, lx, ly), 1.6), -4, 0.01)   # pendek: colokan USB lewat di bawahnya

# ===== Posisi bak di punggung kucing =====
_c, _s = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))
FRONT_Y, FRONT_Z = -18.0, 30.0    # tepi depan sisi bawah lantai bak (di atas tengkuk kucing)
O = (0.0, FRONT_Y - FLOOR * _s, FRONT_Z + FLOOR * _c)   # titik lokal (u=0, v=0, t=0)

def place(m):
    return m.rotate((TILT, 0, 0)).translate(O)

# ===== Kucing =====
cat = load_stl(REF_STL).scale((CAT_SCALE, CAT_SCALE, CAT_SCALE))
cut = place(prism(outline, -FLOOR, 200))          # semua bagian kucing di atas lantai bak
body = (cat - cut) + place(tub)
lid_w = place(lid.translate((0, 0, INNER_H)))

# ===== Simpan =====
def save_stl(m, path):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3].astype(np.float32)
    t = np.asarray(mesh.tri_verts)
    tri = v[t]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    rec = np.zeros(len(t), dtype=[('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')])
    rec['n'] = n
    rec['v'] = tri
    with open(path, 'wb') as fh:
        fh.write(b'logbook rfid kucing kotak, remix CC BY-SA thing:7095121'.ljust(80, b' '))
        fh.write(np.uint32(len(t)).tobytes())
        fh.write(rec.tobytes())
    return len(t)

parts = {'kucing-kotak': body, 'tutup': lid.mirror((0, 0, 1))}   # tutup: muka di bed
for name, m in parts.items():
    assert m.status().name == 'NoError', (name, m.status())
    m = m.translate((0, 0, -m.bounding_box()[2]))
    lo, hi = m.bounding_box()[:3], m.bounding_box()[3:]
    ntri = save_stl(m, f'{OUT}/{name}.stl')
    print(f'{name}: {ntri} segitiga, volume {m.volume()/1000:.1f} cm3, '
          f'ukuran {hi[0]-lo[0]:.1f} x {hi[1]-lo[1]:.1f} x {hi[2]-lo[2]:.1f} mm')

if ASSEMBLY:
    asm = body + lid_w
    save_stl(asm, ASSEMBLY)
    bb = asm.bounding_box()
    print('rakitan: %.1f x %.1f x %.1f mm' % (bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]))
