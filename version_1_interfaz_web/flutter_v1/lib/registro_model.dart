// =====================================================
// MODELO DE DATOS – Registro de acceso
// =====================================================
// Representa un acceso (intento de reconocimiento facial)
// con toda la información obtenida desde el backend.
//
// Campos:
//   - id: identificador único en la base de datos.
//   - status: estado del reconocimiento ("success" o "error").
//   - message: mensaje asociado (ej. "Bienvenido Francisco").
//   - faceId: id de la cara en BD (puede ser null si no aplica).
//   - timestamp: fecha/hora del registro.
//   - origin: origen del reconocimiento (SERVER o ESP32).
// =====================================================

class Registro {
  final int id;
  final String status;
  final String message;
  final int? faceId;
  final DateTime timestamp;
  final String origin;

  Registro({
    required this.id,
    required this.status,
    required this.message,
    required this.faceId,
    required this.timestamp,
    required this.origin,
  });

  // =====================================================
  // FACTORY – Conversión desde JSON recibido del backend
  // =====================================================
  factory Registro.fromJson(Map<String, dynamic> json) {
    return Registro(
      id: json['id'],
      status: json['status'],
      message: json['message'],
      faceId: json['face_id'],
      timestamp: DateTime.parse(json['timestamp']),
      origin: json['origin'] ?? 'DESCONOCIDO',
    );
  }
}
