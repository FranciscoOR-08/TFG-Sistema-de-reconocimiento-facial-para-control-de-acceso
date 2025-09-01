import 'package:web_socket_channel/web_socket_channel.dart';

class ESP32SocketService {
  final WebSocketChannel _channel;

  ESP32SocketService(String ip)
      : _channel = WebSocketChannel.connect(Uri.parse('ws://$ip:8000/ws/stream'));

  void sendCommand(String command) {
    _channel.sink.add(command);
  }

  Stream<dynamic> get stream => _channel.stream;

  void dispose() {
    _channel.sink.close();
  }
}

