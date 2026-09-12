enum PlanetType { terrestrial, gas, ice }

enum Planet {
  mercury(planetType: PlanetType.terrestrial, moons: 0, hasRings: false),
  venus(planetType: PlanetType.terrestrial, moons: 0, hasRings: false),
  uranus(planetType: PlanetType.ice, moons: 27, hasRings: true),
  neptune(planetType: PlanetType.ice, moons: 14, hasRings: true);

  const Planet({
    required this.planetType,
    required this.moons,
    required this.hasRings,
  });

  final PlanetType planetType;
  final int moons;
  final bool hasRings;

  bool get isGiant =>
      planetType == PlanetType.gas || planetType == PlanetType.ice;

  String get getName => toString().split('.').last;
}

void main() {
  print('Planet ${Planet.mercury.getName} is giant: ${Planet.mercury.isGiant}');
  print('Planet ${Planet.venus.getName} is giant: ${Planet.venus.isGiant}');
  print('Planet ${Planet.uranus.getName} is giant: ${Planet.uranus.isGiant}');
  print('Planet ${Planet.neptune.getName} is giant: ${Planet.neptune.isGiant}');
}
