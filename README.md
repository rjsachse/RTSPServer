# RTSPServer

## Overview
This is my first library I have created. RTSPServer Library is for the ESP32, designed to stream video, audio, and subtitles. This library allows you to easily create an RTSP server for streaming multimedia content using an ESP32. It supports various transport types and integrates with the ESP32 camera and I2S audio interfaces.

## Features
- **Video Streaming**: Stream video from the ESP32 camera.
- **Audio Streaming**: Stream audio using I2S.
- **Subtitles**: Stream subtitles alongside video and audio.
- **Transport Types**: Supports multiple transport types, including video-only, audio-only, and combined streams.

## Prerequisites
This library requires the ESP32 core by Espressif. Ensure you have at least version 3.0.7 installed.

### Installing the ESP32 Core by Espressif
1. Open the Arduino IDE.
2. Go to `File` -> `Preferences`.
3. In the "Additional Board Manager URLs" field, add the following URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
4. Click `OK` to close the Preferences window.
5. Go to `Tools` -> `Board` -> `Boards Manager`.
6. Search for `esp32` and install the latest version by Espressif.

## Installation
1. **Manual Installation**:
   - Download the library from [GitHub](https://github.com/rjsachse/ESP32-RTSPServer).
   - Unzip the downloaded file.
   - Move the `MyLibrary` folder to your Arduino libraries directory (usually `Documents/Arduino/libraries`).

2. **Library Manager**:
   - Open the Arduino IDE.
   - Go to `Sketch` -> `Include Library` -> `Add .ZIP Library...`.
   - Select the downloaded `MyLibrary.zip` file.

## Usage
### Include the Library
Include the library in your sketch:
```cpp
#include <RTSPServer.h>
```

### Summary:
- ~~**Overview**: Brief introduction to what the library does.~~
- ~~**Features**: Highlight key features.~~
- ~~**Installation**: Instructions for installing the library.~~
- **Usage**: Examples of how to include and use the library in a sketch.
- **API Reference**: Documentation for the main functions and classes.
- **Example**: Simple example code.
- **License**: Licensing information.
- **Contributions**: How to contribute to the project.
- **Support**: Where to get help.
- **Acknowledgements**: Any special thanks or acknowledgments.
