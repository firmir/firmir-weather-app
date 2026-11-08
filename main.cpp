// firmir-weather: current weather for preset cities from the open-meteo API.
// The UI is declared in weather.fml (compiled by firmlc); the fetch/parse
// logic lives in WeatherApp (WeatherApp.cpp); this file only provides the
// --fetch CLI mode, creates the window and runs the event loop.
//
// Backend selection via FIRMIR_GUI_DISPLAY / FIRMIR_GUI_INPUT /
// FIRMIR_GUI_PLUGIN_PATH, like every firmir-gui app.

#include "weather.hpp"
#include "WeatherApp.hpp"

#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/json.hpp>
#include <firmir/log.hpp>
#include <firmir/networkaccessmanager.hpp>
#include <firmir/networkreply.hpp>
#include <firmir/string.hpp>
#include <firmir/thread.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

/// WMO weather interpretation codes -> short text (CLI mode).
const char* weatherText(int code) {
    switch (code) {
    case 0: return "clear sky";
    case 1: return "mainly clear";
    case 2: return "partly cloudy";
    case 3: return "overcast";
    case 45:
    case 48: return "fog";
    case 51:
    case 53:
    case 55: return "drizzle";
    case 61:
    case 63:
    case 65: return "rain";
    case 66:
    case 67: return "freezing rain";
    case 71:
    case 73:
    case 75:
    case 77: return "snow";
    case 80:
    case 81:
    case 82: return "rain showers";
    case 85:
    case 86: return "snow showers";
    case 95: return "thunderstorm";
    case 96:
    case 99: return "thunderstorm with hail";
    default: return "unknown";
    }
}

} // namespace

int main(int argc, char** argv) {
    // --fetch <lat> <lon>: fetch once and print the parsed result (no UI).
    if (argc > 3 && std::string(argv[1]) == "--fetch") {
        const char* apiBase = std::getenv("FIRMIR_WEATHER_API");
        if (!apiBase || !*apiBase)
            apiBase = "https://api.open-meteo.com/v1/forecast";
        char url[512];
        std::snprintf(url, sizeof(url),
                      "%s?latitude=%.4f&longitude=%.4f&"
                      "current=temperature_2m,weather_code,wind_speed_10m",
                      apiBase, std::atof(argv[2]), std::atof(argv[3]));
        Firmir::NetworkAccessManager network;
        Firmir::NetworkReply* reply =
            network.get(Firmir::NetworkRequest(Firmir::String(url)));
        reply->connect(reply, Firmir::NetworkReply::signalFinished(), [reply] {
            if (reply->error() != Firmir::NetworkReply::Error::NoError) {
                std::printf("fetch error: %s\n",
                            reply->errorString().toStdString().c_str());
                Firmir::Thread::mainThread()->quit();
                return;
            }
            const Firmir::JsonDocument doc =
                Firmir::JsonDocument::fromJson(reply->readAll());
            Firmir::JsonObject current =
                doc.object().value("current").toObject();
            double temp = current.value("temperature_2m").toDouble();
            double wind = current.value("wind_speed_10m").toDouble();
            int code =
                static_cast<int>(current.value("weather_code").toDouble());
            if (current.keys().empty()) { // legacy current_weather block
                current = doc.object().value("current_weather").toObject();
                temp = current.value("temperature").toDouble();
                wind = current.value("windspeed").toDouble();
                code = static_cast<int>(current.value("weathercode").toDouble());
            }
            if (current.keys().empty()) {
                std::printf("bad JSON reply\n");
                Firmir::Thread::mainThread()->quit();
                return;
            }
            std::printf("temp %.1f C, wind %.1f m/s, %s\n", temp, wind,
                        weatherText(code));
            Firmir::Thread::mainThread()->quit();
        });
        return Firmir::Thread::mainThread()->exec();
    }

    auto* platform = Firmir::Gui::Platform::instance();
    if (platform->screens().empty()) {
        std::fprintf(stderr, "no screens — check FIRMIR_GUI_DISPLAY / "
                             "FIRMIR_GUI_PLUGIN_PATH\n");
        return 1;
    }

    // Creates the window, the whole UI tree and the WeatherApp logic object,
    // then shows the window (see weather.fml).
    WeatherObjects objects = createWeather(platform->screens().front());
    objects.root->connect<bool>(
        objects.root, Firmir::Gui::Window::signalVisibilityChanged,
        [](bool visible) {
            if (!visible)
                Firmir::Thread::mainThread()->quit();
        });

    const int rc = Firmir::Thread::mainThread()->exec();
    Firmir::logInfo() << "firmir-weather: bye";
    delete objects.root; // owns the whole tree
    return rc;
}
