//Registro_service.dart
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'registro_model.dart';

class RegistroService {
  static const String baseUrl = 'http://192.168.18.14:8000'; // Cambia TU_IP por la IP de tu backend

  static Future<List<Registro>> fetchRegistros() async {
    final response = await http.get(Uri.parse('$baseUrl/recognition-result/'));

    if (response.statusCode == 200) {
      final data = json.decode(response.body);

      // Aquí asumimos que el backend devuelve directamente una lista (no un objeto con 'results')
      if (data is List) {
        return data.map((e) => Registro.fromJson(e)).toList();
      } else if (data is Map && data.containsKey('results')) {
        // Si backend devuelve un objeto con 'results' (lista)
        final results = data['results'] as List;
        return results.map((e) => Registro.fromJson(e)).toList();
      } else {
        throw Exception('Respuesta inesperada del servidor');
      }
    } else if (response.statusCode == 404) {
      // No hay registros, devuelve lista vacía para evitar error
      return [];
    } else {
      throw Exception('Error al obtener los registros: Código ${response.statusCode}');
    }
  }
}