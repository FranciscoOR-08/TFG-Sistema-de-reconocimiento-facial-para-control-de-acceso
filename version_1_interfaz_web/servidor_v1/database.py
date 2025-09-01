import sqlite3

# Conectar con SQLite o crear la base de datos si no existe
conn = sqlite3.connect("accesos.db", check_same_thread=False)
cursor = conn.cursor()

# Crear la tabla para almacenar los registros de acceso
cursor.execute("""
    CREATE TABLE IF NOT EXISTS registros (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        nombre TEXT,
        fecha TEXT
    )
""")
conn.commit()

def guardar_acceso(nombre):
    from datetime import datetime
    fecha = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cursor.execute("INSERT INTO registros (nombre, fecha) VALUES (?, ?)", (nombre, fecha))
    conn.commit()
