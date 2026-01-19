
#include "../include/weather_client.hpp"
#include "../include/cities.hpp"
#include "../include/city.hpp"

int main() {
  WeatherClient client("Sundbyberg", "Sweden");
  client.getWeather();

  City c("Stockholm", "Norrland", "Uppsala", 17.132112, 18.24929);



  Cities cities;

  cities.add(c);
  cities.getCity("Stockholm");

  cities.remove("Stockholm");

  cities.getCity("Stockholm");

  client.getCoords("London");
  
  return 0; 
}
