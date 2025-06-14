void main() {
  var name = 'name';
  var year = 1977;
  var antennaDiameter = 3.77;
  var flybyObjects = ['Jupiter', 'Saturn', 'Uranus', 'Neptune'];
  var image = {
    'tag': ['saturn', 'voyager'],
    'url': 'https://example.com/voyager.jpg',
  };

  print('name type: ${name.runtimeType}');
  print('year type: ${year.runtimeType}');
  print('antennaDiameter type: ${antennaDiameter.runtimeType}');
  print('flybyObjects type: ${flybyObjects.runtimeType}');
  print('image type: ${image.runtimeType}');
}
