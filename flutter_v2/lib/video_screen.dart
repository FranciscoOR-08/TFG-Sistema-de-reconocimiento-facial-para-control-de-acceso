import 'dart:async';
import 'package:flutter/material.dart';
import 'dart:typed_data';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:http/http.dart' as http;
import 'dart:convert';

// Añade este canal para conectar con FastAPI (modo servidor)
//final WebSocketChannel fastApiChannel =
//WebSocketChannel.connect(Uri.parse('ws://192.168.18.9:8000/ws/stream'));

class VideoScreen extends StatefulWidget {
  const VideoScreen({super.key});

  @override
  State<VideoScreen> createState() => _VideoScreenState();

}

class _VideoScreenState extends State<VideoScreen> {
  late WebSocketChannel fastApiChannel;
  late Stream<dynamic> broadcastStream;

  String lastMessageConfirmacion = 'Esperando mensajes...';
  String faceStatus = ''; // Estado para avisos tipo detección/ reconocimiento
  String currentMode = 'DESCONOCIDO';
  String processingMode = 'SERVIDOR';

  @override
  void initState() {
    super.initState();

    fastApiChannel = WebSocketChannel.connect(Uri.parse('ws://192.168.18.14:8000/ws/stream'));

    broadcastStream = fastApiChannel.stream.asBroadcastStream();

    broadcastStream.listen((event) {

      if (event is String) {
        setState(() {
          lastMessageConfirmacion = event;

          if (event.contains('NO FACE DETECTED')) {
            faceStatus = 'No se detectó ninguna cara';
          } else if (event.contains('FACE DETECTED')) {
            faceStatus = '¡Cara detectada!';
          } else if (event.startsWith('Bienvenido')) {
            faceStatus = event; // Ej: "Bienvenido Juan"
          } else if (event == 'FACE NOT RECOGNISED') {
            faceStatus = 'Rostro no reconocido';
          } else if (event.startsWith('SAMPLE NUMBER')) {
            faceStatus = event; // Estado enrolamiento
          } else if (event.startsWith('Added')) {
            faceStatus = event; // Confirmación añadido
          } else if (event.contains('NO HAY ROSTROS')) {
            faceStatus = '¡No hay rostros registrados!';
          } else {
            faceStatus = ''; // Otros mensajes no afectan la barra amarilla
          }

          // ✅ Manejo extra: actualizar modo si viene como mensaje de texto
          final lower = event.toLowerCase();

          if (lower.contains('streaming')) {
            currentMode = 'STREAMING';
          } else if (lower.contains('detecting')) {
            currentMode = 'DETECTING';
          } else if (lower.contains('capturing')) {
            currentMode = 'CAPTURING';
          } else if (lower.contains('recognising')) {
            currentMode = 'RECOGNISING';
          }

        });
      }
    });
  }

  @override
  void dispose() {
    sendCommandToServer('stream');
    sendCommandToServer('server_mode');

    _enrollNameController.dispose();
    _removeNameController.dispose();
    fastApiChannel.sink.close(); // ← IMPORTANTE
    super.dispose();
  }

  Widget buildStreamWidget() {
    return StreamBuilder(
      stream: broadcastStream,
      builder: (context, snapshot) {
        if (snapshot.hasData && snapshot.data is Uint8List) {
          return Image.memory(
            snapshot.data as Uint8List,
            gaplessPlayback: true,
            fit: BoxFit.contain,
          );
        } else if (snapshot.hasData && snapshot.data is String) {
          return const SizedBox(); // ignora mensajes tipo texto, ya los maneja `listen(...)`
        } else {
          return const Center(child: Text("Esperando video del servidor..."));
        }
      },
    );
  }


  String lastMessage = 'Esperando mensajes...';


  final TextEditingController _enrollNameController = TextEditingController();
  final TextEditingController _removeNameController = TextEditingController();

  /*@override
  void dispose() {
    _enrollNameController.dispose();
    _removeNameController.dispose();
    fastApiChannel.sink.close(); // ← IMPORTANTE
    super.dispose();
  }*/

