"""
=====================================================
Proyecto TFG – Sistema de Control de Acceso Facial
Backend con FastAPI + InsightFace
Autor: Francisco

Descripción:
  - Servidor FastAPI para control de acceso mediante reconocimiento facial.
  - Gestión de embeddings locales (servidor) y remotos (ESP32).
  - Comunicación en tiempo real con WebSocket para streaming.
  - Base de datos SQLite para registrar accesos y su origen (SERVER o ESP32).
=====================================================
"""

# =====================================================
# LIBRERÍAS
# =====================================================
from fastapi import FastAPI, HTTPException, Request, Query, WebSocket
from fastapi.responses import JSONResponse, HTMLResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi import Form
from pydantic import BaseModel
from typing import Optional, List
import asyncio
import httpx
import sqlite3
import numpy as np
import cv2
import os
import pickle
import json
import datetime
import traceback

from insightface.app import FaceAnalysis

# =====================================================
# CONFIGURACIÓN FASTAPI Y CORS
# =====================================================
app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],   # Permitir todas las orígenes en desarrollo
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# =====================================================
# BASE DE DATOS SQLITE
# =====================================================
def create_table():
    """Crea la tabla de resultados de reconocimiento (si no existe)."""
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()

    cursor.execute('''
        CREATE TABLE IF NOT EXISTS recognition_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            status TEXT,
            message TEXT,
            face_id INTEGER,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            origin TEXT DEFAULT 'SERVER'
        )
    ''')
    conn.commit()
    conn.close()

create_table()

