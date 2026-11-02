"""Bangun PCB single-layer (B.Cu) logbook RFID versi compact, dengan routing manual.

Disesuaikan dengan casing Casing/kucing-kotak-menyatu: PCB di lantai bak, di bawah modul OLED,
ruang di atas PCB 15.9 mm (di bawah OLED) sampai 20.4 mm (di tempat lain). Karena itu:
  J1 (RC522) dan J2 (OLED) header siku, pin rebah ke tepi PCB, dupont menjulur keluar papan
  J3 (LED) tanpa header, kabel LED disolder langsung ke pad
  JP1 kabel jumper berisolasi di sisi bawah PCB (sisi atas tertutup dupont J1)
Resistor LED THT tidur di PCB, LED di tutup casing.
"""
import sys
import pcbnew
from pcbnew import VECTOR2I_MM as P, FromMM

out = sys.argv[1]
LIB = r'C:/Program Files/KiCad/10.0/share/kicad/footprints/'
TRACK_W = 1.0
PAD_D = 2.0
BOARD_W, BOARD_H = 46, 48

b = pcbnew.BOARD()
ds = b.GetDesignSettings()
ds.m_MinClearance = FromMM(0.4)
ds.m_TrackMinWidth = FromMM(0.8)
ds.SetCopperLayerCount(2)

nets = {}
def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(b, name)
        b.Add(n)
        nets[name] = n
    return nets[name]

def place(lib, name, ref, value, x, y, rot=0, pad_d=PAD_D):
    fp = pcbnew.FootprintLoad(LIB + lib + '.pretty', name)
    fp.SetReference(ref)
    fp.SetValue(value)
    b.Add(fp)
    fp.SetPosition(P(x, y))
    fp.SetOrientationDegrees(rot)
    if pad_d:
        for p in fp.Pads():
            p.SetSize(pcbnew.F_Cu, P(pad_d, pad_d))
    return fp

def pos(p):
    q = p.GetPosition()
    return (round(pcbnew.ToMM(q.x), 3), round(pcbnew.ToMM(q.y), 3))

def pin(fp, num, netname):
    p = fp.FindPadByNumber(str(num))
    p.SetNet(net(netname))
    return pos(p)

def track(netname, *pts):
    for a, c in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(P(*a))
        t.SetEnd(P(*c))
        t.SetWidth(FromMM(TRACK_W))
        t.SetLayer(pcbnew.B_Cu)
        t.SetNet(net(netname))
        b.Add(t)

def text(s, x, y, size=1.0, rot=0):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(s)
    t.SetPosition(P(x, y))
    t.SetLayer(pcbnew.B_Cu)
    t.SetMirrored(True)
    t.SetTextSize(P(size, size))
    t.SetTextThickness(FromMM(0.2))
    t.SetTextAngleDegrees(rot)
    b.Add(t)

# ===== Komponen =====
# ESP32-C3 SuperMini dilihat dari atas, USB menghadap ke atas (y kecil)
uL = place('Connector_PinSocket_2.54mm', 'PinSocket_1x08_P2.54mm_Vertical', 'U1L', 'SuperMini kiri', 14, 6)
uR = place('Connector_PinSocket_2.54mm', 'PinSocket_1x08_P2.54mm_Vertical', 'U1R', 'SuperMini kanan', 29.24, 6)
# J1 pin 1 = SDA RC522 di ujung kanan, pin 8 = 3.3V di ujung kiri; header siku, pin rebah ke +y
# (tepi bawah papan) karena tepat di atasnya ada modul OLED
j1 = place('Connector_PinHeader_2.54mm', 'PinHeader_1x08_P2.54mm_Horizontal', 'J1', 'RC522', 30.78, 35, 270)
# J2 header siku, pin rebah ke -x (tepi kiri papan); diputar 180° lalu pad dinomori ulang menurut
# posisi supaya pin 1 (pad persegi) tetap di atas. Urutan mengikuti modul OLED yang dipakai:
# GND, VDD, SCK, SDA (modul lain ada yang VCC, GND, ...; cek label modul sebelum mencolok)
j2 = place('Connector_PinHeader_2.54mm', 'PinHeader_1x04_P2.54mm_Horizontal', 'J2', 'OLED', 8, 23.78, 180)
for p in j2.Pads():
    num = 1 + round((pcbnew.ToMM(p.GetPosition().y) - 16.16) / 2.54)
    p.SetNumber(str(num))
    p.SetShape(pcbnew.F_Cu, pcbnew.PAD_SHAPE_RECT if num == 1 else pcbnew.PAD_SHAPE_CIRCLE)
# J3 ke LED di casing: 1 = anoda merah, 2 = anoda hijau, 3 = katoda bersama (GND).
# Tanpa header: kabel LED disolder langsung, tutup casing terlalu dekat untuk dupont tegak.
j3 = place('Connector_PinHeader_2.54mm', 'PinHeader_1x03_P2.54mm_Vertical', 'J3', 'LED (solder)', 43.6, 21.24)
r1 = place('Resistor_THT', 'R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal', 'R1', '220', 33, 17)
r2 = place('Resistor_THT', 'R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal', 'R2', '220', 32.5, 23.78)
jp1 = place('Resistor_THT', 'R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal', 'JP1', 'kabel berisolasi', 11.73, 39)
# Sisi atas di titik ini tertutup dupont J1, jadi JP1 dipasang di sisi bawah (ruang standoff 5 mm)
# sebagai kabel berisolasi. Dibalik atas-bawah supaya posisi pad tetap.
jp1.Flip(jp1.GetPosition(), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)
# Dua pad bernomor sama = disambung kabel, supaya DRC tidak menganggapnya putus
jp1.SetDuplicatePadNumbersAreJumpers(True)
jp1.FindPadByNumber('2').SetNumber('1')
HOLES = [(41, 6), (6.25, 33)]
for i, (x, y) in enumerate(HOLES):
    place('MountingHole', 'MountingHole_3.2mm_M3', f'H{i+1}', 'M3', x, y, pad_d=None)

