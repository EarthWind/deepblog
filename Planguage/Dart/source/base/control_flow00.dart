void main() {
  var year = 1977;
  var flyByObjects = ['Jupiter', 'Saturn', 'Uranus', 'Neptune'];

  if (year >= 2001) {
    print('21th century mission');
  } else {
    print('20th century mission');
  }

  for (final object in flyByObjects) {
    print(object);
  }
  for (int month = 1; month <= 12; month++) {
    print('month: $month');
  }
  while (year < 2006) {
    year++;
  }
  if (year > 2001) {
    print('21st century mission');
  }
}