def insert_result(status: str, message: str, face_id: int, origin: str = "SERVER"):
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO recognition_results (status, message, face_id, origin) 
        VALUES (?, ?, ?, ?)
    ''', (status, message, face_id, origin))
    conn.commit()
    conn.close()

def get_results():
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM recognition_results")
    rows = cursor.fetchall()
    conn.close()
    return rows

def delete_all_results():
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute("DELETE FROM recognition_results")
    conn.commit()
    conn.close()

def delete_result_by_id(result_id: int):
    conn = sqlite3.connect('accesos.db')
    cursor = conn.cursor()
    cursor.execute("DELETE FROM recognition_results WHERE id = ?", (result_id,))
    conn.commit()
    conn.close()

# =====================================================
# MODELOS Pydantic
# =====================================================
class FaceRecognitionResult(BaseModel):
    status: str
    message: str
    face_id: Optional[int] = None

class EmbeddingData(BaseModel):
    embedding: List[float]
    nombre: Optional[str] = None

# =====================================================
# ENDPOINTS – RESULTADOS DE RECONOCIMIENTO
# =====================================================
@app.post("/recognition-result/")
async def recognition_result(result: FaceRecognitionResult):
    """Recibe y guarda un resultado de reconocimiento (origen ESP32)."""
    insert_result(result.status, result.message, result.face_id, origin="ESP32")
    return {"message": "Result received", "status": "success"}

@app.get("/recognition-result/")
async def get_all_results():
    """Devuelve todos los resultados almacenados."""
    results = get_results()
    if not results:
        raise HTTPException(status_code=404, detail="No results found")

    formatted = [
        {"id": row[0], "status": row[1], "message": row[2],
         "face_id": row[3], "timestamp": row[4], "origin": row[5]}
        for row in results
    ]
    return {"results": formatted}

@app.delete("/recognition-result/")
async def delete_all():
    """Elimina todos los resultados almacenados."""
    delete_all_results()
    return {"message": "All results deleted"}

@app.delete("/recognition-result/{result_id}")
async def delete_by_id(result_id: int):
    """Elimina un resultado específico por ID."""
    delete_result_by_id(result_id)
    return {"message": f"Result with ID {result_id} deleted"}

# =====================================================
# WEBSOCKETS – STREAMING ENTRE CLIENTES
# =====================================================
connected_clients = set()

@app.websocket("/ws/stream")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket para retransmitir imágenes y mensajes entre clientes."""
    await websocket.accept()
    connected_clients.add(websocket)
    print(f"Cliente conectado: {websocket.client.host}")

    try:
        while True:
            message = await websocket.receive()

            if "bytes" in message:
                binary_data = message["bytes"]
                for client in connected_clients:
                    if client != websocket:
                        try:
                            await client.send_bytes(binary_data)
                        except Exception as e:
                            print(f"Error al enviar a cliente: {e}")

            elif "text" in message:
                text_data = message["text"]
                print("Mensaje de texto recibido del ESP32:", text_data)
                for client in connected_clients:
                    if client != websocket:
                        try:
                            await client.send_text(text_data)
                        except Exception as e:
                            print(f"Error al enviar texto: {e}")

    except Exception as e:
        print(f"Conexión cerrada o error: {e}")
    finally:
        connected_clients.discard(websocket)
        if websocket.client_state.name != "DISCONNECTED":
            await websocket.close()

# =====================================================
# COMANDOS – COMUNICACIÓN CON ESP32
# =====================================================
ESP32_IP = "192.168.18.16"
command_log = []

@app.post("/send-command")
async def send_command_to_esp32(cmd: str = Form(...)):
    """Envía comandos al ESP32 mediante HTTP POST."""
    print(f"Recibido comando: {cmd}")
    timestamp = datetime.datetime.now().isoformat()
    try:
        async with httpx.AsyncClient() as client:
            headers = {"Content-Type": "application/x-www-form-urlencoded"}
            data = {"cmd": cmd}
            response = await client.post(f"http://{ESP32_IP}/send-command", data=data, headers=headers)

            try:
                result_text = response.json()
            except Exception:
                try:
                    result_text = json.loads(response.text.strip())
                except Exception:
                    result_text = {"response": response.text.strip()}

        print("Respuesta ESP32:", response.status_code, response.text)

        command_log.append({
            "timestamp": timestamp,
            "command": cmd,
            "esp32_response": result_text
        })

        return JSONResponse(content={
            "status": "success",
            "timestamp": timestamp,
            "esp32_response": result_text
        })

    except Exception as e:
        print(f"Error enviando comando al ESP32: {e}")
        return JSONResponse(status_code=500, content={
            "status": "error",
            "message": str(e),
            "timestamp": timestamp
        })

@app.get("/command-log")
async def get_command_log():
    """Devuelve historial de comandos enviados al ESP32."""
    return {"log": command_log}

async def send_to_esp32(cmd: str):
    """Función auxiliar para enviar comandos al ESP32."""
    esp32_url = f"http://{ESP32_IP}/send-command"
    async with httpx.AsyncClient() as client:
        response = await client.post(esp32_url, params={"cmd": cmd})
        return response.json()

# =====================================================
# INSIGHTFACE – INICIALIZACIÓN Y FUNCIONES
# =====================================================
face = FaceAnalysis(name="buffalo_l", providers=["CPUExecutionProvider"])
face.prepare(ctx_id=0)

# Embeddings guardados en disco
db_path = "face_db.pkl"
db_path_esp32 = "face_db_esp32.pkl"

if os.path.exists(db_path):
    with open(db_path, "rb") as f:
        face_db = pickle.load(f)
else:
    face_db = {}

if os.path.exists(db_path_esp32):
    with open(db_path_esp32, "rb") as f:
        face_db_esp32 = pickle.load(f)
else:
    face_db_esp32 = {}

def save_face_db():
    with open(db_path, "wb") as f:
        pickle.dump(face_db, f)

def save_face_db_esp32():
    with open(db_path_esp32, "wb") as f:
        pickle.dump(face_db_esp32, f)

def image_bytes_to_bgr(image_bytes):
    """Convierte bytes de imagen a formato BGR (OpenCV)."""
    image_np = np.frombuffer(image_bytes, np.uint8)
    return cv2.imdecode(image_np, cv2.IMREAD_COLOR)

def compare_embeddings(embedding1, embedding2):
    """Compara embeddings normalizados mediante producto punto."""
    embedding1 = embedding1 / np.linalg.norm(embedding1)
    embedding2 = embedding2 / np.linalg.norm(embedding2)
    return np.dot(embedding1, embedding2)

# =====================================================
# ENDPOINT – PROCESAMIENTO DE IMAGEN
# =====================================================
enroll_buffer = {}
NUM_EMBEDDINGS_REQUIRED = 3

@app.post("/upload-image")
async def upload_image(
    request: Request,
    modo: str = Query(..., enum=["detect", "recognize", "enroll"]),
    nombre: Optional[str] = None
):
    """Recibe imagen y procesa según el modo (detect / recognize / enroll)."""
    contents = await request.body()
    print(f"[{modo.upper()}] Imagen recibida - {len(contents)} bytes")

    img = image_bytes_to_bgr(contents)
    faces = face.get(img)

    if not faces:
        return {"status": "error", "message": "NO FACE DETECTED"}

    embedding = faces[0].embedding

    # --- Detectar ---
    if modo == "detect":
        return {"status": "success", "message": "FACE DETECTED"}

    # --- Reconocer ---
    elif modo == "recognize":
        if not face_db:
            return {"status": "error", "type": "recognition", "message": "Database is empty"}

        best_match, best_score = None, -1
        for nombre_registrado, emb_registrado in face_db.items():
            score = compare_embeddings(embedding, emb_registrado)
            if score > best_score:
                best_score, best_match = score, nombre_registrado

        if best_score > 0.6:
            insert_result("success", best_match, -1, origin="SERVER")
            return {"status": "success", "type": "recognition",
                    "name": best_match, "score": float(round(best_score, 3)),
                    "message": f"Bienvenido {best_match}"}
        else:
            insert_result("error", "Unknown face", -1, origin="SERVER")
            return {"status": "error", "type": "recognition",
                    "name": "Unknown", "score": float(round(best_score, 3)),
                    "message": "Unknown face"}

    # --- Enrolar ---
    elif modo == "enroll":
        if not nombre:
            return {"status": "error", "message": "Missing 'nombre' parameter for enrollment"}

        if nombre not in enroll_buffer:
            enroll_buffer[nombre] = []
        enroll_buffer[nombre].append(embedding)
        count = len(enroll_buffer[nombre])

        if count < NUM_EMBEDDINGS_REQUIRED:
            return {"status": "partial",
                    "message": f"SAMPLE NUMBER {count} FOR '{nombre}'"}

        avg_embedding = np.mean(enroll_buffer[nombre], axis=0)
        face_db[nombre] = avg_embedding
        save_face_db()
        del enroll_buffer[nombre]

        return {"status": "success",
                "message": f"Face '{nombre}' enrolled con {NUM_EMBEDDINGS_REQUIRED} muestras."}

    return {"status": "error", "message": "Unhandled mode"}

# =====================================================
# ENDPOINTS – GESTIÓN DE EMBEDDINGS ESP32
# =====================================================
@app.get("/get-embeddings")
async def get_embeddings():
    """Devuelve embeddings ESP32 guardados (diccionario completo)."""
    if not os.path.exists(db_path_esp32):
        return JSONResponse(content={})
    try:
        with open(db_path_esp32, "rb") as f:
            face_db_esp32_loaded = pickle.load(f)
    except Exception as e:
        return JSONResponse(status_code=500,
                            content={"error": "Error leyendo embeddings ESP32",
                                     "detail": str(e)})
    return JSONResponse(content=face_db_esp32_loaded)

@app.get("/get-face-names")
async def get_face_names():
    """Devuelve solo los nombres de embeddings ESP32 guardados."""
    if not os.path.exists(db_path_esp32):
        return {"faces": []}
    try:
        with open(db_path_esp32, "rb") as f:
            face_db_esp32_loaded = pickle.load(f)
    except Exception as e:
        return JSONResponse(status_code=500,
                            content={"error": "Error leyendo embeddings ESP32",
                                     "detail": str(e)})
    return {"faces": list(face_db_esp32_loaded.keys())}

@app.post("/upload-embedding")
async def upload_embedding(data: EmbeddingData, modo: str = Query(..., enum=["enroll", "recognize"])):
    """Recibe embeddings enviados desde ESP32 (enroll y recognize)."""
    embedding_list = data.embedding
    if len(embedding_list) != 512:
        raise HTTPException(status_code=400, detail="Embedding length incorrecta")

    if modo == "enroll":
        if not data.nombre:
            raise HTTPException(status_code=400, detail="Falta el nombre en el modo enroll")
        nombre = data.nombre.strip().lower()
        face_db_esp32[nombre] = embedding_list
        save_face_db_esp32()
        return {"status": "success", "message": f"{nombre} registrado"}

    return {"status": "error", "message": "Modo recognize no implementado en servidor"}

@app.post("/clear-embeddings-esp32")
async def clear_embeddings():
    """Elimina todos los embeddings ESP32."""
    global face_db_esp32
    face_db_esp32 = {}
    save_face_db_esp32()
    return {"status": "success", "message": "Embeddings eliminados"}

@app.delete("/delete-embedding-esp32/{name}")
async def delete_embedding(name: str):
    """Elimina un embedding ESP32 por nombre."""
    if name in face_db_esp32:
        del face_db_esp32[name]
        save_face_db_esp32()
        return {"status": "success", "message": f"Embedding '{name}' eliminado"}
    else:
        return {"status": "error", "message": f"'{name}' no encontrado"}

# =====================================================
# ENDPOINTS – GESTIÓN DE EMBEDDINGS SERVIDOR
# =====================================================
@app.get("/get-embeddings-servidor")
def get_embeddings_servidor():
    """Devuelve lista de nombres de embeddings en servidor."""
    return list(face_db.keys())

@app.delete("/delete-embedding-servidor/{nombre}")
async def delete_embedding_by_name(nombre: str):
    """Elimina un embedding del servidor por nombre."""
    if nombre in face_db:
        del face_db[nombre]
        save_face_db()
        return {"status": "success", "message": f"Embedding '{nombre}' eliminado"}
    else:
        raise HTTPException(status_code=404, detail=f"Embedding '{nombre}' no encontrado")

@app.post("/clear-embeddings-servidor")
def clear_embeddings_servidor():
    """Elimina todos los embeddings del servidor."""
    face_db.clear()
    save_face_db()
    return {"status": "success", "message": "Embeddings del servidor eliminados"}
