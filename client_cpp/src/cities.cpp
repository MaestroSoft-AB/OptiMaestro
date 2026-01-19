#include "../include/cities.hpp"

Cities::Cities()
    : count(0) {

    }
        
int Cities::add(City _City) {
    this->cities.push_back(_City);
    this->count++;
    return 0;
}

int Cities::remove(std::string _Name) {
  auto res = std::find_if(this->cities.begin(),
        this->cities.end(),
        [&](City& city){return city.getName() == _Name;});

  if (res != this->cities.end()) {
    std::cout << "Deleting: " << _Name << std::endl;
    this->cities.erase(res);
    this->count--;
    return 0;
  } else {
    std::cout << "Unable to find" << _Name << std::endl;
    return -1;
  }
  return -1;
}

int Cities::getCity(std::string _Name) {
    auto res = std::find_if(this->cities.begin(),
        this->cities.end(),
        [&](City& city){return city.getName() == _Name;});

    if (res != this->cities.end()) {
      std::cout << "Ladies and gentlemen, we got him" << std::endl;
      return 0;

    } else {
      std::cout << "Not found" << std::endl;
      return -1;
    }

    return -1;
}
