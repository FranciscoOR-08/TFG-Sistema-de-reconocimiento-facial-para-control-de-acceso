"""
===========================================
Proyecto TFG – Sistema de Control de Acceso Facial
Backend con FastAPI + InsightFace
Autor: Francisco

Descripción:
  - Servidor FastAPI para control de acceso mediante reconocimiento facial.
  - Gestión de embeddings locales (servidor) y remotos (ESP32).
  - Uso de InsightFace para extracción de embeddings.
  - Base de datos SQLite para registrar resultados de reconocimiento.
===========================================
"""


from fastapi import FastAPI, HTTPException, Request, Query
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Optional, List
import sqlite3
import os
import pickle
import numpy as np
import cv2
from insightface.app import FaceAnalysis

# =====================================================
# CONFIGURACIÓN Y VARIABLES GLOBALES
# =====================================================

app = FastAPI()

# CORS (para desarrollo; ajustar en producción)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Archivos de base de datos
FACE_DB_FILE = "face_db.pkl"          # Embeddings del servidor
db_path_esp32 = "face_db_esp32.pkl"  # Embeddings enviados desde ESP32

# Buffer temporal para enrolamiento (varias muestras antes de guardar)
enroll_buffer = {}  # {nombre: [embedding1, embedding2, ...]}
NUM_EMBEDDINGS_REQUIRED = 3

# =====================================================
# CARGA INICIAL DE BASES DE DATOS DE EMBEDDINGS
# =====================================================

# Inicialización InsightFace (modelo buffalo_l en CPU)
face = FaceAnalysis(name="buffalo_l", providers=["CPUExecutionProvider"])
face.prepare(ctx_id=0)

# Cargar embeddings del servidor
if os.path.exists(FACE_DB_FILE):
    with open(FACE_DB_FILE, "rb") as f:
        face_db = pickle.load(f)
else:
    face_db = {}

# Cargar embeddings recibidos desde ESP32
if os.path.exists(db_path_esp32):
    with open(db_path_esp32, "rb") as f:
        face_db_esp32 = pickle.load(f)
else:
    face_db_esp32 = {}

# =====================================================
# FUNCIONES AUXILIARES PARA GESTIÓN DE BASES DE DATOS
# =====================================================

def save_face_db():
    """Guardar embeddings del servidor en archivo pickle"""
    with open(FACE_DB_FILE, "wb") as f:
        pickle.dump(face_db, f)
        
def save_face_db_esp32():
    """Guardar embeddings enviados desde ESP32 en archivo pickle"""
    with open(db_path_esp32, "wb") as f:
        pickle.dump(face_db_esp32, f, protocol=pickle.HIGHEST_PROTOCOL)

def convert_np_arrays_to_lists(obj):
    """Convertir numpy arrays a listas para serialización JSON"""
    if isinstance(obj, dict):
        return {k: convert_np_arrays_to_lists(v) for k, v in obj.items()}
    elif isinstance(obj, np.ndarray):
        return obj.tolist()
    elif isinstance(obj, list):
        return [convert_np_arrays_to_lists(i) for i in obj]
    else:
        return obj

# =====================================================
# FUNCIONES PARA BASE DE DATOS SQLITE DE RESULTADOS
# =====================================================
"""
def create_table():
    Crear tabla SQLite si no existe
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS recognition_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            status TEXT,
            message TEXT,
            face_id INTEGER,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    conn.commit()
    conn.close()

def insert_result(status: str, message: str, face_id: int):
    Insertar un nuevo resultado en la base de datos
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO recognition_results (status, message, face_id) 
        VALUES (?, ?, ?)
    ''', (status, message, face_id))
    conn.commit()
    conn.close()
"""
# =====================================================
# FUNCIONES PARA BASE DE DATOS SQLITE DE RESULTADOS
# =====================================================

def create_table():
    """Crear tabla SQLite si no existe"""
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS recognition_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            status TEXT,
            message TEXT,
            face_id INTEGER,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            origin TEXT DEFAULT 'SERVER'   -- 👈 nuevo campo
        )
    ''')
    conn.commit()
    conn.close()

# 👇 ejecuta esto al arrancar
create_table()

def insert_result(status: str, message: str, face_id: int, origin: str = "SERVER"):
    """Insertar un nuevo resultado en la base de datos"""
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO recognition_results (status, message, face_id, origin) 
        VALUES (?, ?, ?, ?)
    ''', (status, message, face_id, origin))
    conn.commit()
    conn.close()

