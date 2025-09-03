// =====================================================
// Servicio: RegistroService
// Encargado de comunicarse con el backend FastAPI
// para obtener el historial de accesos desde la API.
// =====================================================
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'registro_model.dart';

class RegistroService {
  // Dirección base del backend FastAPI
  static const String baseUrl = 'http://192.168.18.14:8000'; // Cámbiala según la IP de tu servidor backend

  /// =====================================================
  /// fetchRegistros()
  /// Llama al endpoint `/recognition-result/` del backend
  /// y devuelve una lista de objetos .
  ///
  /// - Si la respuesta es 200 (OK), parsea el JSON.
  /// - El backend puede devolver:
  ///     a) Una lista directamente -> `[{}, {}, ...]`
  ///     b) Un objeto con la clave "results" -> `{"results": [ ... ]}`
  /// - Si no hay registros (404), devuelve lista vacía.
  /// - Si ocurre un error inesperado, lanza excepción.
  /// =====================================================
  static Future<List<Registro>> fetchRegistros() async {
    final response = await http.get(Uri.parse('$baseUrl/recognition-result/'));

    if (response.statusCode == 200) {
      final data = json.decode(response.body);

      // Caso A: backend devuelve directamente una lista
      if (data is List) {
        return data.map((e) => Registro.fromJson(e)).toList();

        // Caso B: backend devuelve un objeto con "results"
      } else if (data is Map && data.containsKey('results')) {
        final results = data['results'] as List;
        return results.map((e) => Registro.fromJson(e)).toList();

      } else {
        throw Exception('Respuesta inesperada del servidor');
      }

    } else if (response.statusCode == 404) {
      // Caso: no hay registros -> lista vacía
      return [];

    } else {
      // Otros errores (500, etc.)
      throw Exception('Error al obtener los registros: Código ${response.statusCode}');
    }
  }
}
