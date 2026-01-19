#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include "city.hpp"

class Cities {
    private:
        std::vector<City> cities;
        u_int16_t count;

    public:
        Cities();
        
        int add(City _City);
        int remove(std::string _Name);
        int getCity(std::string _Name);
        int searchCity(std::string _Query);
        void listCities();

};
