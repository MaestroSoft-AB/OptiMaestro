#include "../include/weather_client.hpp"

WeatherClient::WeatherClient(std::string _City, std::string _Country)
    : lat(59.3753f), 
    lon(17.969f), 
    http_response_code(-1),
    temperature(0.0f),
    windspeed(0.0f),
    precipitation(0.0f),
    winddirection(0),
    wmo_code(0)
    {
        this->city = _City;
        this->country = _Country;
    }

int WeatherClient::getWeather() {
    TCP_Client client;

    std::string request =
    "GET /api/v1/weather?latitude=" + std::to_string(this->lat) + "&longitude=" + std::to_string(this->lon) +" HTTP/1.1\r\n"
    "Host: stockholm2.onvo.se:81\r\n"
    "User-Agent: CPPCLIENT/0.1\r\n"
    "Accept: /\r\n"
    "Connection: close\r\n"
    "\r\n";

    client.setTransmitData(request);
    client.transmit();
    client.recieve();
    std::string response = client.getRecieveData();
    parseResponse(response);
    parseBody();

    return 0;
}

int WeatherClient::parseResponse(std::string& _Response) {
    std::size_t firstline_end = _Response.find("\r\n");
    if (firstline_end == std::string::npos) {
        return -1;
    }
    this->http_response_firstline = _Response.substr(0, firstline_end);

    std::size_t code_start = this->http_response_firstline.find(" ");
    if (code_start == std::string::npos) {
        return -1;
    }
    this->http_response_code = std::stoi(this->http_response_firstline.substr(code_start, 4));

    std::size_t body_start = _Response.find("{");
    if (body_start == std::string::npos) {
        return -1;
    }
    this->http_response_body = _Response.substr(body_start);

    return 0;
}

int WeatherClient::parseBody() {
    if (this->http_response_body.empty()) {
        return -1;
    }

    nlohmann::json bodyobj = nlohmann::json::parse(this->http_response_body);
    
    std::string timestamp = bodyobj["timestamp"];
    std::size_t t = timestamp.find("T");
    this->date = timestamp.substr(0, t);
    this->time = timestamp.substr(t+1);
    
    if (bodyobj.contains("weather") && bodyobj["weather"].contains("temperature")) {
        this->temperature = bodyobj["weather"]["temperature"].get<float>();
    }
    if (bodyobj.contains("weather") && bodyobj["weather"].contains("windspeed")) {
        this->windspeed = bodyobj["weather"]["windspeed"].get<float>();
    }
    if (bodyobj.contains("weather") && bodyobj["weather"].contains("precipitation")) {
        this->precipitation = bodyobj["weather"]["precipitation"].get<float>();
    } 
    if (bodyobj.contains("weather") && bodyobj["weather"].contains("winddirection")) {
        this->winddirection = bodyobj["weather"]["winddirection"].get<int>();
    }
    if (bodyobj.contains("weather") && bodyobj["weather"].contains("wmo_code")) {
        this->wmo_code = bodyobj["weather"]["wmo_code"].get<int>();
    }
    if (bodyobj.contains("units") && bodyobj["units"].contains("temperature_unit")) {
        this->temperature_unit = bodyobj["units"]["temperature_unit"];
    }
    if (bodyobj.contains("units") && bodyobj["units"].contains("windspeed_unit")) {
        this->windspeed_unit = bodyobj["units"]["windspeed_unit"];
    }
    if (bodyobj.contains("units") && bodyobj["units"].contains("precipitation_unit")) {
        this->precipitation_unit = bodyobj["units"]["precipitation_unit"];
    }
    if (bodyobj.contains("units") && bodyobj["units"].contains("winddirection_unit")) {
        this->winddirection_unit = bodyobj["units"]["winddirection_unit"];
    }

    return 0;
}

int WeatherClient::getCoords(std::string _Name) {
    TCP_Client client;

    //TODO ADD HTTPS
    std::string request =
    "GET /v1/search?name=" + _Name + " HTTP/1.1\r\n"
    "Host: geocoding-api.open-meteo.com\r\n"
    "User-Agent: curl/8.16.0\r\n"
    "Accept: */*\r\n"
    "Connection: close\r\n"
    "\r\n";

    std::cout << request << std::endl;
    client.setTransmitData(request);
    client.transmit();
    client.recieve();
    std::string response = client.getRecieveData();

    std::cout << response << std::endl;

    return 0;
}


