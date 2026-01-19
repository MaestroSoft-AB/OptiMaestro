#ifndef _CITY_HPP_
#define _CITY_HPP_

#include <string>

class City {
    private:
        std::string name;
        std::string region;
        std::string country;
        //const std::string UUID;
        double lat;
        double lon;
    public:
        City(std::string _Name, std::string _Region, std::string _Country, double _Lat, double _Lon);

        double getLat();
        double getLon();
        std::string getName();
        std::string getRegion();
        std::string getCountry();

        //std::string getUUID();
};

#endif
