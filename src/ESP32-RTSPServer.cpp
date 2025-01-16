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
    videoUnicastSocket(-1),
    audioUnicastSocket(-1), 
    subtitlesUnicastSocket(-1),
    videoMulticastSocket(-1),
    audioMulticastSocket(-1),
    subtitlesMulticastSocket(-1),
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
    isPlaying(false),
    firstClientConnected(false),
    firstClientIsMulticast(false),
    firstClientIsTCP(false)
{
    isPlayingMutex = xSemaphoreCreateMutex(); // Initialize the mutex
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
  vSemaphoreDelete(this->isPlayingMutex);
  vSemaphoreDelete(this->sendTcpMutex);
  vSemaphoreDelete(this->maxClientsMutex);
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
  
  closeSockets();
  
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






void RTSPServer::closeSockets() {
  if (videoUnicastSocket != -1) {
    close(videoUnicastSocket);
    videoUnicastSocket = -1;
  }
  if (audioUnicastSocket != -1) {
    close(audioUnicastSocket);
    audioUnicastSocket = -1;
  }
  if (subtitlesUnicastSocket != -1) {
    close(subtitlesUnicastSocket);
    subtitlesUnicastSocket = -1;
  }
  if (videoMulticastSocket != -1) {
    close(videoMulticastSocket);
    videoMulticastSocket = -1;
  }
  if (audioMulticastSocket != -1) {
    close(audioMulticastSocket);
    audioMulticastSocket = -1;
  }
  if (subtitlesMulticastSocket != -1) {
    close(subtitlesMulticastSocket);
    subtitlesMulticastSocket = -1;
  }
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
    if (FD_ISSET(this->rtspSocket, &read_fds)) {
      if (getActiveRTSPClients() >= currentMaxClients) {
        client_sock = accept(this->rtspSocket, (struct sockaddr *)&clientAddr, &addr_len);
        if (client_sock < 0) {
          ESP_LOGE(LOG_TAG, "Accept error");
          continue;
        }

        const char* response = "RTSP/1.0 503 Service Unavailable\r\n\r\n";
        write(client_sock, response, strlen(response));
        close(client_sock);
        ESP_LOGE(LOG_TAG, "Max clients reached. Sent 503 error to new client.");
        continue;
      }

      client_sock = accept(this->rtspSocket, (struct sockaddr *)&clientAddr, &addr_len);
      if (client_sock < 0) {
        ESP_LOGE(LOG_TAG, "Accept error");
        continue;
      }

      ESP_LOGI(LOG_TAG, "New client connected");

      // Create a new session for the new client
      RTSP_Session session = {
        esp_random(),
        IPAddress(clientAddr.sin_addr.s_addr),
        0,
        0,
        0,
        client_sock,
        0,
        false,
        false,
        false
      };
      sessions[session.sessionID] = session;

      // Add new client socket to array
      for (int i = 0; i < currentMaxClients; i++) {
        if (client_sockets[i] == 0) {
          client_sockets[i] = client_sock;
          incrementActiveRTSPClients();
          ESP_LOGI(LOG_TAG, "Added to list of sockets as %d", i);
          break;
        }
      }
    }

    // Handle client sockets
    for (int i = 0; i < currentMaxClients; i++) {
      int sd = client_sockets[i];

      if (FD_ISSET(sd, &read_fds)) {
        // Get the session for this client
        RTSP_Session* session = nullptr;
        for (auto& sess : sessions) {
          if (sess.second.sock == sd) {
            session = &sess.second;
            break;
          }
        }
        if (session) {
          // Handle RTSP request
          bool keepConnection = handleRTSPRequest(*session);
          if (!keepConnection) {
             // Check if all clients have disconnected 
            if (getActiveRTSPClients() == 1) {
              setIsPlaying(false);
              closeSockets();
              ESP_LOGD(LOG_TAG, "All clients disconnected. Resetting firstClientConnected flag."); 
              this->firstClientConnected = false; 
              this->firstClientIsMulticast = false; 
              this->firstClientIsTCP = false; 
            }
            close(sd);
            client_sockets[i] = 0;
            sessions.erase(session->sessionID); // Remove session when client disconnects
            decrementActiveRTSPClients();
          }
        }
      }
    }
  }
}