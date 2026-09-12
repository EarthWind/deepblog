class SpaceCraft {
  String name;
  DateTime? launchDate;

  SpaceCraft(this.name, this.launchDate);

  int get launchYear => launchDate?.year ?? 0;

  void describe() {
    print('Spacecraft: $name');
    var launchDate = this.launchDate;
    if (launchDate != null) {
      int years = DateTime.now().difference(launchDate).inDays ~/ 365;
      print('Launched: $years years ago');
    } else {
      print('Unlaunched');
    }
  }
}

class Orbiter extends SpaceCraft {
  double altitude;
  Orbiter(String name, DateTime launchDate, this.altitude)
      : super(name, launchDate);

  @override
  void describe() {
    super.describe();
    print('Orbited at an altitude of $altitude kilometers.');
  }
}

void main() {
  var voyager = SpaceCraft('Voyager', DateTime(1977, 9, 5));
  print('Launched: ${voyager.launchYear}');
  voyager.describe();

  var extendVoyager = Orbiter('Voyager', DateTime(1977, 9, 5), 10000);
  print('Launched: ${extendVoyager.launchYear}');
  extendVoyager.describe();
}
