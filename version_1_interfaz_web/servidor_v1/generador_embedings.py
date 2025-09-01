import numpy as np
import pickle
import os

def crear_embedding():
    return np.random.rand(512)

def leer_embeddings(archivo="face_db.pkl"):
    if os.path.exists(archivo):
        with open(archivo, 'rb') as f:
            embeddings = pickle.load(f)
        print(f"Leidos {len(embeddings)} embeddings desde '{archivo}'.")
        return embeddings
    else:
        print(f"No existe el archivo '{archivo}', se crea uno nuevo.")
        return []

def guardar_embeddings(embeddings, archivo="face_db.pkl"):
    with open(archivo, 'wb') as f:
        pickle.dump(embeddings, f)
    print(f"Guardados {len(embeddings)} embeddings en '{archivo}'.")

def añadir_embeddings(cantidad, archivo="face_db.pkl"):
    embeddings = leer_embeddings(archivo)
    for _ in range(cantidad):
        embeddings.append(crear_embedding())
    guardar_embeddings(embeddings, archivo)

# PRUEBA
print("Estado inicial:")
embeddings = leer_embeddings()

print("\nAñadiendo 10 embeddings...")
añadir_embeddings(10)

print("\nEstado final:")
embeddings = leer_embeddings()