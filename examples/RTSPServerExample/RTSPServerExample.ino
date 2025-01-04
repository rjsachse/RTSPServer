#include <WiFi.h>
#include <RTSPServer.h>
#include "esp_camera.h"

// ===================
// Select camera model
// ===================
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE  // Has PSRAM
//#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_CAMS3_UNIT  // Has PSRAM
//#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
//#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
#define CAMERA_MODEL_XENOIONEX // Has PSRAM Custom Board
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3 // Has PSRAM
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3 // Has PSRAM
#include "camera_pins.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "**********";
const char *password = "**********";

// RTSPServer instance
RTSPServer rtspServer;

// Define HAVE_AUDIO to include audio-related code
#define HAVE_AUDIO // Comment out if don't have audio

#ifdef HAVE_AUDIO
#include <ESP_I2S.h>
// I2SClass object for I2S communication
I2SClass I2S;

// I2S pins configuration
#define I2S_SCK          4  // Serial Clock (SCK) or Bit Clock (BCLK)
#define I2S_WS           5  // Word Select (WS) or Left Right Clock (LRCLK)
#define I2S_SDI          6  // Serial Data In (Mic)

// Audio variables
int sampleRate = 16000;      // Sample rate in Hz
const size_t sampleBytes = 1024; // Sample buffer size (in bytes)
int16_t* sampleBuffer = NULL;  // Pointer to the sample buffer
#endif


// Variable to hold quality for RTSP frame
int quality;
// Task handles
TaskHandle_t videoTaskHandle = NULL; 
TaskHandle_t audioTaskHandle = NULL; 
TaskHandle_t subtitlesTaskHandle = NULL;


/** 
 * @brief Sets up the camera with the specified configuration. 
*/
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  // for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif
  Serial.println("Camera Setup Complete");
}

/** 
 * @brief Retrieves the current frame quality from the camera. 
*/
void getFrameQuality() { 
  sensor_t * s = esp_camera_sensor_get(); 
  quality = s->status.quality; 
  Serial.printf("Camera Quality is: %d\n", quality);
}

#ifdef HAVE_AUDIO
/** 
 * @brief Sets up the I2S microphone. 
 * 
 * @return true if setup is successful, false otherwise. 
 */
static bool setupMic() {
  bool res;
  // I2S mic and I2S amp can share same I2S channel
  I2S.setPins(I2S_SCK, I2S_WS, -1, I2S_SDI, -1); // BCLK/SCK, LRCLK/WS, SDOUT, SDIN, MCLK
  res = I2S.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  if (sampleBuffer == NULL) sampleBuffer = (int16_t*)malloc(sampleBytes);
  return res;
}

/** 
 * @brief Reads audio data from the I2S microphone. 
 * 
 * @return The number of bytes read. 
 */
static size_t micInput() {
  // read esp mic
  size_t bytesRead = 0;
  bytesRead = I2S.readBytes((char*)sampleBuffer, sampleBytes);
  return bytesRead;
}
/**
 * @brief Task to send audio data via RTP. 
 */
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

#endif

/** 
 * @brief Task to send jpeg frames via RTP. 
*/
void sendVideo(void* pvParameters) { 
  while (true) { 
    // Send frame via RTP
    if(rtspServer.readyToSendFrame()) {
      camera_fb_t* fb = esp_camera_fb_get();
      rtspServer.sendRTSPFrame(fb->buf, fb->len, quality, fb->width, fb->height);
      esp_camera_fb_return(fb);
    }
    vTaskDelay(pdMS_TO_TICKS(1)); 
  }
}

/**
 * @brief Task to send subtitles via RTP. 
 */
void sendSubtitles(void* pvParameters) {
  char data[100];
  while (true) {
    if(rtspServer.readyToSendSubtitles()) {
      size_t len = snprintf(data, sizeof(data), "FPS: %lu", rtspServer.rtpFps);
      rtspServer.sendRTSPSubtitles(data, len);
    }
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second has to be 1 second
  }
}

void printDeviceInfo() {
  // Local function to format size
  auto fmtSize = [](size_t bytes) -> String {
    const char* sizes[] = { "B", "KB", "MB", "GB" };
    int order = 0;
    while (bytes >= 1024 && order < 3) {
      order++;
      bytes = bytes / 1024;
    }
    return String(bytes) + " " + sizes[order];
  };

  // Print device information
  Serial.println("");
  Serial.println("==== Device Information ====");
  Serial.printf("ESP32 Chip ID: %u\n", ESP.getEfuseMac());
  Serial.printf("Flash Chip Size: %s\n", fmtSize(ESP.getFlashChipSize()));
  if (psramFound()) {
    Serial.printf("PSRAM Size: %s\n", fmtSize(ESP.getPsramSize()));
  } else {
    Serial.println("No PSRAM is found");
  }
  Serial.println("");
  // Print sketch information
  Serial.println("==== Sketch Information ====");
  Serial.printf("Sketch Size: %s\n", fmtSize(ESP.getSketchSize()));
  Serial.printf("Free Sketch Space: %s\n", fmtSize(ESP.getFreeSketchSpace()));
  Serial.printf("Sketch MD5: %s\n", ESP.getSketchMD5().c_str());
  Serial.println("");
  // Print task information
  Serial.println("==== Task Information ====");
  Serial.printf("Total tasks: %u\n", uxTaskGetNumberOfTasks() - 1);
  Serial.println("");
  // Print network information
  Serial.println("==== Network Information ====");
  Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  Serial.println("");
  // Print RTSP server information
  Serial.println("==== RTSP Server Information ====");
  Serial.printf("RTSP Port: %d\n", rtspServer.rtspPort);
  Serial.printf("Sample Rate: %d\n", rtspServer.sampleRate);
  Serial.printf("Transport Type: %d\n", rtspServer.transport);
  Serial.printf("Video Port: %d\n", rtspServer.rtpVideoPort);
  Serial.printf("Audio Port: %d\n", rtspServer.rtpAudioPort);
  Serial.printf("Subtitles Port: %d\n", rtspServer.rtpSubtitlesPort);
  Serial.printf("RTP IP: %s\n", rtspServer.rtpIp.toString().c_str());
  Serial.printf("RTP TTL: %d\n", rtspServer.rtpTTL);
  Serial.println("");
  Serial.printf("RTSP Address: rtsp://%s:%d\n", WiFi.localIP().toString().c_str(), rtspServer.rtspPort);
  Serial.println("==============================");
  Serial.println("");
}


