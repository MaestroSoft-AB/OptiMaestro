#include "../include/city.hpp"

City::City(std::string _Name, std::string _Region, std::string _Country, double _Lat, double _Lon)
    : name(_Name), region(_Region), country(_Country), lat(_Lat), lon(_Lon) {
    }

double City::getLat() {return this->lat;}
double City::getLon() {return this->lon;}

std::string City::getName() {return this->name;}
std::string City::getRegion() {return this->region;}
std::string City::getCountry() {return this->country;}
