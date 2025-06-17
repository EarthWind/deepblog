class Rectangle {
  double width;
  double height;

  Rectangle(this.width, this.height);

  Rectangle.square(double size) : this(size, size);
}

void main() {
  var rect1 = Rectangle(3, 4);
  print('Rectangle 1: width=${rect1.width}, height=${rect1.height}');

  var squar1 = Rectangle.square(5);
  print('Square 1: width=${squar1.width}, height=${squar1.height}');
}
