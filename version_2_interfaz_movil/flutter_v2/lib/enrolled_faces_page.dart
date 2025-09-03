import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'dart:convert';

class EnrolledFacesPage extends StatefulWidget {
  const EnrolledFacesPage({super.key});

  @override
  State<EnrolledFacesPage> createState() => _EnrolledFacesPageState();
}

class _EnrolledFacesPageState extends State<EnrolledFacesPage> with TickerProviderStateMixin {
  // Listas de rostros guardados en ESP32 y en el servidor
  List<String> esp32Faces = [];
  List<String> serverFaces = [];

  // Banderas de carga para mostrar el spinner
  bool isLoadingESP32 = true;
  bool isLoadingServer = true;

  // Dirección del backend (FastAPI)
  final String ipServidor = 'http://192.168.18.14:8000'; // ⚠Personaliza esta IP

  @override
  void initState() {
    super.initState();
    fetchFaces(); // Al iniciar, descarga los rostros desde ambas fuentes
  }

  // Descarga los rostros de ESP32 y Servidor en paralelo
  Future<void> fetchFaces() async {
    await Future.wait([
      fetchFacesFromUrl('$ipServidor/get-face-names', isESP32: true),
      fetchFacesFromUrl('$ipServidor/get-embeddings-servidor', isESP32: false),
    ]);
  }

  // Llamada genérica para obtener rostros desde una URL
  Future<void> fetchFacesFromUrl(String url, {required bool isESP32}) async {
    try {
      final response = await http.get(Uri.parse(url));
      if (response.statusCode == 200) {
        final data = json.decode(response.body);
        setState(() {
          if (isESP32) {
            esp32Faces = List<String>.from(data['faces']); // JSON devuelto con {"faces": [...]}
            isLoadingESP32 = false;
          } else {
            serverFaces = List<String>.from(data); // Servidor devuelve lista simple
            isLoadingServer = false;
          }
        });
      }
    } catch (e) {
      print('Error cargando rostros ${isESP32 ? 'ESP32' : 'Servidor'}: $e');
      setState(() {
        if (isESP32) isLoadingESP32 = false;
        else isLoadingServer = false;
      });
    }
  }

  // Eliminar un rostro concreto (ESP32 o servidor)
  Future<void> deleteFace(String name, {required bool isESP32}) async {
    final url = isESP32
        ? '$ipServidor/delete-embedding-esp32/$name'
        : '$ipServidor/delete-embedding-servidor/$name';

    try {
      final response = await http.delete(Uri.parse(url));
      if (response.statusCode == 200) {
        setState(() {
          if (isESP32) {
            esp32Faces.remove(name);
          } else {
            serverFaces.remove(name);
          }
        });
      } else {
        print('Error al eliminar $name');
      }
    } catch (e) {
      print('Error al eliminar $name: $e');
    }
  }

  // Eliminar todos los rostros (ESP32 o servidor)
  Future<void> clearAllFaces({required bool isESP32}) async {
    final url = isESP32
        ? '$ipServidor/clear-embeddings-esp32'
        : '$ipServidor/clear-embeddings-servidor';

    try {
      final response = await http.post(Uri.parse(url));
      if (response.statusCode == 200) {
        setState(() {
          if (isESP32) esp32Faces.clear();
          else serverFaces.clear();
        });
      } else {
        print('Error al borrar todos los rostros');
      }
    } catch (e) {
      print('Error al borrar todos los rostros: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return DefaultTabController(
      length: 2, // Dos pestañas: ESP32 y Servidor
      child: Scaffold(
        appBar: AppBar(
          title: const Text('Rostros Enrolados'),
          bottom: const TabBar(
            tabs: [
              Tab(text: 'ESP32'),
              Tab(text: 'Servidor'),
            ],
          ),
        ),
        body: TabBarView(
          children: [
            _buildFaceList(esp32Faces, isLoadingESP32, true),
            _buildFaceList(serverFaces, isLoadingServer, false),
          ],
        ),
      ),
    );
  }

  // Construcción de cada lista de rostros
  Widget _buildFaceList(List<String> faces, bool isLoading, bool isESP32) {
    if (isLoading) {
      return const Center(child: CircularProgressIndicator());
    }

    return Stack(
      children: [
        // Lista de rostros
        faces.isEmpty
            ? const Center(child: Text('No hay rostros enrolados.'))
            : ListView.builder(
          padding: const EdgeInsets.only(bottom: 80), // deja espacio al FAB
          itemCount: faces.length,
          itemBuilder: (context, index) {
            final name = faces[index];
            return ListTile(
              leading: const Icon(Icons.face),
              title: Text(name),
              trailing: IconButton(
                icon: const Icon(Icons.delete, color: Colors.red),
                onPressed: () async {
                  // Confirmación antes de borrar
                  final confirm = await showDialog<bool>(
                    context: context,
                    builder: (ctx) => AlertDialog(
                      title: const Text('Eliminar rostro'),
                      content: Text('¿Eliminar "$name"?'),
                      actions: [
                        TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancelar')),
                        TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Eliminar')),
                      ],
                    ),
                  );
                  if (confirm == true) {
                    await deleteFace(name, isESP32: isESP32);
                  }
                },
              ),
            );
          },
        ),

        // Botón flotante para eliminar todos los rostros
        Positioned(
          bottom: 16,
          right: 16,
          child: FloatingActionButton.extended(
            icon: const Icon(Icons.delete_sweep),
            label: Text('Borrar todos (${isESP32 ? 'ESP32' : 'Servidor'})'),
            backgroundColor: Colors.redAccent,
            onPressed: () async {
              final confirm = await showDialog<bool>(
                context: context,
                builder: (ctx) => AlertDialog(
                  title: const Text('Eliminar todos'),
                  content: Text('¿Estás seguro de eliminar todos los rostros de ${isESP32 ? 'ESP32' : 'Servidor'}?'),
                  actions: [
                    TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancelar')),
                    TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Eliminar')),
                  ],
                ),
              );
              if (confirm == true) {
                await clearAllFaces(isESP32: isESP32);
              }
            },
          ),
        ),
      ],
    );
  }
}
