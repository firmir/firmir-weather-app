#include "WeatherApp.hpp"

#include <firmir/json.hpp>
#include <firmir/networkreply.hpp>
#include <firmir/string.hpp>
#include <firmir/translator.hpp>

#include <cstdio>
#include <chrono>
#include <cstdlib>

namespace {

struct City {
    const char* name;
    double latitude;
    double longitude;
};

const City kCities[] = {
    {"city-moscow", 55.75, 37.62},  {"city-london", 51.51, -0.13},
    {"city-tokyo", 35.68, 139.69},  {"city-new-york", 40.71, -74.01},
    {"city-yakutsk", 62.03, 129.73},
};

/// MET Norway symbol codes ("partlycloudy_day", "heavyrainshowers_and_thunder",
/// ...) -> translation ids (the texts live in translations/*.json).
const char* weatherTextIdForSymbol(const std::string& symbol) {
    // Strip the phase suffix: "partlycloudy_day" -> "partlycloudy".
    std::string base = symbol.substr(0, symbol.find('_'));
    std::string flat = symbol;
    for (char& c : flat)
        if (c == '_')
            c = ' '; // "rainshowers_and_thunder" — word search below
    const auto has = [&flat](const char* word) {
        return flat.find(word) != std::string::npos;
    };
    // Order matters: "rainandthunder" contains "rain", "snowshowers"
    // contains "snow".
    if (has("thunder"))
        return has("hail") ? "wmo-thunderstorm-hail" : "wmo-thunderstorm";
    if (has("snow") && has("shower"))
        return "wmo-snow-showers";
    if (has("snow"))
        return "wmo-snow";
    if (has("shower"))
        return "wmo-rain-showers";
    if (has("sleet"))
        return "wmo-freezing-rain";
    if (base == "lightrain")
        return "wmo-drizzle";
    if (has("rain"))
        return "wmo-rain";
    if (base == "fog")
        return "wmo-fog";
    if (base == "cloudy")
        return "wmo-overcast";
    if (base == "partlycloudy")
        return "wmo-partly-cloudy";
    if (base == "fair")
        return "wmo-mainly-clear";
    if (base == "clearsky")
        return "wmo-clear-sky";
    return "wmo-unknown";
}

/// WMO weather interpretation codes -> translation ids (the texts live in
/// translations/*.json).
const char* weatherTextId(int code) {
    switch (code) {
    case 0: return "wmo-clear-sky";
    case 1: return "wmo-mainly-clear";
    case 2: return "wmo-partly-cloudy";
    case 3: return "wmo-overcast";
    case 45:
    case 48: return "wmo-fog";
    case 51:
    case 53:
    case 55: return "wmo-drizzle";
    case 61:
    case 63:
    case 65: return "wmo-rain";
    case 66:
    case 67: return "wmo-freezing-rain";
    case 71:
    case 73:
    case 75:
    case 77: return "wmo-snow";
    case 80:
    case 81:
    case 82: return "wmo-rain-showers";
    case 85:
    case 86: return "wmo-snow-showers";
    case 95: return "wmo-thunderstorm";
    case 96:
    case 99: return "wmo-thunderstorm-hail";
    default: return "wmo-unknown";
    }
}

} // namespace

WeatherApp::WeatherApp(Object* parent) : Object(parent) {
    // UI translations (translations/<lang>.json next to the executable or
    // in the working directory).
    auto* tr = Firmir::Translator::instance();
    tr->addTranslationFile(Firmir::String("translations/en.json"));
    tr->addTranslationFile(Firmir::String("translations/ru.json"));
    // Startup language: Russian; FIRMIR_LANGUAGE overrides (read by the
    // Translator constructor, before the locale).
    if (!std::getenv("FIRMIR_LANGUAGE"))
        tr->setLanguage(Firmir::String("ru"));

    // Initial property values, so the Firml bindings evaluate to something
    // sensible before the first fetch.
    setProperty("cityName", Firmir::String(""));
    setProperty("tempText", Firmir::String(""));
    setProperty("detailsText", Firmir::String(""));
    setProperty("statusText", tr->trId(Firmir::String("pick-city")));
}

void WeatherApp::switchLanguage() {
    auto* tr = Firmir::Translator::instance();
    tr->setLanguage(Firmir::String(
        tr->language() == Firmir::String("en") ? "ru" : "en"));
    // The text properties are snapshots taken at fetch/selection time;
    // re-render them in the new language without a network round trip.
    renderWeather();
}

void WeatherApp::renderWeather() {
    auto* tr = Firmir::Translator::instance();
    if (m_currentCity < 0) {
        setProperty("statusText", tr->trId(Firmir::String("pick-city")));
        return;
    }
    setProperty("cityName", tr->trId(Firmir::String(kCities[m_currentCity].name)));
    if (!m_hasReading) {
        setProperty("statusText", tr->trId(Firmir::String("loading")));
        return;
    }
    char temp[32];
    std::snprintf(temp, sizeof(temp), "%.1f", m_temp);
    char wind[32];
    std::snprintf(wind, sizeof(wind), "%.1f", m_wind);
    setProperty("tempText",
                tr->trId(Firmir::String("temp-format"), {Firmir::String(temp)}));
    setProperty("detailsText",
                tr->trId(Firmir::String("details-format"),
                         {tr->trId(Firmir::String(m_textId.c_str())),
                          Firmir::String(wind)}));
    setProperty("statusText", Firmir::String(m_provider.c_str()));
}

