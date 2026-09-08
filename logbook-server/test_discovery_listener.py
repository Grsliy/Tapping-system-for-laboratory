"""
Script simulasi buat tes broadcaster UDP di app.py, tanpa perlu Wemos D1 fisik.

Meniru apa yang nanti dilakukan Wemos D1: dengerin broadcast di port 5001,
print pesan yang diterima. Jalankan ini di jendela terpisah SAMBIL app.py jalan.
"""

import socket

DISCOVERY_PORT = 5001

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", DISCOVERY_PORT))

print(f"Dengerin broadcast di port {DISCOVERY_PORT}... (Ctrl+C buat berhenti)")

while True:
    data, addr = sock.recvfrom(1024)
    message = data.decode(errors="ignore")
    print(f"Diterima dari {addr[0]}: {message}")
