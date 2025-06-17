class Logger {
  static final Map<String, Logger> _cache = {};
  final String name;

  Logger._internal(this.name);

  factory Logger(String name) {
    return _cache.putIfAbsent(name, () => Logger._internal(name));
  }
}

void main() {
  var logger1 = Logger('db');
  var logger2 = Logger('db');
  print(logger1 == logger2);
}
