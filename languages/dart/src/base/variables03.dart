void main() {
  final name = 'Bob';
  final String nickname = 'Bobby';
  // This line will cause an error because nickname is final
  // nickname = 'Bobo';  // Error: Can't assign to the final variable 'nickname'.
  print('Hello, $name! Your nickname is $nickname.');
}
