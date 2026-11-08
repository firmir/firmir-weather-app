#pragma once

// firmir-weather application logic, exposed to the Firml UI (weather.fml):
// the city buttons call selectCity(), the labels bind to the cityName /
// tempText / detailsText / statusText dynamic properties.

#include <firmir/networkaccessmanager.hpp>
#include <firmir/object.hpp>

#include <string>

class WeatherApp : public Firmir::Object {
public:
    explicit WeatherApp(Object* parent = nullptr);

    /// Fetches the weather for the city with the given model index.
    void selectCity(double index);
    /// Toggles the UI language between English and Russian.
    void switchLanguage();

private:
    /// Re-emits the text properties (city name, temperature, details,
    /// status) from the stored reading — used after a language switch so
    /// the UI updates without a refetch.
    void renderWeather();

    Firmir::NetworkAccessManager m_network{this};
    int m_currentCity = -1;  // index into the city table, -1 = none
    bool m_hasReading = false;
    double m_temp = 0.0;
    double m_wind = 0.0;   // m/s
    std::string m_textId = "wmo-unknown"; // translation id of the sky text
    std::string m_provider;               // shown as the status line
};
