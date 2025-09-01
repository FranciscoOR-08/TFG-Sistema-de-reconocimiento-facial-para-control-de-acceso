import sqlite3
from datetime import datetime

# Conectar con la base de datos
conn = sqlite3.connect("accesos.db")
cursor = conn.cursor()

# Crear la tabla si no existe
cursor.execute("""
    CREATE TABLE IF NOT EXISTS registros (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        nombre TEXT,
        fecha TEXT
    )
""")
conn.commit()

# Insertar un registro de prueba
nombre = "Usuario de prueba"
fecha = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
cursor.execute("INSERT INTO registros (nombre, fecha) VALUES (?, ?)", (nombre, fecha))
conn.commit()

# Mostrar los registros almacenados
cursor.execute("SELECT * FROM registros")
registros = cursor.fetchall()

print("Registros en la base de datos:")
for r in registros:
    print(r)

# Cerrar la conexión
conn.close()