void setup() {
  // Initialize serial communication
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Setup camera
  setupCamera();
  getFrameQuality();

#ifdef HAVE_AUDIO
  // Setup microphone
  if (setupMic()) {
    Serial.println("Microphone Setup Complete");
    // Create tasks for sending audio
    xTaskCreate(sendAudio, "Audio", 2560, NULL, 1, &audioTaskHandle);
  } else {
    Serial.println("Mic Setup Failed!");
  }
#endif

  // Create tasks for sending video, and subtitles
  xTaskCreate(sendVideo, "Video", 3584, NULL, 1, &videoTaskHandle);
  xTaskCreate(sendSubtitles, "Subtitles", 2048, NULL, 1, &subtitlesTaskHandle);

  // Initialize the RTSP server
  /**
   * @brief Initializes the RTSP server with the specified configuration.
   * 
   * This method can be called with specific parameters, or the parameters
   * can be set directly in the RTSPServer instance before calling begin().
   * If any parameter is not explicitly set, the method uses default values.
   * 
   * @param transport The transport type. Default is VIDEO_AND_SUBTITLES. Options are (VIDEO_ONLY, AUDIO_ONLY, VIDEO_AND_AUDIO, VIDEO_AND_SUBTITLES, AUDIO_AND_SUBTITLES, VIDEO_AUDIO_SUBTITLES).
   * @param rtspPort The RTSP port to use. Default is 554.
   * @param sampleRate The sample rate for audio streaming. Default is 0 must pass or set if using audio.
   * @param port1 The first port (used for video, audio or subtitles depending on transport). Default is 5430.
   * @param port2 The second port (used for audio or subtitles depending on transport). Default is 5432.
   * @param port3 The third port (used for subtitles). Default is 5434.
   * @param rtpIp The IP address for RTP multicast streaming. Default is IPAddress(239, 255, 0, 1).
   * @param rtpTTL The TTL value for RTP multicast packets. Default is 64.
   * @return true if initialization is successful, false otherwise.
   *
   * Example usage:
   * // Option 1: Start RTSP server with default values
   * if (rtspServer.begin()) { 
   *   Serial.println("RTSP server started successfully on port 554"); 
   * } else { 
   *   Serial.println("Failed to start RTSP server"); 
   * }
   * 
   * // Option 2: Set variables directly and then call begin
   * rtspServer.transport = RTSPServer::VIDEO_AUDIO_SUBTITLES; 
   * rtspServer.sampleRate = 48000; 
   * rtspServer.rtspPort = 8554; 
   * rtspServer.rtpIp = IPAddress(239, 255, 0, 1); 
   * rtspServer.rtpTTL = 64; 
   * rtspServer.rtpVideoPort = 5004; 
   * rtspServer.rtpAudioPort = 5006; 
   * rtspServer.rtpSubtitlesPort = 5008;
   * if (rtspServer.begin()) { 
   *   Serial.println("RTSP server started successfully"); 
   * } else { 
   *   Serial.println("Failed to start RTSP server"); 
   * }
   * 
   * // Option 3: Set variables in the begin call
   * if (rtspServer.begin(RTSPServer::VIDEO_AUDIO_SUBTITLES, 554, sampleRate)) { 
   *   Serial.println("RTSP server started successfully"); 
   * } else { 
   *   Serial.println("Failed to start RTSP server"); 
   * }
   */

#ifdef HAVE_AUDIO
  if (rtspServer.begin(RTSPServer::VIDEO_AUDIO_SUBTITLES, 554, sampleRate)) {
    Serial.printf("RTSP server started successfully, Connect to rtsp://%s:554/\n", WiFi.localIP().toString().c_str());
  } else { 
    Serial.println("Failed to start RTSP server"); 
  }
#else
  if (rtspServer.begin()) { 
    Serial.printf("RTSP server started successfully using default values, Connect to rtsp://%s:554/\n", WiFi.localIP().toString().c_str());
  } else { 
    Serial.println("Failed to start RTSP server"); 
  }
#endif
}

void loop() {
  printDeviceInfo(); // just print out info about device
  delay(1000);
  vTaskDelete(NULL); // free 8k ram and delete the loop
}
