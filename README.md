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
   - Move the `ESP32-RTSPServer` folder to your Arduino libraries directory (usually `Documents/Arduino/libraries`).

2. **Library Manager**:
   - Open the Arduino IDE.
   - Search for ESP32-RTSPServer
   - or
   - Go to `Sketch` -> `Include Library` -> `Add .ZIP Library...`.
   - Select the downloaded `ESP32-RTSPServer.zip` file.

## Usage
### Include the Library
Basic Setup
```cpp

#include <ESP32-RTSPServer.h>

// Include all other libraries and setups eg Camera, Audio

// RTSPServer instance
RTSPServer rtspServer;

// Creates a new task so the main camera task can continue
//#define RTSP_VIDEO_NONBLOCK // uncomment if already have a camera task.

// Task handles
TaskHandle_t videoTaskHandle = NULL; 
TaskHandle_t audioTaskHandle = NULL; 
TaskHandle_t subtitlesTaskHandle = NULL;

void getFrameQuality() { 
  sensor_t * s = esp_camera_sensor_get(); 
  quality = s->status.quality; 
  Serial.printf("Camera Quality is: %d\n", quality);
}

void sendVideo(void* pvParameters) { 
  while (true) { 
    // Send frame via RTP
    if(rtspServer.readyToSendFrame()) { // Must use
      camera_fb_t* fb = esp_camera_fb_get();
      rtspServer.sendRTSPFrame(fb->buf, fb->len, quality, fb->width, fb->height);
      esp_camera_fb_return(fb);
    }
    vTaskDelay(pdMS_TO_TICKS(1)); 
  }
}

void sendAudio(void* pvParameters) { 
  while (true) { 
    size_t bytesRead = 0;
    if(rtspServer.readyToSendAudio()) {
      bytesRead = micInput();
      if (bytesRead) rtspServer.sendRTSPAudio(sampleBuffer, bytesRead);
      else Serial.println("No audio Recieved");
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Delay for 1 second 
  }
}

void sendSubtitles(void* pvParameters) {
  char data[100];
  while (true) {
    if(rtspServer.readyToSendAudio()) {
      size_t len = snprintf(data, sizeof(data), "FPS: %lu", rtspServer.rtpFps);
      rtspServer.sendRTSPSubtitles(data, len);
    }
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second has to be 1 second
  }
}

void setup() {

  getFrameQuality(); //Retrieve frame quality

  // Create tasks for sending video, and subtitles
  xTaskCreate(sendVideo, "Video", 3584, NULL, 1, &videoTaskHandle);
  xTaskCreate(sendAudio, "Audio", 2560, NULL, 1, &audioTaskHandle);
  xTaskCreate(sendSubtitles, "Subtitles", 2048, NULL, 1, &subtitlesTaskHandle);

  // Initialize the RTSP server
   //Example Setup usage:
   // Option 1: Start RTSP server with default values
   if (rtspServer.begin()) { 
   Serial.println("RTSP server started successfully on port 554"); 
   } else { 
   Serial.println("Failed to start RTSP server"); 
   }
   
   // Option 2: Set variables directly and then call begin
   rtspServer.transport = RTSPServer::VIDEO_AUDIO_SUBTITLES; 
   rtspServer.sampleRate = 48000; 
   rtspServer.rtspPort = 8554; 
   rtspServer.rtpIp = IPAddress(239, 255, 0, 1); 
   rtspServer.rtpTTL = 64; 
   rtspServer.rtpVideoPort = 5004; 
   rtspServer.rtpAudioPort = 5006; 
   rtspServer.rtpSubtitlesPort = 5008;
   if (rtspServer.begin()) { 
   Serial.println("RTSP server started successfully"); 
   } else { 
   Serial.println("Failed to start RTSP server"); 
   }
   
   // Option 3: Set variables in the begin call
   if (rtspServer.begin(RTSPServer::VIDEO_AUDIO_SUBTITLES, 554, sampleRate)) { 
   Serial.println("RTSP server started successfully"); 
   } else { 
   Serial.println("Failed to start RTSP server"); 
   }
}

void loop() {

}
   
```

### Summary:
- ~~**Overview**: Brief introduction to what the library does.~~
- ~~**Features**: Highlight key features.~~
- ~~**Installation**: Instructions for installing the library.~~
- ~~**Usage**: Examples of how to include and use the library in a sketch.~~
- **API Reference**: Documentation for the main functions and classes.
- ~~**Example**: Simple example code.~~
- ~~**License**: Licensing information.~~
- **Contributions**: How to contribute to the project.
- **Support**: Where to get help.
- **Acknowledgements**: Any special thanks or acknowledgments.