def get_results():
    """Obtener todos los resultados guardados"""
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM recognition_results")
    rows = cursor.fetchall()
    conn.close()
    return rows

def delete_all_results():
    """Eliminar todos los registros de resultados"""
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute("DELETE FROM recognition_results")
    conn.commit()
    conn.close()

def delete_result_by_id(result_id: int):
    """Eliminar un resultado específico por ID"""
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute("DELETE FROM recognition_results WHERE id = ?", (result_id,))
    conn.commit()
    conn.close()

# =====================================================
# MODELOS Pydantic PARA VALIDACIÓN DE DATOS
# =====================================================

class EmbeddingData(BaseModel):
    """Datos recibidos de embeddings"""
    embedding: List[float]
    nombre: Optional[str] = None  # Para enrolamiento

class FaceRecognitionResult(BaseModel):
    """Resultado de reconocimiento facial"""
    status: str
    message: str
    face_id: Optional[int] = None

# =====================================================
# FUNCIONES AUXILIARES DE IMÁGENES Y EMBEDDINGS
# =====================================================

def image_bytes_to_bgr(image_bytes):
    """Convierte bytes de imagen JPEG a formato BGR (OpenCV)"""
    image_np = np.frombuffer(image_bytes, np.uint8)
    return cv2.imdecode(image_np, cv2.IMREAD_COLOR)

def compare_embeddings(embedding1, embedding2):
    """Compara dos embeddings normalizados con producto punto"""
    embedding1 = embedding1 / np.linalg.norm(embedding1)
    embedding2 = embedding2 / np.linalg.norm(embedding2)
    return np.dot(embedding1, embedding2)

# =====================================================
# ENDPOINTS PARA GESTIÓN DE EMBEDDINGS (ESP32)
# =====================================================

@app.get("/get-embeddings")
async def get_embeddings():
    """Devuelve todos los embeddings ESP32 guardados"""
    if not os.path.exists(db_path_esp32):
        return JSONResponse(content={})
    try:
        with open(db_path_esp32, "rb") as f:
            face_db_esp32_loaded = pickle.load(f)
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"error": "Error leyendo embeddings ESP32", "detail": str(e)}
        )
    return JSONResponse(content=face_db_esp32_loaded)

@app.post("/upload-embedding")
async def upload_embedding(data: EmbeddingData, modo: str = Query(..., enum=["enroll", "recognize"])):
    """Recibe embeddings enviados desde ESP32 (enroll o recognize)"""
    embedding_list = data.embedding
    if len(embedding_list) != 512:
        raise HTTPException(status_code=400, detail="Embedding length incorrecta")

    if modo == "enroll":
        if not data.nombre:
            return {"status": "error", "message": "Falta el nombre"}
        face_db_esp32[data.nombre] = embedding_list
        save_face_db_esp32()
        return {"status": "success", "message": f"{data.nombre} registrado"}

    return {"status": "error", "message": "Modo recognize no implementado en servidor"}

@app.post("/clear-embeddings-esp32")
async def clear_embeddings():
    """Elimina todos los embeddings guardados por ESP32"""
    global face_db_esp32
    face_db_esp32 = {}
    save_face_db_esp32()
    return {"status": "success", "message": "Embeddings eliminados"}

@app.delete("/delete-embedding-esp32/{name}")
async def delete_embedding(name: str):
    """Elimina un embedding específico (ESP32)"""
    if name in face_db_esp32:
        del face_db_esp32[name]
        save_face_db_esp32()
        return {"status": "success", "message": f"Embedding '{name}' eliminado"}
    else:
        return {"status": "error", "message": f"'{name}' no encontrado"}

# =====================================================
# ENDPOINT PARA SUBIDA DE IMÁGENES (detect, recognize, enroll)
# =====================================================

