# esp8266-dash

4.2" Waveshare e-paper display hooked up to a NodeMCU 1.0 ESP8266.

Fetches data and updates display every half hour from local server.

## Client

### Wiring

#### Display

| NodeMCU | Display |
| ------- | ------- |
| D1      | BUSY    |
| D0      | RST     |
| D2      | DC      |
| D8      | CS      |
| D5      | CLK     |
| D7      | DIN     |
| GND     | GND     |
| 3V3     | VCC     |

Note: this wiring may make ESP deep sleep difficult/unusable. However, D3 and D4
are FLASH and TXD1 respectively. When I used TXD1 as a GPIO pin, I was unable to
get usable content from the `Serial` calls when connected over USB, so I opted
to not use that pin. FLASH had some caveats as well so I just didn't bother.

## Server

* Fetches weather data from NOAA
* Maybe other things???

### Running

```bash
python3 -m venv .venv --prompt esp8266-dash-server
source ./.venv/bin/activate
pip install -e '.[dev]'  # with dev deps
hypercorn api:app --bind 0.0.0.0:8000 --reload
```

### Containerize

```bash
docker buildx create --use
docker buildx build --platform=linux/amd64,linux/arm64 -f Containerfile -t guppy0130/esp8266-dash-server:latest -t guppy0130/esp8266-dash-server:v0.1.0 --push  .
```
