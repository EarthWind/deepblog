mixin Piloted {
  int astronauts = 1;

  void describeCrew() {
    print('Number of astronauts: $astronauts');
  }
}

class Pilotedcraft with Piloted {
  int cargo = 1000;
  int passengers = 100;

  void describe() {
    print('Cargo capacity: $cargo');
    print('Passenger capacity: $passengers');
  }
}

void main() {
  var craft = Pilotedcraft();
  craft.describe();
  craft.describeCrew();
}
