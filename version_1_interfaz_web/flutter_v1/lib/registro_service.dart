import 'dart:convert';
import 'package:http/http.dart' as http;
import 'registro_model.dart';

class RegistroService {
  static const String baseUrl = 'http://192.168.18.14:8000'; // Cambia TU_IP por la IP de tu backend
  //static const String baseUrl = 'http://192.168.1.7:8000'; // Cambia TU_IP por la IP de tu backend
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