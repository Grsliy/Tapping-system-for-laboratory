import datetime

from flask import Flask, jsonify, request

from database import get_connection, init_db

app = Flask(__name__)


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
    # host="0.0.0.0" supaya nanti bisa diakses ESP-01 dari perangkat lain di jaringan,
    # bukan cuma dari PC ini sendiri.
    app.run(host="0.0.0.0", port=5000, debug=True)
