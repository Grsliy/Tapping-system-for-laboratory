import sqlite3

DB_NAME = "logbook.db"


def get_connection():
    conn = sqlite3.connect(DB_NAME)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS pengunjung (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            uid_kartu TEXT UNIQUE NOT NULL,
            nama TEXT NOT NULL,
            institusi TEXT,
            tujuan TEXT,
            tanggal_daftar TEXT DEFAULT CURRENT_TIMESTAMP,
            status TEXT DEFAULT 'aktif'
        )
    """)

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS log_kunjungan (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            uid_kartu TEXT NOT NULL,
            waktu_tap TEXT DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (uid_kartu) REFERENCES pengunjung (uid_kartu)
        )
    """)

    conn.commit()
    conn.close()
    print("Database siap (logbook.db).")


if __name__ == "__main__":
    init_db()
