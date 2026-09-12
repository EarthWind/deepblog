const oneSecond = Duration(seconds: 5);

Future<void> printWithDelay(String message) async {
  await Future.delayed(oneSecond);
  print(message);
}

void main() async {
  print(DateTime.now().toString());
  await printWithDelay('Async programming is fun!');
  print(DateTime.now().toString());
}
