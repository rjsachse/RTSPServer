#include "ESP32-RTSPServer.h"

const char* RTSPServer::LOG_TAG = "RTSPServer";

/**
 * @brief Constructor for the RTSPServer class.
 * Initializes member variables and creates the clients mutex.
 */
RTSPServer::RTSPServer()
  : rtpFps(0),
    // User can change these settings
    transport(VIDEO_ONLY), // Default transport 
    sampleRate(0),
    rtspPort(554),
    rtpIp(IPAddress(239, 255, 0, 1)), // Default RTP IP 
    rtpTTL(64), // Default TTL
    rtpVideoPort(5430),
    rtpAudioPort(5432),
    rtpSubtitlesPort(5434),
    maxRTSPClients(3),
    //
    rtspSocket(-1),
    videoRtpSocket(-1),
    audioRtpSocket(-1),
    subtitlesRtpSocket(-1),
    activeRTSPClients(0),
    maxClients(1),
    rtpVideoTaskHandle(NULL),
    rtspTaskHandle(NULL),
    rtspStreamBuffer(NULL),
    rtspStreamBufferSize(0),
    //rtspFrameSemaphore(NULL),
    rtpFrameSent(true),
    rtpAudioSent(true),
    rtpSubtitlesSent(true),
    vQuality(0),
    vWidth(0),
    vHeight(0),
    videoSequenceNumber(0),
    videoTimestamp(0),
    audioSequenceNumber(0),
    audioTimestamp(0),
    subtitlesSequenceNumber(0),
    subtitlesTimestamp(0),
    rtpFrameCount(0),
    lastRtpFPSUpdateTime(0),
    videoCh(0),
    audioCh(0),
    subtitlesCh(0),
    isVideo(false),
    isAudio(false),
    isSubtitles(false),
    firstClientConnected(false),
    firstClientIsMulticast(false),
    firstClientIsTCP(false)
{
    clientsMutex = xSemaphoreCreateMutex(); // Initialize the mutex
    sendTcpMutex = xSemaphoreCreateMutex(); // Initialize the mutex
    maxClientsMutex = xSemaphoreCreateMutex();
}

/**
 * @brief Destructor for the RTSPServer class.
 * Cleans up resources such as sockets and stream buffer.
 */
RTSPServer::~RTSPServer() {
  // Clean up resources
  deinit();
  vSemaphoreDelete(this->clientsMutex);
}

/**
 * @brief Initializes the RTSP server with the specified configuration.
 * @param transport The transport type (VIDEO_ONLY, AUDIO_ONLY, VIDEO_AND_AUDIO, etc.).
 * @param rtspPort The RTSP port to use.
 * @param sampleRate The sample rate for audio streaming.
 * @param port1 The first port (used for video or audio depending on transport).
 * @param port2 The second port (used for audio or subtitles depending on transport).
 * @param port3 The third port (used for subtitles).
 * @param rtpIp The IP address for RTP streaming.
 * @param rtpTTL The TTL value for RTP packets.
 * @return true if initialization is successful, false otherwise.
 */
bool RTSPServer::init(TransportType transport, uint16_t rtspPort, uint32_t sampleRate, uint16_t port1, uint16_t port2, uint16_t port3, IPAddress rtpIp, uint8_t rtpTTL) {
  this->transport = (transport != NONE) ? transport : this->transport;
  this->rtspPort = (rtspPort != 0) ? rtspPort : this->rtspPort;
  this->rtpIp = (rtpIp != IPAddress()) ? rtpIp : this->rtpIp;
  this->rtpTTL = (rtpTTL != 255) ? rtpTTL : this->rtpTTL;

  // Check if both sampleRate and this->sampleRate are 0 and handle the error for specified transport types
  if (transport == AUDIO_ONLY || transport == VIDEO_AND_AUDIO || transport == AUDIO_AND_SUBTITLES || transport == VIDEO_AUDIO_SUBTITLES) {
    if (this->sampleRate == 0 && sampleRate == 0) {
      if (Serial) {
        Serial.printf("RTSP Server Error: Sample rate must be set to use audio\n");
      }
      return false; // Return false to indicate failure
    }
    // Set the sampleRate if the passed sampleRate is not 0
    if (sampleRate != 0) {
      this->sampleRate = sampleRate;
    }
  }

  // Dynamically assign ports based on transport type
  switch (this->transport) {
    case VIDEO_ONLY:
      this->rtpVideoPort = (port1 != 0) ? port1 : this->rtpVideoPort;
      this->isVideo = true;
      break;
    case AUDIO_ONLY:
      this->rtpAudioPort = (port1 != 0) ? port1 : this->rtpAudioPort;
      this->isAudio = true;
      break;
    case SUBTITLES_ONLY:
      this->rtpSubtitlesPort = (port1 != 0) ? port1 : this->rtpSubtitlesPort;
      this->isSubtitles = true;
      break;
    case VIDEO_AND_AUDIO:
      this->rtpVideoPort = (port1 != 0) ? port1 : this->rtpVideoPort;
      this->rtpAudioPort = (port2 != 0) ? port2 : this->rtpAudioPort;
      this->isVideo = true;
      this->isAudio = true;
      break;
    case VIDEO_AND_SUBTITLES:
      this->rtpVideoPort = (port1 != 0) ? port1 : this->rtpVideoPort;
      this->rtpSubtitlesPort = (port2 != 0) ? port2 : this->rtpSubtitlesPort;
      this->isVideo = true;
      this->isSubtitles = true;
      break;
    case AUDIO_AND_SUBTITLES:
      this->rtpAudioPort = (port1 != 0) ? port1 : this->rtpAudioPort;
      this->rtpSubtitlesPort = (port2 != 0) ? port2 : this->rtpSubtitlesPort;
      this->isAudio = true;
      this->isSubtitles = true;
      break;
    case VIDEO_AUDIO_SUBTITLES:
      this->rtpVideoPort = (port1 != 0) ? port1 : this->rtpVideoPort;
      this->rtpAudioPort = (port2 != 0) ? port2 : this->rtpAudioPort;
      this->rtpSubtitlesPort = (port3 != 0) ? port3 : this->rtpSubtitlesPort;
      this->isVideo = true;
      this->isAudio = true;
      this->isSubtitles = true;
      break;
    case NONE:
      ESP_LOGE(LOG_TAG, "Transport type can not be NONE");
      return false;
    default:
      ESP_LOGE(LOG_TAG, "Invalid transport type for this init method");
      return false;  // Return false to indicate failure
  }

  // Call prepRTSP and return its result
  return prepRTSP();
}

/**
 * @brief Deinitialize the RTSP server. 
 */
