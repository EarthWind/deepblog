void main() {
  var result = fibonacci(22);
  print('Fibonacci(22) result: $result');
}

int fibonacci(int n) {
  if (n == 0 || n == 1) {
    return n;
  }
  return fibonacci(n - 1) + fibonacci(n - 2);
}