# ===== Pad ke net =====
L = {2: pin(uL, 2, 'GND'), 3: pin(uL, 3, '3V3'), 4: pin(uL, 4, 'SCK'),
     7: pin(uL, 7, 'SCL'), 8: pin(uL, 8, 'SDA')}
R = {1: pin(uR, 1, 'MISO'), 2: pin(uR, 2, 'MOSI'), 3: pin(uR, 3, 'CS'),
     6: pin(uR, 6, 'RST'), 7: pin(uR, 7, 'LED_R'), 8: pin(uR, 8, 'LED_G')}
J1 = {1: pin(j1, 1, 'CS'), 2: pin(j1, 2, 'SCK'), 3: pin(j1, 3, 'MOSI'), 4: pin(j1, 4, 'MISO'),
      6: pin(j1, 6, 'GND'), 7: pin(j1, 7, 'RST'), 8: pin(j1, 8, '3V3')}
J2 = {1: pin(j2, 1, 'GND'), 2: pin(j2, 2, '3V3'), 3: pin(j2, 3, 'SCL'), 4: pin(j2, 4, 'SDA')}
J3 = {1: pin(j3, 1, 'LED_R_A'), 2: pin(j3, 2, 'LED_G_A'), 3: pin(j3, 3, 'GND')}
R1 = (pin(r1, 1, 'LED_R'), pin(r1, 2, 'LED_R_A'))
R2 = (pin(r2, 1, 'LED_G'), pin(r2, 2, 'LED_G_A'))
for p in jp1.Pads():
    p.SetNet(net('SCK'))
JP = tuple(sorted(pos(p) for p in jp1.Pads()))
print('J1', J1); print('J3', J3); print('R1', R1, 'R2', R2); print('JP', JP)

# ===== Routing B.Cu =====
# I2C langsung ke kiri
track('SCL', L[7], J2[3])
track('SDA', L[8], J2[4])
# 3V3: ke OLED (J2 pin 2) masuk dari kanan, ke RC522 lewat bawah modul
track('3V3', L[3], (10.6, 11.08), (10.6, 18.70), J2[2])
track('3V3', L[3], (17.8, 11.08), (17.8, 28.3), (13, 28.3), J1[8])
# GND: ke OLED (J2 pin 1) dan LED lewat tepi papan, ke RC522 lewat bawah modul
track('GND', L[2], (2.5, 8.54), (2.5, 16.16), J2[1])
track('GND', (2.5, 16.16), (2.5, 45), (43.6, 45), J3[3])
track('GND', L[2], (19.6, 8.54), (19.6, 30.1), (18.08, 30.1), J1[6])
# SPI
track('MISO', R[1], (21.4, 6), (21.4, 32.1), (23.16, 32.1), J1[4])
track('MOSI', R[2], (23.2, 8.54), (23.2, 30.3), (25.70, 30.3), J1[3])
track('CS', R[3], (25.0, 11.08), (25.0, 28.5), (30.78, 28.5), J1[1])
track('SCK', L[4], (16.0, 13.62), (16.0, 26.5), (10, 26.5), (10, 39), JP[0])
track('SCK', JP[1], (28.24, 39), J1[2])
track('RST', R[6], (26.8, 18.70), (26.8, 26.5), (34, 26.5), (34, 42), (15.54, 42), J1[7])
# LED status: resistor di PCB, LED di casing lewat J3
track('LED_R', R[7], (31.6, 21.24), (31.6, 17), R1[0])
track('LED_R_A', R1[1], (43.6, 17), J3[1])
track('LED_G', R[8], R2[0])
track('LED_G_A', R2[1], J3[2])

# ===== Outline =====
rect = pcbnew.PCB_SHAPE(b)
rect.SetShape(pcbnew.SHAPE_T_RECT)
rect.SetStart(P(0, 0))
rect.SetEnd(P(BOARD_W, BOARD_H))
rect.SetLayer(pcbnew.Edge_Cuts)
rect.SetWidth(FromMM(0.1))
b.Add(rect)

# ===== Label tembaga (dibaca dari sisi bawah) =====
text('USB', 21.62, 2.3)
text('LOGBOOK RFID', 38.5, 13)
text('LED', 39, 29)
text('RC522', 38.5, 38)
text('JP1', 6.3, 39.6)
text('OLED', 5.4, 27.5, 1.0, 90)

# ===== Ground fill B.Cu =====
# Jalur GND tetap ada sebagai tulang punggung; fill menyambung semuanya dan mengurangi
# tembaga yang harus dietsa.
FILL_CLEARANCE = 0.6
zone = pcbnew.ZONE(b)
zone.SetLayer(pcbnew.B_Cu)
zone.SetNet(net('GND'))
outline = zone.Outline()
outline.NewOutline()
for x, y in [(0, 0), (BOARD_W, 0), (BOARD_W, BOARD_H), (0, BOARD_H)]:
    outline.Append(FromMM(x), FromMM(y))
zone.SetLocalClearance(FromMM(FILL_CLEARANCE))
zone.SetMinThickness(FromMM(0.5))
zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
zone.SetThermalReliefGap(FromMM(0.6))
zone.SetThermalReliefSpokeWidth(FromMM(0.8))
zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
b.Add(zone)
# ZONE_FILLER dari Python crash di KiCad 10.0-rc1; isi zone dengan:
#   kicad-cli pcb drc --refill-zones --save-board logbook-rfid.kicad_pcb

b.Save(out)
print('saved', out)