void RTSPServer::deinit() {
  // Close Tasks
  if (this->rtspTaskHandle != NULL) {
    vTaskDelete(this->rtspTaskHandle);
    this->rtspTaskHandle = NULL;
  }
  if (this->rtpVideoTaskHandle != NULL) {
    vTaskDelete(this->rtpVideoTaskHandle);
    this->rtpVideoTaskHandle = NULL;
  }
  // Close Sockets
  if (this->rtspSocket >= 0) {
    close(this->rtspSocket);
    this->rtspSocket = -1;
  }
  if (this->videoRtpSocket >= 0) {
    close(this->videoRtpSocket);
    this->videoRtpSocket = -1;
  }
  if (this->audioRtpSocket >= 0) {
    close(this->audioRtpSocket);
    this->audioRtpSocket = -1;
  }
  if (this->subtitlesRtpSocket >= 0) {
    close(this->subtitlesRtpSocket);
    this->subtitlesRtpSocket = -1;
  }
  // Free Buffers
  if (this->rtspStreamBuffer) {
    free(this->rtspStreamBuffer);
  }

  ESP_LOGI(LOG_TAG, "RTSP server deinitialized.");
}

/**
 * @brief Reinitialize the RTSP server. 
 * 
 * @return true if reinitialization was successful, false otherwise.
 */
bool RTSPServer::reinit() {
  deinit();  // Deinitialize the RTSP server
  return init();  // Reinitialize the RTSP server
}

/** 
 * @brief Sends a TCP packet.
 * @param packet Pointer to the packet data.
 * @param packetSize Size of the packet data.
 * @param sock Socket to send the packet through. 
 */
// void RTSPServer::sendTcpPacket(const uint8_t* packet, size_t packetSize, int sock) {
//   if (xSemaphoreTake(sendTcpMutex, portMAX_DELAY) == pdTRUE) {
//     ssize_t sent = send(sock, packet, packetSize, 0);
//     if (sent < 0) {
//       int err = errno;
//       if (err != EPIPE && err != ECONNRESET && err != ENOTCONN && err != EBADF) {
//         // If client has closed the connection, these are expected errors else
//         ESP_LOGE(LOG_TAG, "Failed to send TCP packet, errno: %d", err);
//       }
//     }
//     xSemaphoreGive(sendTcpMutex);
//   } else {
//       ESP_LOGE(LOG_TAG, "Failed to acquire mutex");
//   }
// }

void RTSPServer::sendTcpPacket(const uint8_t* packet, size_t packetSize, int sock) {
  if (xSemaphoreTake(sendTcpMutex, portMAX_DELAY) == pdTRUE) {
    ssize_t sent = 0;
    while (sent < packetSize) {
      ssize_t result = send(sock, packet + sent, packetSize - sent, 0);
      if (result < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
          // Use select to wait for the socket to become writable
          fd_set write_fds;
          FD_ZERO(&write_fds);
          FD_SET(sock, &write_fds);
          struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; // 100ms
          int ret = select(sock + 1, NULL, &write_fds, NULL, &tv);
          if (ret <= 0) {
            // Select failed or timeout occurred
            ESP_LOGE(LOG_TAG, "Failed to send TCP packet, select timeout or error");
            break;
          }
          continue; // Retry sending
        } else if (err != EPIPE && err != ECONNRESET && err != ENOTCONN && err != EBADF) {
          // If the client has closed the connection, these are expected errors, else log error
          ESP_LOGE(LOG_TAG, "Failed to send TCP packet, errno: %d", err);
          break;
        } else {
          break; // Connection closed by the client
        }
      } else {
        sent += result;
      }
    }
    xSemaphoreGive(sendTcpMutex);
  } else {
    ESP_LOGE(LOG_TAG, "Failed to acquire mutex");
  }
}


/**
 * @brief Sends RTP subtitles.
 * 
 * @param data Pointer to the subtitles data.
 * @param len Length of the subtitles data.
 * @param sock Socket to use for sending.
 * @param clientIP Client IP address.
 * @param sendRtpPort RTP port to use for sending.
 * @param useTCP Indicates if TCP is used.
 */
