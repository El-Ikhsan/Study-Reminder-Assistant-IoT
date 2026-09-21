<h1 align="center">Study Reminder Assistant - IoT</h1>

<div align="center">
  <p><em>Other repositories in this project:</em></p>
  <h4>
    <a href="https://github.com/El-Ikhsan/Study-Reminder-Assistant">Backend</a>
    <br>
    <a href="https://github.com/El-Ikhsan/Study-Reminder-Assistant-Frontend">Frontend</a>
  </h4>
</div> 

<div align="center">
  <a href="#what-is">About</a>
  <span> • </span>
  <a href="#features">Features</a>
  <span> • </span>
  <a href="#hardware-specs">Hardware Specs</a>
  <span> • </span>
  <a href="#requirements">Requirements</a>
  <span> • </span>
  <a href="#flashing--setup">Setup</a>
  <span> • </span>
  <a href="#license">License</a>
  <p></p>
</div> 

<div align="center">
 
[![Repo Size](https://img.shields.io/github/repo-size/El-Ikhsan/Study-Reminder-Assistant-IoT?style=flat-square&color=blue)](https://github.com/El-Ikhsan/Study-Reminder-Assistant-IoT)
[![GitHub Issues](https://img.shields.io/github/issues/El-Ikhsan/Study-Reminder-Assistant-IoT?style=flat-square&color=orange)](https://github.com/El-Ikhsan/Study-Reminder-Assistant-IoT/issues)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](https://opensource.org/licenses/MIT)
![C++](https://img.shields.io/badge/C%2B%2B-00599C.svg?style=flat-square&logo=cplusplus&logoColor=white)
![Arduino](https://img.shields.io/badge/Arduino-00979D.svg?style=flat-square&logo=arduino&logoColor=white)
![PlatformIO](https://img.shields.io/badge/PlatformIO-F5822A.svg?style=flat-square&logo=platformio&logoColor=white)

</div>

## Showcase

<img  alt="booting" src="https://github.com/user-attachments/assets/c749b4bf-9e51-4c30-b997-9c6424bc4ab5" />
<img  alt="wiring-iot" src="https://github.com/user-attachments/assets/5b034261-5dc1-4506-a44d-c486b1e394af" />

## What is

Study Reminder Assistant (Rinchan) is an intelligent study companion system combining Pomodoro time management, environmental IoT sensor monitoring (temperature, light, and noise), and AI-driven interactions. This repository contains the **Firmware IoT**, running on an ESP32-S3 microcontroller to collect environmental telemetry, render real-time facial expressions on a TFT display, play audio cues, and communicate seamlessly with the backend via WebSockets.

## Features

- Real-time environmental sensing: temperature (BMP280), ambient light (BH1750), and noise levels (INMP441 I2S microphone).
- Dynamic facial emotion animations on TFT LCD (ILI9341) reacting to study conditions and Pomodoro states.
- High-quality audio playback via I2S DAC amplifier (MAX98357A).
- Bi-directional real-time communication with the backend via WebSockets.
- Wi-Fi manager with captive portal AP configuration and NVS persistent storage.
- Physical button control (short press, long press) and LED indicators.

## Hardware Specs

- **Microcontroller**: ESP32-S3 DevKitC-1 (16MB Flash, 8MB PSRAM)
- **Display**: ILI9341 2.8" SPI TFT LCD (240x320)
- **Temperature & Pressure Sensor**: BMP280 (I2C: SDA 4, SCL 5)
- **Ambient Light Sensor**: BH1750 (I2C: SDA 4, SCL 5)
- **Microphone**: INMP441 (I2S: BCLK 11, LRC 12, DIN 13)
- **Audio DAC & Speaker**: MAX98357A (I2S: BCLK 15, LRC 16, DOUT 17)
- **Storage**: LittleFS for animations, images, and audio assets

## Requirements

Make sure your system has the following tools installed:

- [PlatformIO](https://platformio.org/) (via VS Code extension or CLI)
- ESP32 USB Driver (CP210x or CH340 depending on your board)

## Flashing & Setup

1. Clone the repository and enter the project directory.

```bash
git clone https://github.com/El-Ikhsan/Study-Reminder-Assistant-IoT.git
cd Study-Reminder-Assistant-IoT
```

2. Configure backend endpoints in `include/config.h`:
   - Set `LOCAL_DEV_MODE 1` to use local backend (`wrangler dev`).
   - Set `LOCAL_DEV_MODE 0` to use Cloudflare Workers production.

3. Build and upload firmware to the ESP32-S3 board.

```bash
pio run --target upload
```

4. Upload filesystem assets (LittleFS animations and audio).

```bash
pio run --target uploadfs
```

5. Monitor serial log output.

```bash
pio device monitor
```

## License

This project is licensed under the MIT License.
