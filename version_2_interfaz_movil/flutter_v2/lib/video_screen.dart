// =====================================================
// Pantalla de video en vivo (VideoScreen)
//
// Funcionalidades:
//  - Conexión a backend FastAPI vía WebSocket (/ws/stream).
//  - Recepción de imágenes (binario) y mensajes (texto).
//  - Envío de comandos al backend vía HTTP POST (/send-command).
//  - Control de modos: streaming, detección, reconocimiento y enrolamiento.
//  - Interfaz con estado visual de la cámara y notificaciones.
// =====================================================

import 'dart:async';
import 'package:flutter/material.dart';
import 'dart:typed_data';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:http/http.dart' as http;
import 'dart:convert';

class VideoScreen extends StatefulWidget {
  const VideoScreen({super.key});

  @override
  State<VideoScreen> createState() => _VideoScreenState();
}

class _VideoScreenState extends State<VideoScreen> {
  // Canal WebSocket hacia el backend FastAPI
  late WebSocketChannel fastApiChannel;

  // Stream en broadcast para poder escuchar múltiples veces
  late Stream<dynamic> broadcastStream;

  // Variables de estado de UI
  String lastMessageConfirmacion = 'Esperando mensajes...';
  String faceStatus = '';          // Estado actual de detección / reconocimiento
  String currentMode = 'DESCONOCIDO';
  String processingMode = 'SERVIDOR';

  // Controladores de texto para enrolamiento y borrado
  final TextEditingController _enrollNameController = TextEditingController();
  final TextEditingController _removeNameController = TextEditingController();

  @override
  void initState() {
    super.initState();

    // Conectar al WebSocket del backend
    fastApiChannel = WebSocketChannel.connect(
      Uri.parse('ws://192.168.18.14:8000/ws/stream'),
    );

    // Crear stream broadcast
    broadcastStream = fastApiChannel.stream.asBroadcastStream();

    // Escuchar los mensajes recibidos del WebSocket
    broadcastStream.listen((event) {
      if (event is String) {
        setState(() {
          lastMessageConfirmacion = event;

          // Actualizar estado visible en la interfaz según mensaje recibido
          if (event.contains('NO FACE DETECTED')) {
            faceStatus = 'No se detectó ninguna cara';
          } else if (event.contains('FACE DETECTED')) {
            faceStatus = '¡Cara detectada!';
          } else if (event.startsWith('Bienvenido')) {
            faceStatus = event; // Ejemplo: "Bienvenido Juan"
          } else if (event == 'FACE NOT RECOGNISED') {
            faceStatus = 'Rostro no reconocido';
          } else if (event.startsWith('SAMPLE NUMBER')) {
            faceStatus = event; // Estado durante enrolamiento
          } else if (event.startsWith('Added')) {
            faceStatus = event; // Confirmación de registro exitoso
          } else if (event.contains('NO HAY ROSTROS')) {
            faceStatus = '¡No hay rostros registrados!';
          } else {
            faceStatus = ''; // Otros mensajes no se muestran
          }

          // Actualizar estado de modo según texto recibido
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
    // Antes de cerrar: volver a modo stream + servidor
    sendCommandToServer('stream');
    sendCommandToServer('server_mode');

    _enrollNameController.dispose();
    _removeNameController.dispose();
    fastApiChannel.sink.close(); // Cierra la conexión WS
    super.dispose();
  }

  // =====================================================
  // Renderizar video en tiempo real (StreamBuilder)
  // =====================================================
  Widget buildStreamWidget() {
    return StreamBuilder(
      stream: broadcastStream,
      builder: (context, snapshot) {
        if (snapshot.hasData && snapshot.data is Uint8List) {
          // Datos binarios → frame de video
          return Image.memory(
            snapshot.data as Uint8List,
            gaplessPlayback: true,
            fit: BoxFit.contain,
          );
        } else if (snapshot.hasData && snapshot.data is String) {
          // Mensajes de texto → se ignoran aquí (ya se gestionan en listen)
          return const SizedBox();
        } else {
          return const Center(child: Text("Esperando video del servidor..."));
        }
      },
    );
  }

  // Último mensaje del backend (texto)
  String lastMessage = 'Esperando mensajes...';

  // =====================================================
  // Enviar comando HTTP POST a backend (/send-command)
  // =====================================================
  Future<void> sendCommandToServer(String cmd) async {
    try {
      setState(() {
        faceStatus = ''; // Reset estado al enviar nuevo comando
      });

      final uri = Uri.parse('http://192.168.18.14:8000/send-command');

      final response = await http.post(
        uri,
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: {'cmd': cmd},
      );

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        dynamic esp32Resp = data['esp32_response'];
        String? modeFromResp;

        // Backend puede devolver respuesta como mapa o string
        if (esp32Resp is Map) {
          modeFromResp = esp32Resp['response']?.toString().toUpperCase();
        } else if (esp32Resp is String) {
          try {
            final parsed = jsonDecode(esp32Resp);
            if (parsed is Map) {
              modeFromResp = parsed['response']?.toString().toUpperCase();
            }
          } catch (_) {}
        }

        setState(() {
          lastMessage = 'Respuesta servidor: $esp32Resp';

          // Actualizar estado según respuesta
          if (modeFromResp != null) {
            if (modeFromResp == 'ESP32_MODE_ON') {
              processingMode = 'ESP32';
            } else if (modeFromResp == 'SERVER_MODE_ON') {
              processingMode = 'SERVIDOR';
            } else if (['STREAMING', 'DETECTING', 'CAPTURING', 'RECOGNISING']
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

  // =====================================================
  // Interfaz principal de la pantalla
  // =====================================================
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
              // Card con video en vivo y barra amarilla de estado
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

              // Panel de control de modos
              Card(
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('Modo actual: $currentMode',
                          style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
                      Text('Procesamiento: $processingMode',
                          style: const TextStyle(fontSize: 14, color: Colors.grey)),
                      const SizedBox(height: 8),

                      // Botones de control
                      Wrap(
                        spacing: 10,
                        runSpacing: 10,
                        alignment: WrapAlignment.center,
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
                          Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              ElevatedButton(
                                onPressed: () => sendCommandToServer('esp32_mode'),
                                child: const Text('Modo ESP32'),
                              ),
                              const SizedBox(width: 10),
                              ElevatedButton(
                                onPressed: () => sendCommandToServer('server_mode'),
                                child: const Text('Modo Servidor'),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 12),

              // Formulario de enrolamiento
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
            ],
          ),
        ),
      ),
    );
  }
}