void RTSPServer::sendRtpSubtitles(const char* data, size_t len, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP) {
  
  const int RtpHeaderSize = 12; // RTP header size
  int RtpPacketSize = len + RtpHeaderSize;

  uint8_t packet[512];
  memset(packet, 0x00, sizeof(packet));

  // If TCP, we need these first 4 bytes
  packet[0] = '$'; // Magic number 
  packet[1] = this->subtitlesCh; // Channel number for RTP (2 for subtitles)
  packet[2] = (RtpPacketSize >> 8) & 0xFF; // Packet length high byte 
  packet[3] = RtpPacketSize & 0xFF; // Packet length low byte
  
  // RTP header
  packet[4] = 0x80; // Version: 2, Padding: 0, Extension: 0, CSRC Count: 0
  packet[5] = 0x80 | 0x62; // Marker bit set and payload type 98
  packet[6] = (this->subtitlesSequenceNumber >> 8) & 0xFF; // Sequence Number (high byte)
  packet[7] = this->subtitlesSequenceNumber & 0xFF; // Sequence Number (low byte)
  packet[8] = (this->subtitlesTimestamp >> 24) & 0xFF; // Timestamp (high byte)
  packet[9] = (this->subtitlesTimestamp >> 16) & 0xFF; // Timestamp (next byte)
  packet[10] = (this->subtitlesTimestamp >> 8) & 0xFF; // Timestamp (next byte)
  packet[11] = this->subtitlesTimestamp & 0xFF; // Timestamp (low byte)
  packet[12] = (this->subtitlesSSRC >> 24) & 0xFF; // SSRC (high byte)
  packet[13] = (this->subtitlesSSRC >> 16) & 0xFF; // SSRC (next byte)
  packet[14] = (this->subtitlesSSRC >> 8) & 0xFF; // SSRC (next byte)
  packet[15] = this->subtitlesSSRC & 0xFF; // SSRC (low byte)

  int packetOffset = RtpHeaderSize + 4;

  // Copy SRT data to the packet
  memcpy(packet + packetOffset, data, len);
  packetOffset += len;

  // Send packet using TCP or UDP
  if (useTCP) {
    sendTcpPacket(packet, packetOffset, sock);
  } else {
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(sendRtpPort);
    inet_aton(clientIP.toString().c_str(), &client_addr.sin_addr);

    sendto(this->subtitlesRtpSocket, packet + 4, packetOffset - 4, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
  }
  this->subtitlesSequenceNumber++;
  this->subtitlesTimestamp += 1000; // Increment the timestamp
}

/**
 * @brief Sends RTP audio data.
 * 
 * @param data Pointer to the audio data.
 * @param len Length of the audio data.
 * @param sock Socket to use for sending.
 * @param clientIP Client IP address.
 * @param sendRtpPort RTP port to use for sending.
 * @param useTCP Indicates if TCP is used.
 */
void RTSPServer::sendRtpAudio(const int16_t* data, size_t len, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP) {
  const int RtpHeaderSize = 12; // RTP header size
  const int MAX_FRAGMENT_SIZE = 1024; // Adjust based on your requirements
  uint32_t audioLen = len;

  size_t fragmentOffset = 0;
  while (fragmentOffset < audioLen) {
    int fragmentLen = MAX_FRAGMENT_SIZE;
    if (fragmentLen + fragmentOffset > audioLen) {
      fragmentLen = audioLen - fragmentOffset;
    }

    int RtpPacketSize = fragmentLen + RtpHeaderSize;
    uint8_t packet[2048]; // Adjust the size based on MAX_FRAGMENT_SIZE
    memset(packet, 0x00, sizeof(packet));

    // If TCP, we need these first 4 bytes
    packet[0] = '$'; // Magic number 
    packet[1] = this->audioCh; // Channel number for RTP (1 for audio)
    packet[2] = (RtpPacketSize >> 8) & 0xFF; // Packet length high byte 
    packet[3] = RtpPacketSize & 0xFF; // Packet length low byte

    // RTP header
    packet[4] = 0x80; // Version: 2, Padding: 0, Extension: 0, CSRC Count: 0
    packet[5] = 0x61 | 0x80;  // Dynamic payload type (97) and marker bit
    packet[6] = (this->audioSequenceNumber >> 8) & 0xFF; // Sequence Number (high byte)
    packet[7] = this->audioSequenceNumber & 0xFF; // Sequence Number (low byte)
    packet[8] = (this->audioTimestamp >> 24) & 0xFF; // Timestamp (high byte)
    packet[9] = (this->audioTimestamp >> 16) & 0xFF; // Timestamp (next byte)
    packet[10] = (this->audioTimestamp >> 8) & 0xFF; // Timestamp (next byte)
    packet[11] = this->audioTimestamp & 0xFF; // Timestamp (low byte)
    packet[12] = (this->audioSSRC >> 24) & 0xFF; // SSRC (high byte)
    packet[13] = (this->audioSSRC >> 16) & 0xFF; // SSRC (next byte)
    packet[14] = (this->audioSSRC >> 8) & 0xFF; // SSRC (next byte)
    packet[15] = this->audioSSRC & 0xFF; // SSRC (low byte)

    int packetOffset = RtpHeaderSize + 4;

    // Convert audio data from little-endian to big-endian and copy to the packet
    for (size_t i = 0; i < fragmentLen / 2; i++) {
      packet[packetOffset++] = (data[fragmentOffset / 2 + i] >> 8) & 0xFF; // High byte
      packet[packetOffset++] = data[fragmentOffset / 2 + i] & 0xFF; // Low byte
    }

    // Send packet using TCP or UDP
    if (useTCP) {
      sendTcpPacket(packet, packetOffset, sock);
    } else {
      struct sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof(client_addr));
      client_addr.sin_family = AF_INET;
      client_addr.sin_port = htons(sendRtpPort);
      inet_aton(clientIP.toString().c_str(), &client_addr.sin_addr);

      int rtpSocket = isMulticast ? this->audioMulticastSocket : this->audioUnicastSocket;

      sendto(rtpSocket, packet + 4, packetOffset - 4, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
    }
    fragmentOffset += fragmentLen;
    this->audioSequenceNumber++;
    this->audioTimestamp += fragmentLen / 2; // Convert fragment length to number of samples
  }
}

/**
 * @brief Sends an RTP frame.
 * 
 * @param data Pointer to the frame data.
 * @param len Length of the frame data.
 * @param quality Quality of the frame.
 * @param width Width of the frame.
 * @param height Height of the frame.
 * @param sock Socket to use for sending.
 * @param clientIP Client IP address.
 * @param sendRtpPort RTP port to use for sending.
 * @param useTCP Indicates if TCP is used.
 */
void RTSPServer::sendRtpFrame(const uint8_t* data, size_t len, uint8_t quality, uint16_t width, uint16_t height, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP) {
  static uint32_t lastSendTime = millis(); // Track the last time a frame was sent
  uint32_t currentTime = millis(); // Get the current time in milliseconds

  // Calculate the actual time elapsed since the last frame was sent
  uint32_t actualElapsedTime = currentTime - lastSendTime;
  // Increment the timestamp based on the actual elapsed time
  this->videoTimestamp += (actualElapsedTime * 90000) / 1000;   // Convert milliseconds to 90kHz units

  // Work out the RTP sent FPS to use for subtitles
  this->rtpFrameCount++; 
  // Update FPS every second 
  if (currentTime - this->lastRtpFPSUpdateTime >= 1000) { 
    this->rtpFps = this->rtpFrameCount; // Store the current FPS 
    this->rtpFrameCount = 0; // Reset the frame count for the next second 
    this->lastRtpFPSUpdateTime = currentTime; // Update the last FPS update time 
  }

  const int RtpHeaderSize = 20;
  const int MAX_FRAGMENT_SIZE = 1438;
  uint32_t jpegLen = len;

  size_t fragmentOffset = 0;
  while (fragmentOffset < jpegLen) {
    int fragmentLen = MAX_FRAGMENT_SIZE;
    if (fragmentLen + fragmentOffset > jpegLen) {
      fragmentLen = jpegLen - fragmentOffset;
    }

    bool isLastFragment = (fragmentOffset + fragmentLen) == jpegLen;
    int RtpPacketSize = fragmentLen + RtpHeaderSize;

    uint8_t packet[2048];
    memset(packet, 0x00, sizeof(packet));

    // If TCP, we need these first 4 bytes
    packet[0] = '$'; // Magic number 
    packet[1] = this->videoCh; // Channel number for RTP (0 for video)
    packet[2] = (RtpPacketSize >> 8) & 0xFF; // Packet length high byte 
    packet[3] = RtpPacketSize & 0xFF; // Packet length low byte
    
    // RTP header
    packet[4] = 0x80;
    packet[5] = 0x1a | (isLastFragment ? 0x80 : 0x00);
    packet[6] = (this->videoSequenceNumber >> 8) & 0xFF;
    packet[7] = this->videoSequenceNumber & 0xFF;
    packet[8] = (this->videoTimestamp >> 24) & 0xFF;
    packet[9] = (this->videoTimestamp >> 16) & 0xFF;
    packet[10] = (this->videoTimestamp >> 8) & 0xFF;
    packet[11] = this->videoTimestamp & 0xFF;
    packet[12] = (this->videoSSRC >> 24) & 0xFF;
    packet[13] = (this->videoSSRC >> 16) & 0xFF;
    packet[14] = (this->videoSSRC >> 8) & 0xFF;
    packet[15] = this->videoSSRC & 0xFF;

    // JPEG RTP header
    packet[16] = 0x00;
    packet[17] = (fragmentOffset >> 16) & 0xFF;
    packet[18] = (fragmentOffset >> 8) & 0xFF;
    packet[19] = fragmentOffset & 0xFF;
    packet[20] = 0x00;
    packet[21] = quality;
    packet[22] = width / 8;
    packet[23] = height / 8;

    int packetOffset = 24;

    // Copy JPEG data to the packet
    memcpy(packet + packetOffset, data + fragmentOffset, fragmentLen);
    packetOffset += fragmentLen;

    // Send packet using TCP or UDP
    if (useTCP) {
      sendTcpPacket(packet, packetOffset, sock);
    } else {
      struct sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof(client_addr));
      client_addr.sin_family = AF_INET;
      client_addr.sin_port = htons(sendRtpPort);
      inet_aton(clientIP.toString().c_str(), &client_addr.sin_addr);

      sendto(this->videoRtpSocket, packet + 4, packetOffset - 4, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
    }
    fragmentOffset += fragmentLen;
    this->videoSequenceNumber++;
  }
  lastSendTime = currentTime;
}

/**
 * @brief Checks if the server is ready to send a frame.
 * 
 * @return true if ready, false otherwise.
 */
bool RTSPServer::readyToSendFrame() const {
  bool send = false;
  xSemaphoreTake(clientsMutex, portMAX_DELAY); // Take the mutex
  if (this->activeRTSPClients > 0 && this->rtpFrameSent) {
    send = true;
  }
  xSemaphoreGive(clientsMutex); // Release the mutex
  return send; 
}

/**
 * @brief Checks if the server is ready to send audio.
 * 
 * @return true if ready, false otherwise.
 */
bool RTSPServer::readyToSendAudio() const {
  bool send = false;
  xSemaphoreTake(clientsMutex, portMAX_DELAY); // Take the mutex
  if (this->activeRTSPClients > 0 && this->rtpAudioSent) {
    send = true;
  }
  xSemaphoreGive(clientsMutex); // Release the mutex
  return send; 
}

/**
 * @brief Checks if the server is ready to send subtitles.
 * 
 * @return true if ready, false otherwise.
 */
bool RTSPServer::readyToSendSubtitles() const {
  bool send = false;
  xSemaphoreTake(clientsMutex, portMAX_DELAY); // Take the mutex
  if (this->activeRTSPClients > 0 && this->rtpSubtitlesSent) {
    send = true;
  }
  xSemaphoreGive(clientsMutex); // Release the mutex
  return send; 
}

/**
 * @brief Starts a timer for subtitles.
 * @param userCallback Callback function to be called when the timer expires.
 */
void RTSPServer::startSubtitlesTimer(esp_timer_cb_t userCallback) { 
  const esp_timer_create_args_t timerConfig = { 
    .callback = userCallback, // User-defined callback function 
    .arg = nullptr, // Optional argument, can be set to NULL
    .dispatch_method = ESP_TIMER_TASK, // Dispatch method, set to default
    .name = "periodic_timer" ,
    .skip_unhandled_events = false // Optional, can be set to false
    }; 
    // Create the timer 
    esp_timer_create(&timerConfig, &sendSubtitlesTimer); 
    // Start the timer with the specified period (in microseconds) 
    esp_timer_start_periodic(sendSubtitlesTimer, 1000000); 
}

/**
 * @brief Wrapper for the RTP video task.
 * 
 * @param pvParameters Task parameters.
 */
void RTSPServer::rtpVideoTaskWrapper(void* pvParameters) {
  RTSPServer* server = static_cast<RTSPServer*>(pvParameters);
  server->rtpVideoTask();
}

/**
 * @brief Task for handling RTP video streaming.
 */
void RTSPServer::rtpVideoTask() {
  while (true) {
    //if (xSemaphoreTake(this->rtspFrameSemaphore, portMAX_DELAY) == pdTRUE) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (const auto& sessionPair : this->sessions) {
      const RTSP_Session& session = sessionPair.second; 
      if (session.isPlaying) {
        if (session.isMulticast) {
          this->sendRtpFrame(this->rtspStreamBuffer, this->rtspStreamBufferSize, this->vQuality, this->vWidth, this->vHeight, session.sock, this->rtpIp, this->rtpVideoPort, false);
        } else {
          this->sendRtpFrame(this->rtspStreamBuffer, this->rtspStreamBufferSize, this->vQuality, this->vWidth, this->vHeight, session.sock, session.clientIP, session.cVideoPort, session.isTCP);
        }
      }
    }
    this->rtspStreamBufferSize = 0;
    this->rtpFrameSent = true;
    //}
  }
  vTaskDelete(NULL);
}

