// =====================================================
// SERVICIO – RegistroService
// =====================================================
// Este servicio se encarga de conectarse al backend FastAPI
// y obtener la lista de registros de accesos (intentos de
// reconocimiento facial).
//
// Uso principal: RegistroService.fetchRegistros()
// =====================================================
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'registro_model.dart';

class RegistroService {
  static const String baseUrl = 'http://192.168.18.14:8000'; // IMPORTANTE: cambiar la IP a la de tu servidor real
  // =====================================================
  // FUNCIÓN: fetchRegistros()
  // -----------------------------------------------------
  // Llama al endpoint GET /recognition-result/ en el backend
  // y devuelve una lista de objetos Registro.
  //
  // Flujo:
  //   1. Hace la petición HTTP GET.
  //   2. Si la respuesta es 200 OK:
  //       - Decodifica el JSON.
  //       - Extrae el array "results".
  //       - Convierte cada objeto en Registro usando fromJson().
  //   3. Si hay error -> lanza una excepción.
  // =====================================================
  static Future<List<Registro>> fetchRegistros() async {
    final response = await http.get(Uri.parse('$baseUrl/recognition-result/'));

    if (response.statusCode == 200) {
      final data = json.decode(response.body);
      final results = data['results'] as List;
      return results.map((e) => Registro.fromJson(e)).toList();
    } else {
      throw Exception('Error al obtener los registros');
    }
  }
}