#ifndef _WEATHER_CLIENT_HPP_
#define _WEATHER_CLIENT_HPP_

#include <string>
#include "../include/tcp_client.hpp"
#include "../../libs/include/json.hpp"


class WeatherClient {
    private:
        std::string city;
        std::string country;
        double lat;
        double lon;

        std::string http_response_firstline;
        std::string http_response_body;
        int http_response_code;

        float temperature;
        float windspeed;
        float precipitation;
        int winddirection;
        int wmo_code;
        std::string temperature_unit;
        std::string windspeed_unit;
        std::string precipitation_unit;
        std::string winddirection_unit;
        std::string date;
        std::string time;
        

        int parseResponse(std::string& _Response);
        int parseBody();

    public:
        WeatherClient(std::string _City, std::string _Country); 
        int getWeather();
        int getCoords(std::string _Name);
        



};

#endif