/**
 * @brief Sends an RTSP frame.
 * 
 * @param data Pointer to the frame data.
 * @param len Length of the frame data.
 * @param quality Quality of the frame.
 * @param width Width of the frame.
 * @param height Height of the frame.
 */
void RTSPServer::sendRTSPFrame(const uint8_t* data, size_t len, int quality, int width, int height) {
  this->rtpFrameSent = false;
#ifdef RTSP_VIDEO_NONBLOCK
  this->vQuality = quality;
  this->vWidth = width;
  this->vHeight = height;
  if (!this->rtspStreamBufferSize && this->rtspStreamBuffer != NULL) {
    memcpy(this->rtspStreamBuffer, data, len);
    this->rtspStreamBufferSize = len;
    xTaskNotifyGive(rtpVideoTaskHandle); // Signal frame ready for RTSP
    //xSemaphoreGive(this->rtspFrameSemaphore); // Signal frame ready for RTSP
  }
#else
  for (const auto& sessionPair : this->sessions) {
    const RTSP_Session& session = sessionPair.second; 
    if (session.isPlaying) {
      if (session.isMulticast) {
        sendRtpFrame(data, len, quality, width, height, session.sock, this->rtpIp, this->rtpVideoPort, false);
      } else {
        sendRtpFrame(data, len, quality, width, height, session.sock, session.clientIP, session.cVideoPort, session.isTCP);
      }
    }
  }
  this->rtpFrameSent = true;
#endif
}

/**
 * @brief Sends RTSP audio data.
 * 
 * @param data Pointer to the audio data.
 * @param len Length of the audio data.
 */
void RTSPServer::sendRTSPAudio(int16_t* data, size_t len) {
  this->rtpAudioSent = false;
  for (const auto& sessionPair : this->sessions) {
    const RTSP_Session& session = sessionPair.second; 
    if (session.isPlaying) {
      if (session.isMulticast) {
        this->sendRtpAudio(data, len, session.sock, this->rtpIp, this->rtpAudioPort, false);
      } else {
        this->sendRtpAudio(data, len, session.sock, session.clientIP, session.cAudioPort, session.isTCP);
      }
    }
  }
  this->rtpAudioSent = true;
}

/**
 * @brief Sends RTSP subtitles.
 * 
 * @param data Pointer to the subtitles data.
 * @param len Length of the subtitles data.
 */
void RTSPServer::sendRTSPSubtitles(char* data, size_t len) {
  this->rtpSubtitlesSent = false;
  for (const auto& sessionPair : this->sessions) {
    const RTSP_Session& session = sessionPair.second; 
    if (session.isPlaying) {
      if (session.isMulticast) {
        this->sendRtpSubtitles(data, len, session.sock, this->rtpIp, this->rtpSubtitlesPort, false);
      } else {
        this->sendRtpSubtitles(data, len, session.sock, session.clientIP, session.cSrtPort, session.isTCP);
      }
    }
  }
  this->rtpSubtitlesSent = true;
}

/**
 * @brief Sets up RTP streaming.
 * 
 * @param rtpSocket Reference to the RTP socket.
 * @param isMulticast Indicates if multicast is used.
 * @param rtpPort The RTP port to use.
 * @param rtpIp The RTP IP address.
 */
