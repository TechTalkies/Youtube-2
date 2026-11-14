# RGB Matrix Flight Radar
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-29ABE0?logo=kofi&logoColor=white)](https://ko-fi.com/techtalkies) ![platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Pico%20W-green) ![IDE](https://img.shields.io/badge/IDE-Arduino-blue) ![license](https://img.shields.io/badge/license-MIT-green)


A DIY flight radar built using a **Raspberry Pi Pico W** and a **64×32 RGB LED Matrix**. It retrieves real aircraft tracking data from the OpenSky Network API and displays nearby flights on a retro-style radar interface.

Rotate the encoder to browse and select flights, view flight information, and customise the radar settings.

## Full build video
[![Watch the video](./thumb2.png)](https://youtu.be/mjMD_VJQc5s)


## Features

- Live aircraft data from the OpenSky Network API.
- Animated radar display on a 64×32 RGB LED matrix.
- Browse nearby flights using a rotary encoder.
- Select a flight to view additional information.
- Display flight callsign, altitude, speed and distance.
- Adjustable radar coverage area.
- Adjustable display brightness.
- Configurable distance and speed units.
- Wi-Fi connectivity using Raspberry Pi Pico W.

## Hardware Required

| Component | Description |
|---|---|
| [Raspberry Pi Pico W](https://link.amazon/B0br5yU7C) | Main controller |
| [RGB LED Matrix](https://www.waveshare.com/rgb-matrix-p3-64x32.htm?sku=20117) | 64×32, 3mm pitch |
| [Rotary Encoder ](https://link.amazon/B0aw36QAx)| For navigation and settings |
| Custom PCB | Controller and component connections |
| USB Type-C Panel Mount | Power connection |
| 3D Printed Enclosure | Project enclosure |

## Rotary Encoder Connections

| Rotary Encoder Pin | Raspberry Pi Pico W |
|---|---|
| CLK | GP26 |
| DT | GP27 |
| SW | GP28 |
| VCC | 3.3V |
| GND | GND |

**Note:** Connect the encoder VCC to the Pico W's 3.3V pin, not 5V.

## API Setup

This project uses the [OpenSky Network API](https://opensky-network.org/) to retrieve aircraft tracking information.

You will need an OpenSky Network account and API credentials.

1. Visit [OpenSky Network](https://opensky-network.org/) and create an account.
2. Log in to your account.
3. Open your account page.
4. Find the **API Clients** section.
5. Create an API client and download the credentials.
6. Use the credentials in the Arduino IDE Secrets configuration.

## Arduino IDE Setup

1. Install the Raspberry Pi Pico board package in Arduino IDE.
2. Download the code or clone this repository.
3. Open the project in Arduino IDE.
4. Open the `secrets` tab.
5. Configure the following details:

```cpp
#define WIFI_SSID       "YOUR_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define OPENSKY_CLIENT_ID     "YOUR_CLIENT_ID"
#define OPENSKY_CLIENT_SECRET "YOUR_CLIENT_SECRET"

#define HOME_LAT       YOUR_LATITUDE
#define HOME_LON       YOUR_LONGITUDE
```



## Using the Flight Radar

Once powered on, the device connects to Wi-Fi, authenticates with OpenSky Network and retrieves nearby aircraft information.

### Rotary Encoder Controls

| Action | Function |
|---|---|
| Rotate | Browse and select flights |
| Short press | View additional flight information |
| Long press | Open the settings menu |

### Flight Information

The radar displays:

- Flight callsign
- Altitude
- Speed
- Distance from your configured location

### Settings

The settings menu allows you to adjust:

- Display brightness
- Radar coverage area
- Distance and speed units

## Libraries and Resources

- [OpenSky Network API](https://openskynetwork.github.io/opensky-api/)
- [Raspberry Pi Pico Arduino Core](https://github.com/earlephilhower/arduino-pico)
- [Waveshare RGB Matrix Documentation](https://www.waveshare.com/wiki/RGB-Matrix-P3)

![USB-C mount](./USB-Type-c-panel-mount.png)

## Credits

- Aircraft tracking data provided by [OpenSky Network](https://opensky-network.org/).
- PCB manufacturing by [Aivon](https://aivon.com/).

## License

Please give credit back to the Channel or this repo when using the code.

---

**Built by Tech Talkies**

YouTube: https://www.youtube.com/@techtalkies1