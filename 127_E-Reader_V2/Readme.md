# ESP32 E-Ink E-Reader
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-29ABE0?logo=kofi&logoColor=white)](https://ko-fi.com/techtalkies) ![platform](https://img.shields.io/badge/platform-XIAO%20ESP32--S3-green) ![IDE](https://img.shields.io/badge/IDE-Arduino-blue) ![license](https://img.shields.io/badge/license-MIT-green)

A feature-rich, ultra-low-power DIY e-reader built using a **[Seeed Studio XIAO Driver board](https://www.seeedstudio.com/2-13-Monochrome-ePaper-Display-with-122x250-Pixels-p-5778.html?sensecap_affiliate=P9GHEkF&referring_service=link)** and an **[E-Ink Display](https://www.seeedstudio.com/2-13-Monochrome-ePaper-Display-with-122x250-Pixels-p-5778.html?sensecap_affiliate=P9GHEkF&referring_service=link)**. It connects to the internet to browse the Project Gutenberg library, downloads books directly to an SD card, and allows for completely offline reading with persistent bookmarks and customizable settings.

Use the three hardware buttons to navigate the library, download new books, jump pages, and configure your preferred reading experience.

[![Ko-fi Support](../support_me_256.png)](https://ko-fi.com/techtalkies)

## Full build video
[![Watch the video](./thumb2.png)](https://youtu.be/WrvI4v4DobE)

## Features

- **Online Catalog:** Browse live book data from the Gutendex API.
- **Direct Downloads:** Download full `.txt.utf-8` books directly to your SD card.
- **Advanced Power Saving:** Multi-stage sleep architecture (configurable Light and Deep Sleep timers) for weeks of battery life.
- **Persistent Bookmarks:** Automatically saves your reading progress for every book.
- **Customizable UI:** Toggle Dark Mode and adjust E-Ink refresh rates directly from the device.
- **Captive Portal Wi-Fi:** Easily connect to new Wi-Fi networks using a mobile setup page—no hardcoding required.
- **Robust Networking:** Custom chunked-JSON parsing and redirect-chasing for reliable downloads.

## Hardware Required

| Component | Description |
|---|---|
| [E-Paper Driver board](https://www.seeedstudio.com/XIAO-ePaper-Display-Board-EE05-p-6755.html?sensecap_affiliate=P9GHEkF&referring_service=link) | With Xiao ESP32S3 Plus |
| [E-Ink Display](https://www.seeedstudio.com/2-13-Monochrome-ePaper-Display-with-122x250-Pixels-p-5778.html?sensecap_affiliate=P9GHEkF&referring_service=link) | 2.13" E-Paper display |
| MicroSD Card Module (Mini) | For storing books and settings |
| MicroSD Card | Formatted to FAT32 |
| 3.7V LiPo Battery | Portable power source |
| 3D Printed Enclosure | Project enclosure |

**SD Card Module (SPI1)**
- CS: D15
- SCK: D17
- MISO: D18
- MOSI: D19

## API Setup

This project uses the [Gutendex API](https://gutendex.com/) to retrieve the Project Gutenberg catalog. 
**No API keys or accounts are required.**



### Flashing
You can flash the firmware from https://techtalkies.github.io/flash.html

## Using the E-Reader

### Initial Wi-Fi Setup
On its first boot, the device operates entirely offline. To download books:
1. Go to `[ Settings ]` -> `Setup WiFi`.
2. Connect your smartphone to the temporary Wi-Fi network: **E-Reader Setup** (Password: `12345678`).
3. A login portal will appear (or navigate to `192.168.4.1`).
4. Select your home network, enter the password, and save. The device will switch back to normal mode.

### Sleep States
- **Light Sleep:** Freezes the CPU to save power while you read. Wakes instantly on any button press.
- **Deep Sleep:** Powers down the display and system completely after extended inactivity. Press the **OK (Left)** button to wake the device and instantly resume your exact page.

## Credits

- Book catalog and data provided by [Project Gutenberg](https://www.gutenberg.org/) and [Gutendex](https://gutendex.com/).

## License

Please give credit back to the Channel or this repo when using the code.

---

**Built by Tech Talkies**

YouTube: https://www.youtube.com/@techtalkies1