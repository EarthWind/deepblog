import 'dart:async';
import 'dart:convert';
import 'dart:isolate';

void main() async {
  final worker = Worker();
  await worker.spawn();
  await worker.ParseJson('{"name": "John", "age": 30, "city": "New York"}');
}

class Worker {
  late SendPort _sendPort;
  final Completer<void> _isolateReady = Completer.sync();

  Future<void> spawn() async {
    final receivePort = ReceivePort();
    receivePort.listen(_handleResponseFromIsolate);
    await Isolate.spawn(_startRemoteIsolate, receivePort.sendPort);
  }

  void _handleResponseFromIsolate(dynamic message) {
    if (message is SendPort) {
      _sendPort = message;
      _isolateReady.complete();
    } else if (message is Map<String, dynamic>) {
      print('Received data: ${message}');
    }
  }

  static void _startRemoteIsolate(SendPort port) {
    final receivePort = ReceivePort();
    port.send(receivePort.sendPort);

    receivePort.listen((message) {
      if (message is String) {
        final transformed = jsonDecode(message);
        port.send(transformed);
      }
    });
  }

  Future<void> ParseJson(String message) async {
    await _isolateReady.future;
    _sendPort.send(message);
  }
}
