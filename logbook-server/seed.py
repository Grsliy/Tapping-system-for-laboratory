"""Skrip buat masukin data uji ke database, supaya endpoint /tap bisa langsung dites."""

from database import get_connection, init_db

init_db()
conn = get_connection()
cursor = conn.cursor()

cursor.execute(
    """
    INSERT OR IGNORE INTO pengunjung (uid_kartu, nama, institusi, tujuan)
    VALUES (?, ?, ?, ?)
    """,
    ("DEADBEEF", "Budi Testing", "UGM", "Uji coba sistem"),
)

conn.commit()
conn.close()
print("Data uji ditambahkan: UID=DEADBEEF, Nama=Budi Testing")
