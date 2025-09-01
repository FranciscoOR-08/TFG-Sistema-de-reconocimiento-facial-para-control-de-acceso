import insightface
import cv2

# Cargar modelo preentrenado
model = insightface.app.FaceAnalysis(name='buffalo_l', providers=['CPUExecutionProvider'])
model.prepare(ctx_id=0)

# Cargar imagen
img = cv2.imread("persona.jpg")
faces = model.get(img)

# Mostrar resultados
for face in faces:
    print("Embedding:", face.embedding)
    print("Box:", face.bbox)

# Dibujar rostro detectado
for face in faces:
    x1, y1, x2, y2 = map(int, face.bbox)
    cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)

cv2.imshow("Detected Face", img)
cv2.waitKey(0)
cv2.destroyAllWindows()
