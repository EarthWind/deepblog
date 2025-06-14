void main() {
  int lineNumber;
  bool weLikeCount = true; // Change this to false to test the other branch

  if (weLikeCount) {
    lineNumber = 1;
  } else {
    lineNumber = 2;
  }
  print('Line number is $lineNumber');
}
