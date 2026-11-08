# firmir-weather

Current weather for preset cities (Moscow, London, Tokyo, New York, Yakutsk)
from the MET Norway API (api.met.no — free, no API key; open-meteo-shaped
endpoints are also understood, see `FIRMIR_WEATHER_API` in WeatherApp.cpp) —
firmir-networking fetches, the JSON parser in firmir-core parses. The UI
starts in Russian; the top-right button toggles English, `FIRMIR_LANGUAGE`
overrides the startup language.

```
cmake -S . -B build && cmake --build build -j

Firmir is located via pkg-config (installed `firmir-*.pc` files); if not
found there, the sibling `../firmir` checkout is configured and built.
./build/firmir-weather                    # the UI
./build/firmir-weather --fetch 55.75 37.62  # one-shot CLI fetch (no UI)
```

Requires network access; offline the status line shows the fetch error.
