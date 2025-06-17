class Point {
  double x;
  double y;

  Point(this.x, this.y);

  Point.origin()
      : x = 0,
        y = 0;

  Point.fromList(List<double> coords)
      : x = coords[0],
        y = coords[1];
}

void main() {
  var p1 = Point(3, 4);
  print('Point p1: (${p1.x}, ${p1.y})');

  var p2 = Point.origin();
  print('Point p2: (${p2.x}, ${p2.y})');

  var p3 = Point.fromList([5, 6]);
  print('Point p3: (${p3.x}, ${p3.y})');
}
