//Registro_model.dart
class Registro {
  final int id;
  final String status;
  final String message;
  final int? faceId;
  final DateTime timestamp;
  final String origin; // 👈 nuevo campo

  Registro({
    required this.id,
    required this.status,
    required this.message,
    required this.faceId,
    required this.timestamp,
    required this.origin,
  });

  factory Registro.fromJson(Map<String, dynamic> json) {
    return Registro(
      id: json['id'],
      status: json['status'],
      message: json['message'],
      faceId: json['face_id'],
      timestamp: DateTime.parse(json['timestamp']),
      origin: json['origin'] ?? 'DESCONOCIDO', // fallback por si falta
    );
  }
}