import datetime
import os
import socket
import threading
import time

import sqlite3

from flask import Flask, jsonify, render_template, request

from database import get_connection, init_db

app = Flask(__name__)

DISCOVERY_PORT = 5001
BROADCAST_INTERVAL = 5  # detik
HTTP_PORT = 5000


def get_local_ip():
    """Cari IP lokal PC ini di jaringan (bukan 127.0.0.1) lewat trik socket:
    connect ke alamat luar tanpa benar-benar kirim data, lalu baca alamat asal
    koneksinya. Lebih akurat daripada socket.gethostbyname(hostname) di PC dengan
    banyak network adapter."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip


def broadcast_server_ip():
    """Kirim broadcast UDP berkala berisi IP server ini, supaya Wemos D1 bisa
    otomatis tahu IP terkini walau IP PC lab berubah-ubah (lihat bagian
    'Catatan soal IP server yang tidak statis' di Sistem-Logbook-Pengunjung-RFID.md)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    while True:
        ip = get_local_ip()
        message = f"LOGBOOK_SERVER|{ip}|{HTTP_PORT}"
        try:
            sock.sendto(message.encode(), ("255.255.255.255", DISCOVERY_PORT))
        except OSError as e:
            print(f"Gagal broadcast: {e}")
        time.sleep(BROADCAST_INTERVAL)


@app.route("/")
def index():
    return "Logbook Pengunjung Server jalan."


@app.route("/tap", methods=["POST"])
def tap():
    data = request.get_json(silent=True) or request.form
    uid = data.get("uid") if data else None

    if not uid:
        return jsonify({"status": "ERROR", "message": "UID tidak ada di request"}), 400

    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute(
        "SELECT * FROM pengunjung WHERE uid_kartu = ? AND status = 'aktif'", (uid,)
    )
    pengunjung = cursor.fetchone()

    if pengunjung is None:
        conn.close()
        return jsonify({"status": "REJECTED", "message": "UID tidak terdaftar"}), 200

    waktu = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cursor.execute(
        "INSERT INTO log_kunjungan (uid_kartu, waktu_tap) VALUES (?, ?)", (uid, waktu)
    )
    conn.commit()
    conn.close()

    return jsonify({"status": "OK", "nama": pengunjung["nama"], "waktu": waktu}), 200


@app.route("/daftar", methods=["GET", "POST"])
def daftar():
    pesan = None
    sukses = False

    if request.method == "POST":
        uid_kartu = request.form.get("uid_kartu", "").strip()
        nama = request.form.get("nama", "").strip()
        institusi = request.form.get("institusi", "").strip()
        tujuan = request.form.get("tujuan", "").strip()

        if not uid_kartu or not nama:
            pesan = "UID kartu dan nama wajib diisi."
        else:
            conn = get_connection()
            cursor = conn.cursor()
            try:
                cursor.execute(
                    """INSERT INTO pengunjung (uid_kartu, nama, institusi, tujuan)
                       VALUES (?, ?, ?, ?)""",
                    (uid_kartu, nama, institusi, tujuan),
                )
                conn.commit()
                pesan = f"Berhasil mendaftarkan {nama} (UID: {uid_kartu})."
                sukses = True
            except sqlite3.IntegrityError:
                pesan = f"UID kartu {uid_kartu} sudah terdaftar sebelumnya."
            finally:
                conn.close()

    return render_template("daftar.html", pesan=pesan, sukses=sukses)


@app.route("/log", methods=["GET"])
def lihat_log():
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute("""
        SELECT log_kunjungan.waktu_tap, pengunjung.nama, pengunjung.institusi
        FROM log_kunjungan
        JOIN pengunjung ON log_kunjungan.uid_kartu = pengunjung.uid_kartu
        ORDER BY log_kunjungan.waktu_tap DESC
    """)
    rows = [dict(row) for row in cursor.fetchall()]
    conn.close()
    return jsonify(rows)


if __name__ == "__main__":
    init_db()

    # Flask debug=True bikin proses reload sendiri (spawn subprocess) -- tanpa dicek,
    # broadcaster bisa jalan dobel (satu di proses "pengawas", satu di proses asli).
    # WERKZEUG_RUN_MAIN cuma "true" di proses yang benar-benar melayani request.
    if os.environ.get("WERKZEUG_RUN_MAIN") == "true" or not app.debug:
        broadcaster_thread = threading.Thread(target=broadcast_server_ip, daemon=True)
        broadcaster_thread.start()
        print(
            f"Broadcaster UDP jalan -> 255.255.255.255:{DISCOVERY_PORT} "
            f"tiap {BROADCAST_INTERVAL} detik"
        )

    # host="0.0.0.0" supaya nanti bisa diakses Wemos D1 dari perangkat lain di jaringan,
    # bukan cuma dari PC ini sendiri.
    app.run(host="0.0.0.0", port=5000, debug=True)
