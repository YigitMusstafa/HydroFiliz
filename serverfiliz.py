"""
HydroFiliz Flask Sunucu
-----------------------
Kurulum : pip install flask
Calistir: python serverfiliz.py
"""

from flask import Flask, request, jsonify, send_from_directory
import sqlite3, os
from datetime import datetime

app = Flask(__name__)
DB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hydrofiliz.db")

# ─── VERİTABANI ──────────────────────────────────────────────────────────────
def db_olustur():
    con = sqlite3.connect(DB)
    cur = con.cursor()

    cur.execute("""
        CREATE TABLE IF NOT EXISTS bitkiler (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            ad           TEXT NOT NULL,
            isim         TEXT,
            ekim_tarihi  TEXT NOT NULL,
            hasat_gun    INTEGER NOT NULL,
            hasatlandi   INTEGER DEFAULT 0,
            hasat_tarihi TEXT,
            notlar       TEXT
        )
    """)

    cur.execute("""
        CREATE TABLE IF NOT EXISTS olcumler (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            bitki_id  INTEGER NOT NULL,
            zaman     TEXT NOT NULL,
            sicaklik  REAL,
            nem       REAL,
            su_sicak  REAL,
            tds       REAL,
            isik      REAL,
            co2       REAL,
            FOREIGN KEY (bitki_id) REFERENCES bitkiler(id)
        )
    """)

    # Eski DB'de "hasatlandı" sütunu varsa yenisine kopyala
    cols = [row[1] for row in cur.execute("PRAGMA table_info(bitkiler)").fetchall()]
    if "hasatlandı" in cols and "hasatlandi" not in cols:
        cur.execute("ALTER TABLE bitkiler ADD COLUMN hasatlandi INTEGER DEFAULT 0")
        cur.execute("UPDATE bitkiler SET hasatlandi = \"hasatlandı\"")

    con.commit()
    con.close()
    print("Veritabani hazir:", DB)

def db_baglan():
    con = sqlite3.connect(DB, check_same_thread=False)
    con.row_factory = sqlite3.Row
    return con