@app.post("/upload-image")
async def upload_image(
    request: Request,
    modo: str = Query(..., enum=["detect", "recognize", "enroll"]),
    nombre: Optional[str] = None
):
    """Recibe imagen y procesa según modo seleccionado"""
    contents = await request.body()
    img = image_bytes_to_bgr(contents)
    faces = face.get(img)

    if not faces:
        return {"status": "error", "message": "Ningún rostro detectado"}

    embedding = faces[0].embedding

    # --- DETECTAR ---
    if modo == "detect":
        return {"status": "success", "message": "Rostro detectado"}

    # --- RECONOCER ---
    elif modo == "recognize":
        if not face_db:
            return {"status": "error", "message": "Base de datos vacía"}

        best_match, best_score = None, -1
        for nombre_registrado, emb_registrado in face_db.items():
            score = compare_embeddings(embedding, emb_registrado)
            if score > best_score:
                best_score, best_match = score, nombre_registrado

        if best_score > 0.6:
            insert_result("success", best_match, -1, origin="SERVER")
            return {"status": "success", "message": best_match}
        else:
            insert_result("error", "Usuario desconocido", -1, origin="SERVER")
            return {"status": "error", "message": "Usuario Desconocido"}

    # --- ENROLAR ---
    elif modo == "enroll":
        if not nombre:
            return {"status": "error", "message": "Missing 'nombre' parameter for enrollment"}

        if nombre not in enroll_buffer:
            enroll_buffer[nombre] = []

        enroll_buffer[nombre].append(embedding)
        count = len(enroll_buffer[nombre])

        if count < NUM_EMBEDDINGS_REQUIRED:
            return {
                "status": "partial",
                "message": f"Muestra {count} recibida para '{nombre}'. Faltan {NUM_EMBEDDINGS_REQUIRED - count}."
            }

        avg_embedding = np.mean(enroll_buffer[nombre], axis=0)
        face_db[nombre] = avg_embedding
        save_face_db()
        del enroll_buffer[nombre]

        return {
            "status": "success",
            "message": f"Usuario '{nombre}' registrado con {NUM_EMBEDDINGS_REQUIRED} muestras."
        }

    return {"status": "error", "message": "Unhandled mode"}

# =====================================================
# ENDPOINTS PARA GESTIÓN DE EMBEDDINGS (SERVIDOR)
# =====================================================

@app.get("/get-embeddings-servidor")
def get_embeddings_servidor():
    """Lista los nombres de embeddings guardados en servidor"""
    return list(face_db.keys())

@app.get("/get-embeddings-servidor-todo")
def get_embeddings_servidor_todo():
    """Devuelve todo el diccionario de embeddings del servidor"""
    converted = {
        k: v.tolist() if isinstance(v, np.ndarray) else v
        for k, v in face_db.items()
    }
    return JSONResponse(content=converted)

@app.delete("/delete-embedding-servidor/{nombre}")
async def delete_embedding_by_name(nombre: str):
    """Elimina un embedding específico del servidor"""
    if nombre in face_db:
        del face_db[nombre]
        save_face_db()
        return {"status": "success", "message": f"Embedding '{nombre}' eliminado"}
    else:
        raise HTTPException(status_code=404, detail=f"Embedding '{nombre}' no encontrado")

@app.post("/clear-embeddings-servidor")
def clear_embeddings_servidor():
    """Elimina todos los embeddings del servidor"""
    face_db.clear()
    save_face_db()
    return {"status": "success", "message": "Embeddings del servidor eliminados"}

# =====================================================
# ENDPOINTS PARA RESULTADOS DE RECONOCIMIENTO
# =====================================================

@app.post("/recognition-result/")
async def recognition_result(result: FaceRecognitionResult):
    """Recibe y guarda un resultado de reconocimiento en la BD (origen ESP32)"""
    insert_result(result.status, result.message, result.face_id, origin="ESP32")
    return {"message": "Result received", "status": "success"}


@app.get("/recognition-result/")
async def get_all_results():
    """Devuelve todos los resultados almacenados"""
    results = get_results()
    if not results:
        raise HTTPException(status_code=404, detail="No results found")

    formatted = [
        {
            "id": row[0],
            "status": row[1],
            "message": row[2],
            "face_id": row[3],
            "timestamp": row[4],
            "origin": row[5]   # 👈 ahora se incluye
        }
        for row in results
    ]
    return {"results": formatted}


@app.delete("/recognition-result/")
async def delete_all():
    """Elimina todos los resultados de la BD"""
    delete_all_results()
    return {"message": "All results deleted"}

@app.delete("/recognition-result/{result_id}")
async def delete_by_id(result_id: int):
    """Elimina un resultado específico por ID"""
    delete_result_by_id(result_id)
    return {"message": f"Result with ID {result_id} deleted"}