void RTSPServer::setupRTP(int& rtpSocket, bool isMulticast, uint16_t rtpPort, IPAddress rtpIp) {
  rtpSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (rtpSocket < 0) {
    ESP_LOGE(LOG_TAG, "Failed to create RTP socket");
    return;
  }
  struct sockaddr_in rtpAddr;
  memset(&rtpAddr, 0, sizeof(rtpAddr));
  rtpAddr.sin_family = AF_INET;
  rtpAddr.sin_port = htons(rtpPort);
  if (isMulticast) {
    inet_aton(rtpIp.toString().c_str(), &rtpAddr.sin_addr);
    setsockopt(rtpSocket, IPPROTO_IP, IP_MULTICAST_TTL, &this->rtpTTL, sizeof(this->rtpTTL));
  } else {
    rtpAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(rtpSocket, (struct sockaddr *)&rtpAddr, sizeof(rtpAddr)) < 0) {
      ESP_LOGE(LOG_TAG, "Failed to bind RTP socket on port %d", rtpPort);
      return;
    }
  }
}

void RTSPServer::setMaxClients(uint8_t newMaxClients) {
  if (xSemaphoreTake(maxClientsMutex, portMAX_DELAY) == pdTRUE) {
    if (newMaxClients <= MAX_CLIENTS) {
      maxClients = newMaxClients;
      ESP_LOGI(LOG_TAG, "Max clients updated to: %d", maxClients);
    } else {
      ESP_LOGW(LOG_TAG, "Requested max clients (%d) exceeds the hardcoded limit (%d). Max clients set to %d.", newMaxClients, MAX_CLIENTS, MAX_CLIENTS);
      maxClients = MAX_CLIENTS;
    }
    xSemaphoreGive(maxClientsMutex);
  } else {
    ESP_LOGE(LOG_TAG, "Failed to acquire maxClients mutex");
  }
}

uint8_t RTSPServer::getMaxClients() {
  uint8_t clients = 0;
  if (xSemaphoreTake(maxClientsMutex, portMAX_DELAY) == pdTRUE) {
    clients = maxClients;
    xSemaphoreGive(maxClientsMutex);
  } else {
    ESP_LOGE(LOG_TAG, "Failed to acquire maxClients mutex");
  }
  return clients;
}

/**
 * @brief Get the count of active clients.
 */
uint8_t RTSPServer::getActiveClients() {
  xSemaphoreTake(clientsMutex, portMAX_DELAY); // Take the mutex
  uint8_t aC = this->activeRTSPClients;
  xSemaphoreGive(clientsMutex); // Give the mutex back
  return aC;
}

/**
 * @brief Increments the count of active clients.
 */
void RTSPServer::incrementActiveClients() {
  xSemaphoreTake(clientsMutex, portMAX_DELAY); // Take the mutex
  this->activeRTSPClients++;
  xSemaphoreGive(clientsMutex); // Give the mutex back
}

/**
 * @brief Decrements the count of active clients.
 */
void RTSPServer::decrementActiveClients() {
  xSemaphoreTake(clientsMutex, portMAX_DELAY); // Take the mutex
  if (this->activeRTSPClients > 0) {
    this->activeRTSPClients--;
  } else {
    ESP_LOGW(LOG_TAG, "Attempted to decrement activeRTSPClients below 0");
  }
  xSemaphoreGive(clientsMutex); // Give the mutex back
}


/**
 * @brief Captures the CSeq from an RTSP request.
 * 
 * @param request The RTSP request.
 * @return The CSeq value.
 */
int RTSPServer::captureCSeq(char* request) {
  char* cseqStr = strstr(request, "CSeq: ");
  if (cseqStr == NULL) {
    return -1;
  }
  cseqStr += 6;
  char* endOfLine = strchr(cseqStr, '\n');
  if (endOfLine) {
    *endOfLine = 0;  // Temporarily null-terminate the CSeq line
  }
  int cseq = atoi(cseqStr);
  if (endOfLine) {
    *endOfLine = '\n';  // Restore the newline character
  }
  return cseq;
}

/**
 * @brief Generates a new session ID.
 * 
 * @return The generated session ID.
 */
uint32_t RTSPServer::generateSessionID() {
  return esp_random();
}

/**
 * @brief Extracts the session ID from an RTSP request.
 * 
 * @param request The RTSP request.
 * @return The extracted session ID.
 */
uint32_t RTSPServer::extractSessionID(char* request) {
  char* sessionStr = strstr(request, "Session: ");
  if (sessionStr == NULL) {
    return 0;
  }
  sessionStr += 9;
  char* endOfLine = strchr(sessionStr, '\n');
  if (endOfLine) {
    *endOfLine = 0; // Temporarily null-terminate the session line
  }

  // Trim any leading/trailing whitespace
  while (isspace(*sessionStr)) sessionStr++;
  char* end = sessionStr + strlen(sessionStr) - 1;
  while (end > sessionStr && isspace(*end)) end--;
  *(end + 1) = 0;

  uint32_t sessionID = strtoul(sessionStr, NULL, 10);

  // Restore the newline character
  if (endOfLine) {
    *endOfLine = '\n';
  }

  return sessionID;
}

/**
 * @brief Generates the Date header for RTSP responses.
 * 
 * @return The Date header as a string.
 */