# ─── ESP32'DEN VERİ AL ───────────────────────────────────────────────────────
@app.route("/veri", methods=["POST"])
def veri_al():
    try:
        d = request.get_json(force=True)
        if not d:
            return jsonify({"hata": "JSON bos"}), 400

        bitki_id = d.get("bitki_id")
        if not bitki_id:
            return jsonify({"hata": "bitki_id eksik"}), 400

        con = db_baglan()
        cur = con.cursor()
        bitki = cur.execute("SELECT id FROM bitkiler WHERE id=?", (bitki_id,)).fetchone()
        if not bitki:
            con.close()
            return jsonify({"hata": f"bitki_id={bitki_id} bulunamadi"}), 404

        zaman = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        cur.execute("""
            INSERT INTO olcumler (bitki_id, zaman, sicaklik, nem, su_sicak, tds, isik, co2)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            bitki_id, zaman,
            d.get("sicaklik"), d.get("nem"), d.get("su_sicak"),
            d.get("tds"), d.get("isik"), d.get("co2")
        ))
        con.commit()
        con.close()
        print(f"[{zaman}] Veri alindi -> bitki_id={bitki_id} | "
              f"Sic={d.get('sicaklik')}C Nem={d.get('nem')}% "
              f"TDS={d.get('tds')}ppm CO2={d.get('co2')}ppm")
        return jsonify({"durum": "ok", "zaman": zaman}), 200

    except Exception as e:
        print("HATA:", e)
        return jsonify({"hata": str(e)}), 500

# ─── BİTKİ EKLE ──────────────────────────────────────────────────────────────
@app.route("/bitki/ekle", methods=["POST"])
def bitki_ekle():
    d = request.get_json(force=True)
    con = db_baglan()
    cur = con.cursor()
    cur.execute("""
        INSERT INTO bitkiler (ad, isim, ekim_tarihi, hasat_gun, notlar)
        VALUES (?, ?, ?, ?, ?)
    """, (
        d.get("ad", "Bilinmiyor"),
        d.get("isim", ""),
        d.get("ekim_tarihi", datetime.now().strftime("%Y-%m-%d")),
        d.get("hasat_gun", 30),
        d.get("notlar", "")
    ))
    yeni_id = cur.lastrowid
    con.commit()
    con.close()
    return jsonify({"durum": "ok", "id": yeni_id}), 201

# ─── BİTKİLERİ LİSTELE ───────────────────────────────────────────────────────
# ?hepsi=1  → hasat edilmişler dahil tüm bitkiler (dashboard kullanır)
# (parametre yok) → sadece aktif bitkiler (ESP32 kullanır, max 10)
@app.route("/bitkiler", methods=["GET"])
def bitkiler_listele():
    hepsi = request.args.get("hepsi", "0") == "1"
    filtre = "" if hepsi else "WHERE b.hasatlandi = 0"
    con = db_baglan()
    rows = con.execute(f"""
        SELECT b.*,
               (SELECT COUNT(*) FROM olcumler WHERE bitki_id = b.id) AS olcum_sayisi,
               (SELECT zaman FROM olcumler WHERE bitki_id = b.id ORDER BY id DESC LIMIT 1) AS son_olcum
        FROM bitkiler b
        {filtre}
        ORDER BY b.id DESC
    """).fetchall()
    con.close()
    return jsonify([dict(r) for r in rows])

# ─── TEK BİTKİ BİLGİSİ ──────────────────────────────────────────────────────
@app.route("/bitki/<int:bid>", methods=["GET"])
def bitki_bilgi(bid):
    con = db_baglan()
    row = con.execute("SELECT * FROM bitkiler WHERE id=?", (bid,)).fetchone()
    con.close()
    if not row:
        return jsonify({"hata": "bulunamadi"}), 404
    d = dict(row)
    # Ekim tarihinden bugüne kaç gün geçmiş hesapla
    try:
        from datetime import date
        ekim = date.fromisoformat(d["ekim_tarihi"])
        d["gun"] = (date.today() - ekim).days
    except:
        d["gun"] = 0
    return jsonify(d)

# ─── GRAFİK VERİSİ ───────────────────────────────────────────────────────────
@app.route("/bitki/<int:bid>/grafik", methods=["GET"])
def grafik_verisi(bid):
    limit = request.args.get("limit", 100, type=int)
    con = db_baglan()
    rows = con.execute("""
        SELECT zaman, sicaklik, nem, su_sicak, tds, isik, co2
        FROM olcumler WHERE bitki_id=?
        ORDER BY id DESC LIMIT ?
    """, (bid, limit)).fetchall()
    con.close()
    rows = [dict(r) for r in reversed(rows)]
    return jsonify({
        "zaman":    [r["zaman"]    for r in rows],
        "sicaklik": [r["sicaklik"] for r in rows],
        "nem":      [r["nem"]      for r in rows],
        "su_sicak": [r["su_sicak"] for r in rows],
        "tds":      [r["tds"]      for r in rows],
        "isik":     [r["isik"]     for r in rows],
        "co2":      [r["co2"]      for r in rows],
    })

# ─── SON ÖLÇÜM ────────────────────────────────────────────────────────────────
@app.route("/bitki/<int:bid>/son", methods=["GET"])
def son_olcum(bid):
    con = db_baglan()
    row = con.execute("""
        SELECT * FROM olcumler WHERE bitki_id=? ORDER BY id DESC LIMIT 1
    """, (bid,)).fetchone()
    con.close()
    if not row:
        return jsonify({"hata": "olcum yok"}), 404
    return jsonify(dict(row))

# ─── BİTKİ SİL ───────────────────────────────────────────────────────────────
@app.route("/bitki/<int:bid>/sil", methods=["DELETE"])
def bitki_sil(bid):
    con = db_baglan()
    con.execute("DELETE FROM olcumler WHERE bitki_id=?", (bid,))
    con.execute("DELETE FROM bitkiler WHERE id=?", (bid,))
    con.commit()
    con.close()
    return jsonify({"durum": "silindi"})

# ─── HASAT ET ────────────────────────────────────────────────────────────────
@app.route("/bitki/<int:bid>/hasat", methods=["POST"])
def hasat_et(bid):
    con = db_baglan()
    con.execute("""
        UPDATE bitkiler SET hasatlandi=1, hasat_tarihi=? WHERE id=?
    """, (datetime.now().strftime("%Y-%m-%d"), bid))
    con.commit()
    con.close()
    return jsonify({"durum": "hasatlandi"})

# ─── VERİTABANI GÖRÜNTÜLE ────────────────────────────────────────────────────
@app.route("/tablo")
def tablo_goster():
    con = db_baglan()
    bitkiler = [dict(r) for r in con.execute("SELECT * FROM bitkiler ORDER BY id DESC").fetchall()]
    olcumler = [dict(r) for r in con.execute(
        "SELECT * FROM olcumler ORDER BY id DESC LIMIT 200").fetchall()]
    con.close()

    def tablo_html(baslik, satirlar):
        if not satirlar:
            return f"<h2>{baslik}</h2><p style='color:#999'>Kayıt yok.</p>"
        sutunlar = list(satirlar[0].keys())
        th = "".join(f"<th>{s}</th>" for s in sutunlar)
        rows = ""
        for r in satirlar:
            rows += "<tr>" + "".join(f"<td>{r[s]}</td>" for s in sutunlar) + "</tr>"
        return f"""<h2>{baslik} <span style='font-size:13px;color:#888'>({len(satirlar)} kayıt)</span></h2>
        <table>{th}{rows}</table>"""

    html = f"""<!DOCTYPE html><html lang='tr'><head><meta charset='UTF-8'>
    <title>HydroFiliz DB</title>
    <style>
        body {{ font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }}
        h2 {{ color: #2e7d32; margin-top: 30px; }}
        table {{ border-collapse: collapse; background: white; width: 100%;
                 margin-bottom: 30px; box-shadow: 0 1px 4px rgba(0,0,0,0.1); }}
        th {{ background: #2e7d32; color: white; padding: 8px 12px; text-align: left; font-size: 13px; }}
        td {{ padding: 7px 12px; font-size: 12px; border-bottom: 1px solid #eee; }}
        tr:hover td {{ background: #f9f9f9; }}
    </style></head><body>
    <h1 style='color:#1b5e20'>🌱 HydroFiliz — Veritabanı</h1>
    {tablo_html("Bitkiler", bitkiler)}
    {tablo_html("Ölçümler (son 200)", olcumler)}
    </body></html>"""
    return html

# ─── WEB DASHBOARD ───────────────────────────────────────────────────────────
@app.route("/")
def anasayfa():
    return send_from_directory(".", "dashboard.html")

# ─── ÇALIŞTIR ────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    db_olustur()
    print("\nHydroFiliz Sunucu Basliyor...")
    print("Dashboard : http://localhost:5000")
    print("Bitkiler  : http://localhost:5000/bitkiler")
    print("DB        : hydrofiliz.db\n")
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
