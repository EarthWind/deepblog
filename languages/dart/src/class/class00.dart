class SpaceCraft {
  String name;
  DateTime? launchDate;

  int get launchYear => launchDate?.year ?? 0;

  SpaceCraft(this.name, this.launchDate);

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

void main() {
  var voyager = SpaceCraft('Voyager', DateTime(1977, 9, 5));
  print('Launched: ${voyager.launchYear}');
  voyager.describe();
}