const char* RTSPServer::dateHeader() {
  static char buffer[50];
  time_t now = time(NULL);
  strftime(buffer, sizeof(buffer), "Date: %a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
  return buffer;
}

/**
 * @brief Handles the OPTIONS RTSP request.
 * 
 * @param request The RTSP request.
 * @param session The RTSP session.
 */
void RTSPServer::handleOptions(char* request, RTSP_Session& session) {
  char* urlStart = strstr(request, "rtsp://");
  if (urlStart) {
    char* pathStart = strchr(urlStart + 7, '/');
    char* pathEnd = strchr(pathStart, ' ');
    if (pathStart && pathEnd) {
      *pathEnd = 0; // Null-terminate the path
      // Path can be processed here if needed
    }
  }
  char response[512];
  snprintf(response, sizeof(response), 
           "RTSP/1.0 200 OK\r\n"
           "CSeq: %d\r\n"
           "%s\r\n"
           "Public: DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN\r\n\r\n", 
           session.cseq, 
           dateHeader());
  write(session.sock, response, strlen(response));
}

/**
 * @brief Handles the DESCRIBE RTSP request.
 * 
 * @param session The RTSP session.
 */
void RTSPServer::handleDescribe(const RTSP_Session& session) {
  char sdpDescription[512];
  int sdpLen = snprintf(sdpDescription, sizeof(sdpDescription),
                        "v=0\r\n"
                        "o=- %ld 1 IN IP4 %s\r\n"
                        "s=\r\n"
                        "c=IN IP4 0.0.0.0\r\n"
                        "t=0 0\r\n"
                        "a=control:*\r\n",
                        session.sessionID, WiFi.localIP().toString().c_str());

  if (isVideo) {
    sdpLen += snprintf(sdpDescription + sdpLen, sizeof(sdpDescription) - sdpLen,
                       "m=video 0 RTP/AVP 26\r\n"
                       "a=control:video\r\n");
  }

  const char* mediaCondition = "sendrecv"; 
  // if (haveMic && haveAmp) mediaCondition = "sendrecv"; 
  // else if (haveMic) mediaCondition = "sendonly"; 
  // else if (haveAmp) mediaCondition = "recvonly"; 
  // else mediaCondition = "inactive"; 

  if (isAudio) {
    sdpLen += snprintf(sdpDescription + sdpLen, sizeof(sdpDescription) - sdpLen,
                       "m=audio 0 RTP/AVP 97\r\n"
                       "a=rtpmap:97 L16/%lu/1\r\n"
                       "a=control:audio\r\n"
                       "a=%s\r\n", sampleRate, mediaCondition);
  }

  if (isSubtitles) {
    sdpLen += snprintf(sdpDescription + sdpLen, sizeof(sdpDescription) - sdpLen,
                       "m=text 0 RTP/AVP 98\r\n"
                       "a=rtpmap:98 t140/1000\r\n"
                       "a=control:subtitles\r\n");
  }

  char response[1024];
  int responseLen = snprintf(response, sizeof(response),
                             "RTSP/1.0 200 OK\r\nCSeq: %d\r\n%s\r\nContent-Base: rtsp://%s:554/\r\nContent-Type: application/sdp\r\nContent-Length: %d\r\n\r\n"
                             "%s",
                             session.cseq, dateHeader(), WiFi.localIP().toString().c_str(), sdpLen, sdpDescription);
  write(session.sock, response, responseLen);
}

/**
 * @brief Handles the SETUP RTSP request.
 * 
 * @param request The RTSP request.
 * @param session The RTSP session.
 */
void RTSPServer::handleSetup(char* request, RTSP_Session& session) {
  session.isMulticast = strstr(request, "multicast") != NULL;
  session.isTCP = strstr(request, "RTP/AVP/TCP") != NULL;

  // Check if the first client is multicast and reject unicast or TCP connections
  if (firstClientConnected && firstClientIsMulticast && (!session.isMulticast || session.isTCP)) {
    ESP_LOGW(LOG_TAG, "Rejecting unicast or TCP connection because the first client is multicast");
    char* response = (char*)malloc(512);
    if (response == NULL) {
      ESP_LOGE(LOG_TAG, "Failed to allocate memory");
      return;
    }
    snprintf(response, 512,
             "RTSP/1.0 461 Unsupported Transport\r\n"
             "CSeq: %d\r\n"
             "%s\r\n\r\n",
             session.cseq, dateHeader());
    write(session.sock, response, strlen(response));
    free(response);
    return;
  }

  // Track the first client's connection type
  if (!firstClientConnected) {
    firstClientConnected = true;
    firstClientIsMulticast = session.isMulticast;
    firstClientIsTCP = session.isTCP;

    // Set max clients based on the first client's connection type
    if (firstClientIsMulticast) {
      setMaxClients(this->maxRTSPClients);
    } else {
      setMaxClients(1); // Only allow 1 client for unicast
    }
  }

  bool setVideo = strstr(request, "video") != NULL;
  bool setAudio = strstr(request, "audio") != NULL;
  bool setSubtitles = strstr(request, "subtitles") != NULL;
  uint16_t clientPort = 0;
  uint16_t serverPort = 0;
  uint8_t rtpChannel = 0;

  // Extract client port or RTP channel based on transport method
  if (session.isTCP) {
    char* interleaveStart = strstr(request, "interleaved=");
    if (interleaveStart) {
      interleaveStart += 12;
      char* interleaveEnd = strchr(interleaveStart, '-');
      if (interleaveStart && interleaveEnd) {
        *interleaveEnd = 0;
        rtpChannel = atoi(interleaveStart);
        ESP_LOGD(LOG_TAG, "Extracted RTP channel: %d", rtpChannel);
      } else {
        ESP_LOGE(LOG_TAG, "Failed to find interleave end");
      }
    } else {
      ESP_LOGE(LOG_TAG, "Failed to find interleaved=");
    }
  } else if (!session.isMulticast) {
    char* rtpPortStart = strstr(request, "client_port=");
    if (rtpPortStart) {
      rtpPortStart += 12;
      char* rtpPortEnd = strchr(rtpPortStart, '-');
      if (rtpPortStart && rtpPortEnd) {
        *rtpPortEnd = 0;
        clientPort = atoi(rtpPortStart);
        ESP_LOGD(LOG_TAG, "Extracted client port: %d", clientPort);
      } else {
        ESP_LOGE(LOG_TAG, "Failed to find client port end");
      }
    } else {
      ESP_LOGE(LOG_TAG, "Failed to find client_port=");
    }
  }

  // Setup video, audio, or subtitles based on the request
  if (setVideo) {
    session.cVideoPort = clientPort;
    serverPort = this->rtpVideoPort;
    this->videoCh = rtpChannel;
    if(!session.isTCP)this->setupRTP(this->videoRtpSocket, session.isMulticast, serverPort, this->rtpIp);
  }
  if (setAudio) {
    session.cAudioPort = clientPort;
    serverPort = this->rtpAudioPort;
    this->audioCh = rtpChannel;
    if(!session.isTCP)this->setupRTP(this->audioRtpSocket, session.isMulticast, serverPort, this->rtpIp);
  }
  if (setSubtitles) {
    session.cSrtPort = clientPort;
    serverPort = this->rtpSubtitlesPort;
    this->subtitlesCh = rtpChannel;
    if(!session.isTCP) this->setupRTP(this->subtitlesRtpSocket, session.isMulticast, serverPort, this->rtpIp);
  }

#ifdef RTSP_VIDEO_NONBLOCK
  if (setVideo && this->rtpVideoTaskHandle == NULL) {
    xTaskCreate(rtpVideoTaskWrapper, "rtpVideoTask", RTP_STACK_SIZE, this, RTP_PRI, &this->rtpVideoTaskHandle);
  }
  if (this->rtspStreamBuffer == NULL && psramFound()) {
    this->rtspStreamBuffer = (uint8_t*)ps_malloc(MAX_RTSP_BUFFER);
  }
#endif

  char* response = (char*)malloc(512);
  if (response == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to allocate memory");
    return;
  }

  // Formulate the response based on transport method
  if (session.isTCP) {
    snprintf(response, 512,
             "RTSP/1.0 200 OK\r\n"
             "CSeq: %d\r\n"
             "%s\r\n"
             "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n"
             "Session: %lu\r\n\r\n",
             session.cseq, dateHeader(), rtpChannel, rtpChannel + 1, session.sessionID);
  } else if (session.isMulticast) {
    snprintf(response, 512,
             "RTSP/1.0 200 OK\r\nCSeq: %d\r\n%s\r\nTransport: RTP/AVP;multicast;destination=%s;port=%d-%d;ttl=%d\r\nSession: %lu\r\n\r\n",
             session.cseq, dateHeader(), this->rtpIp.toString().c_str(), serverPort, serverPort + 1, this->rtpTTL, session.sessionID);
  } else {
    snprintf(response, 512,
             "RTSP/1.0 200 OK\r\nCSeq: %d\r\n%s\r\nTransport: RTP/AVP;unicast;destination=127.0.0.1;source=127.0.0.1;client_port=%d-%d;server_port=%d-%d\r\nSession: %lu\r\n\r\n",
             session.cseq, dateHeader(), clientPort, clientPort + 1, serverPort, serverPort + 1, session.sessionID);
  }

  write(session.sock, response, strlen(response));
  free(response);
  this->sessions[session.sessionID] = session;
}

/**
 * @brief Handles the PLAY RTSP request.
 * 
 * @param session The RTSP session.
 */
void RTSPServer::handlePlay(RTSP_Session& session) {
  this->incrementActiveClients();
  session.isPlaying = true;
  this->sessions[session.sessionID] = session;

  char response[256];
  snprintf(response, sizeof(response),
           "RTSP/1.0 200 OK\r\n"
           "CSeq: %d\r\n"
           "%s\r\n"
           "Range: npt=0.000-\r\n"
           "Session: %lu\r\n"
           "RTP-Info: url=rtsp://127.0.0.1:554/\r\n\r\n",
           session.cseq,
           dateHeader(),
           session.sessionID);

  write(session.sock, response, strlen(response));
}

/**
 * @brief Handles the PAUSE RTSP request.
 * 
 * @param session The RTSP session.
 */
void RTSPServer::handlePause(RTSP_Session& session) {
  if (this->sessions.find(session.sessionID) != sessions.end()) {
    this->sessions[session.sessionID].isPlaying = false;
    char response[128];
    int len = snprintf(response, sizeof(response),
                       "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %lu\r\n\r\n",
                       session.cseq, session.sessionID);
    write(session.sock, response, len);
    this->decrementActiveClients();
    ESP_LOGD(LOG_TAG, "Session %u is now paused.", session.sessionID);
  } else {
    ESP_LOGE(LOG_TAG, "Session ID %u not found for PAUSE request.", session.sessionID);
  }
}

/**
 * @brief Handles the TEARDOWN RTSP request.
 * 
 * @param session The RTSP session.
 */
void RTSPServer::handleTeardown(RTSP_Session& session) {
  if (this->sessions.find(session.sessionID) != sessions.end()) {
    this->sessions.erase(session.sessionID);

    if (this->videoRtpSocket != -1) { 
      close(this->videoRtpSocket); 
      this->videoRtpSocket = -1; 
    } 
    if (this->audioRtpSocket != -1) { 
      close(this->audioRtpSocket); 
      this->audioRtpSocket = -1; 
    } 
    if (this->subtitlesRtpSocket != -1) { 
      close(this->subtitlesRtpSocket); 
      this->subtitlesRtpSocket = -1; 
    }

    char response[128];
    int len = snprintf(response, sizeof(response),
                       "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %lu\r\n\r\n",
                       session.cseq, session.sessionID);
    write(session.sock, response, len);
    this->decrementActiveClients();
    // Check if all clients are disconnected 
    if (this->getActiveClients() == 0) { 
      ESP_LOGD(LOG_TAG, "All clients disconnected. Resetting firstClientConnected flag."); 
      this->firstClientConnected = false; 
      this->firstClientIsMulticast = false; 
      this->firstClientIsTCP = false; 
    }
    ESP_LOGD(LOG_TAG, "RTSP Session %u has been torn down.", session.sessionID);
  } else {
    ESP_LOGE(LOG_TAG, "Session ID %u not found for TEARDOWN request.", session.sessionID);
  }
}

/**
 * @brief Handles incoming RTSP requests.
 * 
 * @param sock The socket file descriptor.
 * @param clientAddr The client address.
 * @return true if the request was handled successfully, false otherwise.
 */
bool RTSPServer::handleRTSPRequest(int sock, struct sockaddr_in clientAddr) {
  // Allocate buffer dynamically using ps_malloc
  const size_t bufferSize = 8092; // Define the buffer size
  char *buffer = (char *)ps_malloc(bufferSize);
  if (!buffer) {
    ESP_LOGE(LOG_TAG, "Failed to allocate buffer with ps_malloc");
    return false;
  }

  int totalLen = 0;
  int len = 0;
  int sessSock = sock;

  // Read data from socket until end of RTSP header or buffer limit is reached
  while ((len = recv(sock, buffer + totalLen, bufferSize - totalLen - 1, 0)) > 0) {
    totalLen += len;
    if (strstr(buffer, "\r\n\r\n")) {
      break;
    }
    if (totalLen >= bufferSize) { // Adjusted for null-terminator
      ESP_LOGE(LOG_TAG, "Request too large for buffer. Total length: %d", totalLen);
      free(buffer); // Free allocated memory
      return false;
    }
  }
  ESP_LOGD(LOG_TAG, "Request total length: %d", totalLen);

  if (totalLen <= 0) {
    int err = errno;
    free(buffer); // Free allocated memory
    if (err == EWOULDBLOCK || err == EAGAIN) {
      return true;
    } else if (err == ECONNRESET || err == ENOTCONN) {
      // Handle teardown when connection is reset or not connected based on client IP
      ESP_LOGD(LOG_TAG, "HandleTeardown");
      IPAddress clientIP(clientAddr.sin_addr.s_addr);
      for (auto& sess : sessions) {
        if (sess.second.clientIP == clientIP) {
          this->handleTeardown(sess.second);
          break;
        }
      }
      return false;
    } else {
      ESP_LOGE(LOG_TAG, "Error reading from socket, error: %d", err);
      return false;
    }
  }

  // Check to see if RTCP packet and ignore for now...
  buffer[totalLen] = 0; // Null-terminate the buffer
  if (buffer[0] == '$') {
    free(buffer); // Free allocated memory
    return true; 
  }

  uint8_t firstByte = buffer[0]; 
  uint8_t version = (firstByte >> 6) & 0x03;
  if (version == 2) { 
    uint8_t payloadType = buffer[1] & 0x7F;
    if (payloadType >= 200 && payloadType <= 204) {
      free(buffer); // Free allocated memory
      return true;
    }
    free(buffer); // Free allocated memory
    return true;
  }

  int cseq = captureCSeq(buffer);
  if (cseq == -1) {
    ESP_LOGE(LOG_TAG, "CSeq not found in request");
    write(sock, "RTSP/1.0 400 Bad Request\r\n\r\n", 29);
    free(buffer); // Free allocated memory
    return true;
  }

  RTSP_Session session;
  bool sessionExists = false;
  uint32_t sessionID = 0;
  IPAddress clientIP = IPAddress(clientAddr.sin_addr.s_addr);

  // Extract session ID from the request
  char* sessionIDStr = strstr(buffer, "Session: ");
  if (sessionIDStr) {
    sessionIDStr += 9;
    char* sessionIDEnd = strstr(sessionIDStr, "\r\n");
    if (sessionIDEnd) {
      *sessionIDEnd = 0;
      sessionID = strtoul(sessionIDStr, NULL, 10);
      *sessionIDEnd = '\r'; // Restore the character
      if (sessions.find(sessionID) != sessions.end()) {
        session = sessions[sessionID]; 
        sessionExists = true;
        session.cseq = cseq;
        sessions[sessionID] = session;
      }
    }
  }

  if (!sessionExists) {
    for (const auto& sess : sessions) {
      if (sess.second.clientIP == clientIP) {
        session = sess.second;
        sessionExists = true;
        session.cseq = cseq;
        sessions[session.sessionID] = session;
        break;
      }
    }
  }

  if (!sessionExists) {
    session = {
      esp_random(),
      IPAddress(clientAddr.sin_addr.s_addr),
      0,
      0,
      0,
      sessSock,
      cseq,
      false,
      false,
      false
    };
    sessions[session.sessionID] = session;
    ESP_LOGI(LOG_TAG, "Created new session with ID: %lu", session.sessionID);
  }

  // Handle different RTSP methods
  if (strncmp(buffer, "OPTIONS", 7) == 0) {
    ESP_LOGD(LOG_TAG, "HandleOptions");
    this->handleOptions(buffer, session);
  } else if (strncmp(buffer, "DESCRIBE", 8) == 0) {
    ESP_LOGD(LOG_TAG, "HandleDescribe");
    this->handleDescribe(session);
  } else if (strncmp(buffer, "SETUP", 5) == 0) {
    ESP_LOGD(LOG_TAG, "HandleSetup");
    this->handleSetup(buffer, session);
  } else if (strncmp(buffer, "PLAY", 4) == 0) {
    ESP_LOGD(LOG_TAG, "HandlePlay");
    this->handlePlay(session);
  } else if (strncmp(buffer, "TEARDOWN", 8) == 0) {
    ESP_LOGD(LOG_TAG, "HandleTeardown");
    this->handleTeardown(session);
    free(buffer); // Free allocated memory
    return false;
  } else if (strncmp(buffer, "PAUSE", 5) == 0) {
    ESP_LOGD(LOG_TAG, "HandlePause");
    this->handlePause(session);
  } else {
    ESP_LOGW(LOG_TAG, "Unknown RTSP method: %s", buffer);
  }

  free(buffer); // Free allocated memory
  return true;
}

/**
 * @brief Sets a socket to non-blocking mode.
 * 
 * This function sets the given socket to non-blocking mode, allowing it to
 * perform operations that would normally block, without blocking.
 * 
 * @param sock The socket file descriptor.
 */
bool RTSPServer::setNonBlocking(int sock) { 
  int flags = fcntl(sock, F_GETFL, 0); 
  if (flags == -1) { 
    ESP_LOGE(LOG_TAG, "Failed to get socket flags"); 
    return false; 
  } 
  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) { 
    ESP_LOGE(LOG_TAG, "Failed to set socket to non-blocking mode"); 
    return false;
  } 
  ESP_LOGI(LOG_TAG, "Socket set to non-blocking mode");
  return true;
}