  Future<void> sendCommandToServer(String cmd) async {
    try {
      setState(() {
        faceStatus = '';
      });
      // Construimos la URL con el parámetro cmd en query string
      final uri = Uri.parse('http://192.168.18.14:8000/send-command');

      final response = await http.post(
        uri,
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: {'cmd': cmd},
      );

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        // esp32_response viene como string JSON, intentamos parsearlo para obtener el modo
        dynamic esp32Resp = data['esp32_response'];
        String? modeFromResp;

        if (esp32Resp is Map) {
          // Caso ideal: el servidor ya devolvió un objeto
          modeFromResp = esp32Resp['response']?.toString().toUpperCase();
        } else if (esp32Resp is String) {
          try {
            final parsed = jsonDecode(esp32Resp);
            if (parsed is Map) {
              modeFromResp = parsed['response']?.toString().toUpperCase();
            }
          } catch (e) {
            // Ignorar errores de parseo si la cadena no es JSON
          }
        }
        setState(() {
          lastMessage = 'Respuesta servidor: $esp32Resp';

          if (modeFromResp != null) {
            if (modeFromResp == 'ESP32_MODE_ON') {
              processingMode = 'ESP32';
            } else if (modeFromResp == 'SERVER_MODE_ON') {
              processingMode = 'SERVIDOR';
            }else if (['STREAMING', 'DETECTING', 'CAPTURING', 'RECOGNISING']
                .contains(modeFromResp)) {
              currentMode = modeFromResp;
            }
          }
        });
      } else {
        setState(() {
          lastMessage = 'Error en servidor: ${response.statusCode}';
        });
      }
    } catch (e) {
      setState(() {
        lastMessage = 'Error al conectar con servidor: $e';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Cámara en Vivo'),
        backgroundColor: Colors.deepPurple,
      ),
      body: SingleChildScrollView(
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 70.0, horizontal: 12.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Card(
                color: Colors.black,
                elevation: 4,
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                child: SizedBox(
                  height: 240,
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(12),
                    child: Column(
                      children: [
                        Expanded(child: buildStreamWidget()),
                        if (faceStatus.isNotEmpty)
                          Container(
                            width: double.infinity,
                            color: Colors.yellow[700],
                            padding: const EdgeInsets.all(8),
                            child: Text(
                              faceStatus,
                              textAlign: TextAlign.center,
                              style: const TextStyle(
                                fontWeight: FontWeight.bold,
                                fontSize: 16,
                              ),
                            ),
                          ),
                      ],
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 12),
              Card(
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        'Modo actual: $currentMode',
                        style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                      ),
                      Text(
                        'Procesamiento: $processingMode',
                        style: const TextStyle(fontSize: 14, color: Colors.grey),
                      ),
                      const SizedBox(height: 8),
                      Wrap(
                        spacing: 10,
                        runSpacing: 10,
                        alignment: WrapAlignment.center, // centra los elementos
                        children: [
                          ElevatedButton(
                            onPressed: () => sendCommandToServer('stream'),
                            child: const Text('Iniciar Stream'),
                          ),
                          ElevatedButton(
                            onPressed: () => sendCommandToServer('detect'),
                            child: const Text('Iniciar Detección'),
                          ),
                          ElevatedButton(
                            onPressed: () => sendCommandToServer('recognise'),
                            child: const Text('Reconocer Rostro'),
                          ),

                          // 👇 agrupamos los dos modos en una fila
                          Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              ElevatedButton(
                                onPressed: () => sendCommandToServer('esp32_mode'),
                                child: const Text('Modo ESP32'),
                              ),
                              const SizedBox(width: 10), // espacio entre ellos
                              ElevatedButton(
                                onPressed: () => sendCommandToServer('server_mode'),
                                child: const Text('Modo Servidor'),
                              ),
                            ],
                          ),

                          // 👇 este ejemplo es para un botón suelto centrado
                          /*Align(
                            alignment: Alignment.center,
                            child: ElevatedButton(
                              onPressed: () => sendCommandToServer('delete_all'),
                              child: const Text('Eliminar Todos'),
                            ),
                          ),*/
                        ],
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 12),
              Card(
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Column(
                    children: [
                      TextField(
                        controller: _enrollNameController,
                        decoration: const InputDecoration(
                          labelText: 'Nombre para enrolar',
                          border: OutlineInputBorder(),
                        ),
                      ),
                      const SizedBox(height: 8),
                      ElevatedButton(
                        onPressed: () {
                          final name = _enrollNameController.text.trim();
                          if (name.isNotEmpty) {
                            sendCommandToServer('capture:$name');
                          }
                        },
                        child: const Text('Enrolar Rostro'),
                      ),
                    ],
                  ),
                ),
              ),
              /*const SizedBox(height: 12),
              Card(
                color: Colors.grey[100],
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.all(12.0),
                  child: Text(
                    lastMessage,
                    style: const TextStyle(fontSize: 14),
                  ),
                ),
              ),*/
            ],
          ),
        ),
      ),
    );
  }
}