void WeatherApp::selectCity(double index) {
    const int i = static_cast<int>(index);
    if (i < 0 || i >= static_cast<int>(std::size(kCities)))
        return;
    const City& city = kCities[i];
    m_currentCity = i;
    m_hasReading = false;

    setProperty("cityName",
                Firmir::Translator::instance()->trId(
                    Firmir::String(city.name)));
    setProperty("tempText", Firmir::String(""));
    setProperty("detailsText", Firmir::String(""));
    setProperty("statusText",
                        Firmir::Translator::instance()->trId(
                            Firmir::String("loading")));

    // Weather data comes from MET Norway (api.met.no: free, no API key,
    // requires an identifying User-Agent per its terms of service).
    // FIRMIR_WEATHER_API overrides the endpoint with an open-meteo-shaped
    // URL (the tests' mock server uses that shape; the reply format is
    // auto-detected when parsing).
    const char* apiBase = std::getenv("FIRMIR_WEATHER_API");
    char url[512];
    if (apiBase && *apiBase)
        std::snprintf(url, sizeof(url),
                      "%s?latitude=%.4f&longitude=%.4f&"
                      "current=temperature_2m,weather_code,wind_speed_10m",
                      apiBase, city.latitude, city.longitude);
    else
        std::snprintf(url, sizeof(url),
                      "https://api.met.no/weatherapi/locationforecast/2.0/"
                      "compact?lat=%.4f&lon=%.4f",
                      city.latitude, city.longitude);
    Firmir::NetworkRequest request{Firmir::String(url)};
    request.setHeader(
        Firmir::String("User-Agent"),
        Firmir::String("firmir-weather/1.0 (+https://github.com/firmir)"));
    // Bound the wait: an unreachable/silent host must surface an error
    // instead of leaving "loading..." up forever.
    request.setConnectTimeout(std::chrono::milliseconds(5000));
    request.setTransferTimeout(std::chrono::milliseconds(10000));
    Firmir::NetworkReply* reply = m_network.get(request);
    reply->connect(reply, Firmir::NetworkReply::signalFinished(), [this, reply] {
        auto* tr = Firmir::Translator::instance();
        if (reply->error() != Firmir::NetworkReply::Error::NoError) {
            setProperty("statusText",
                        tr->trId(Firmir::String(
                            reply->error() == Firmir::NetworkReply::Error::Timeout
                                ? "status-timeout"
                                : "status-offline")));
            reply->deleteLater();
            return;
        }
        Firmir::JsonDocument::ParseError parseError;
        const Firmir::JsonDocument doc =
            Firmir::JsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != Firmir::JsonDocument::ParseError::NoError ||
            !doc.isObject()) {
            setProperty("statusText",
                        tr->trId(Firmir::String("status-bad-reply")));
            reply->deleteLater();
            return;
        }
        // The reply format is auto-detected: MET Norway compact
        // ({"properties":{"timeseries":[...]}}) or open-meteo
        // ({"current":{...}} / legacy {"current_weather":{...}}).
        double temp = 0.0;
        double wind = 0.0; // m/s
        std::string textId;
        std::string provider;
        const Firmir::JsonObject root = doc.object();
        const Firmir::JsonArray timeseries =
            root.value("properties").toObject().value("timeseries").toArray();
        if (!timeseries.isEmpty()) {
            // MET Norway: timeseries[0].data.instant.details
            // {air_temperature, wind_speed}; the summary symbol lives in
            // next_1_hours (absent for the last hour of the forecast).
            const Firmir::JsonObject data =
                timeseries.first().toObject().value("data").toObject();
            const Firmir::JsonObject details =
                data.value("instant").toObject().value("details").toObject();
            temp = details.value("air_temperature").toDouble();
            wind = details.value("wind_speed").toDouble();
            const std::string symbol =
                data.value("next_1_hours").toObject().value("summary")
                    .toObject().value("symbol_code").toString().toStdString();
            textId = weatherTextIdForSymbol(symbol);
            provider = "met.no";
        } else {
            Firmir::JsonObject current = root.value("current").toObject();
            temp = current.value("temperature_2m").toDouble();
            wind = current.value("wind_speed_10m").toDouble() / 3.6; // -> m/s
            int code =
                static_cast<int>(current.value("weather_code").toDouble());
            if (current.keys().empty()) {
                current = root.value("current_weather").toObject();
                temp = current.value("temperature").toDouble();
                wind = current.value("windspeed").toDouble() / 3.6;
                code = static_cast<int>(
                    current.value("weathercode").toDouble());
            }
            if (current.keys().empty()) {
                setProperty("statusText",
                            tr->trId(Firmir::String("status-bad-reply")));
                reply->deleteLater();
                return;
            }
            textId = weatherTextId(code);
            provider = "open-meteo.org";
        }
        m_temp = temp;
        m_wind = wind;
        m_textId = textId;
        m_provider = provider;
        m_hasReading = true;
        renderWeather(); // formats the texts in the active language
        reply->deleteLater();
    });
}