/**
 * @brief Prepares the RTSP server for streaming.
 * 
 * @return true if the preparation is successful, false otherwise.
 */
bool RTSPServer::prepRTSP() {
  uint64_t mac = ESP.getEfuseMac();
  this->videoSSRC = static_cast<uint32_t>(mac & 0xFFFFFFFF);
  this->audioSSRC = static_cast<uint32_t>((mac >> 32) & 0xFFFFFFFF);
  this->subtitlesSSRC = static_cast<uint32_t>((mac >> 48) & 0xFFFFFFFF);

  // Create RTSP socket
  this->rtspSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (this->rtspSocket < 0) {
    ESP_LOGE(LOG_TAG, "Failed to create RTSP socket.");
    return false;
  }

  // Set RTSP socket to non-blocking mode
  if (!setNonBlocking(this->rtspSocket)) {
    ESP_LOGE(LOG_TAG, "Failed to set RTSP socket to non-blocking mode.");
    close(this->rtspSocket);
    return false;
  }

  // Configure server address
  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(this->rtspPort);

  // Bind RTSP socket
  if (bind(this->rtspSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    ESP_LOGE(LOG_TAG, "Failed to bind RTSP socket: %d", this->rtspSocket);
    close(this->rtspSocket);
    return false;
  }
  
  // Listen on RTSP socket
  if (listen(this->rtspSocket, 5) < 0) {
    ESP_LOGE(LOG_TAG, "Failed to listen on RTSP socket.");
    close(this->rtspSocket);
    return false;
  }

  // Create RTSP task
  if (this->rtspTaskHandle == NULL) {
    if (xTaskCreate(rtspTaskWrapper, "rtspTask", RTSP_STACK_SIZE, this, RTSP_PRI, &this->rtspTaskHandle) != pdPASS) {
      ESP_LOGE(LOG_TAG, "Failed to create RTSP task.");
      close(this->rtspSocket);
      return false;
    }
  }

  ESP_LOGI(LOG_TAG, "RTSP server setup completed, listening on port: %d", this->rtspPort);
  return true;
}

/**
 * @brief Wrapper for the RTSP task.
 * 
 * @param pvParameters Task parameters.
 */
void RTSPServer::rtspTaskWrapper(void* pvParameters) {
  RTSPServer* server = static_cast<RTSPServer*>(pvParameters);
  server->rtspTask();
}

/**
 * @brief Task for handling RTSP requests.
 */
void RTSPServer::rtspTask() {
  struct sockaddr_in clientAddr;
  socklen_t addr_len = sizeof(clientAddr);
  fd_set read_fds;
  int client_sockets[MAX_CLIENTS] = {0};
  int max_sd, activity, client_sock;

  ESP_LOGI(LOG_TAG, "RTSP Server listening on port %d", this->rtspPort);

  while (true) {
    // Clear and set the socket set
    FD_ZERO(&read_fds);
    FD_SET(this->rtspSocket, &read_fds);
    max_sd = this->rtspSocket;

    uint8_t currentMaxClients = getMaxClients(); // Get the current max clients value
    uint8_t activeClientsCount = 0; // Track the number of active clients
    
    // Add client sockets to set
    for (int i = 0; i < currentMaxClients; i++) {
      int sd = client_sockets[i];
      if (sd > 0) FD_SET(sd, &read_fds);
      if (sd > max_sd) max_sd = sd;
    }

    // Wait for activity on sockets
    activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

    if (activity < 0 && errno != EINTR) {
      ESP_LOGE(LOG_TAG, "Select error");
      continue;
    }

    // Handle new connection
    if (FD_ISSET(this->rtspSocket, &read_fds) && activeClientsCount < currentMaxClients) {
      client_sock = accept(this->rtspSocket, (struct sockaddr *)&clientAddr, &addr_len);
      if (client_sock < 0) {
        ESP_LOGE(LOG_TAG, "Accept error");
        continue;
      }

      ESP_LOGI(LOG_TAG, "New client connected");

      // Add new client socket to array
      for (int i = 0; i < currentMaxClients; i++) {
        if (client_sockets[i] == 0) {
          client_sockets[i] = client_sock;
          activeClientsCount++;
          ESP_LOGI(LOG_TAG, "Added to list of sockets as %d", i);
          break;
        }
      }
    }

    // Handle client sockets
    for (int i = 0; i < currentMaxClients; i++) {
      int sd = client_sockets[i];

      if (FD_ISSET(sd, &read_fds)) {
        // Handle RTSP request
        bool keepConnection = handleRTSPRequest(sd, clientAddr);
        if (!keepConnection) {
          close(sd);
          client_sockets[i] = 0;
          activeClientsCount--;
        }
      }
    }
  }
}