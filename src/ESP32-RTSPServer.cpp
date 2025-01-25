#include "ESP32-RTSPServer.h"

const char* RTSPServer::LOG_TAG = "RTSPServer";

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
    firstClientIsTCP(false),
    authEnabled(false), // Initialize authEnabled to false
    useSecure(false),
    userCert(nullptr),
    userKey(nullptr)
{
    isPlayingMutex = xSemaphoreCreateMutex(); // Initialize the mutex
    sendTcpMutex = xSemaphoreCreateMutex(); // Initialize the mutex
    maxClientsMutex = xSemaphoreCreateMutex();
    // Initialize mbedtls members
    mbedtls_ssl_init(&this->ssl);
    mbedtls_ssl_config_init(&this->conf);
    mbedtls_x509_crt_init(&this->srvcert);
    mbedtls_pk_init(&this->pkey);
    mbedtls_entropy_init(&this->entropy);
    mbedtls_ctr_drbg_init(&this->ctr_drbg);

#ifdef RTSP_LOGGING_ENABLED
    esp_log_level_set(LOG_TAG, ESP_LOG_DEBUG); // Set log level to DEBUG
#endif
}

RTSPServer::~RTSPServer() {
  // Clean up resources
  deinit();
  vSemaphoreDelete(this->isPlayingMutex);
  vSemaphoreDelete(this->sendTcpMutex);
  vSemaphoreDelete(this->maxClientsMutex);
  // Free mbedtls members
  mbedtls_ssl_free(&this->ssl);
  mbedtls_ssl_config_free(&this->conf);
  mbedtls_x509_crt_free(&this->srvcert);
  mbedtls_pk_free(&this->pkey);
  mbedtls_entropy_free(&this->entropy);
  mbedtls_ctr_drbg_free(&this->ctr_drbg);
}

bool RTSPServer::init(TransportType transport, uint16_t rtspPort, uint32_t sampleRate, uint16_t port1, uint16_t port2, uint16_t port3, IPAddress rtpIp, uint8_t rtpTTL) {
  this->transport = (transport != NONE) ? transport : this->transport;
  this->rtspPort = (rtspPort != 0) ? rtspPort : this->rtspPort;
  this->rtpIp = (rtpIp != IPAddress()) ? rtpIp : this->rtpIp;
  this->rtpTTL = (rtpTTL != 255) ? rtpTTL : this->rtpTTL;

  if (transport == AUDIO_ONLY || transport == VIDEO_AND_AUDIO || transport == AUDIO_AND_SUBTITLES || transport == VIDEO_AUDIO_SUBTITLES) {
    if (this->sampleRate == 0 && sampleRate == 0) {
      if (Serial) {
        Serial.printf("RTSP Server Error: Sample rate must be set to use audio\n");
      }
      return false;
    }
    if (sampleRate != 0) {
      this->sampleRate = sampleRate;
    }
  }

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
      RTSP_LOGE(LOG_TAG, "Transport type can not be NONE");
      return false;
    default:
      RTSP_LOGE(LOG_TAG, "Invalid transport type for this init method");
      return false;
  }

  return prepRTSP();
}

void RTSPServer::deinit() {
  if (this->rtspTaskHandle != NULL) {
    vTaskDelete(this->rtspTaskHandle);
    this->rtspTaskHandle = NULL;
  }
  if (this->rtpVideoTaskHandle != NULL) {
    vTaskDelete(this->rtpVideoTaskHandle);
    this->rtpVideoTaskHandle = NULL;
  }
  if (this->rtspSocket >= 0) {
    close(this->rtspSocket);
    this->rtspSocket = -1;
  }
  
  closeSockets();
  
  if (this->rtspStreamBuffer) {
    free(this->rtspStreamBuffer);
  }

  RTSP_LOGI(LOG_TAG, "RTSP server deinitialized.");
}

bool RTSPServer::reinit() {
  deinit();
  return init();
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

const char server_cert[] = "-----BEGIN CERTIFICATE-----\n"
                           "MIIDOzCCAiOgAwIBAgIUQE0XF7aSIgJMKtFBcSOuXGb+ctMwDQYJKoZIhvcNAQEL\n"
                           "BQAwRjELMAkGA1UEBhMCQVUxDjAMBgNVBAgMBUVTUDMyMRIwEAYDVQQKDAlYZW5v\n"
                           "aW9uZXgxEzARBgNVBAMMClJUU1BTZXJ2ZXIwHhcNMjUwMTE5MDAyMzU1WhcNMjYw\n"
                           "MTE5MDAyMzU1WjBGMQswCQYDVQQGEwJBVTEOMAwGA1UECAwFRVNQMzIxEjAQBgNV\n"
                           "BAoMCVhlbm9pb25leDETMBEGA1UEAwwKUlRTUFNlcnZlcjCCASIwDQYJKoZIhvcN\n"
                           "AQEBBQADggEPADCCAQoCggEBALjaoRRh+k4M2k+1f/02cd1aimVhKl0gklTzOxXb\n"
                           "vtcVkdPNzZnBI0mzzjuNB3E1KOM4iZ7i3H0p62hK045I5Cyju/Jvq/enX3heVvSK\n"
                           "VOREoh1CZ5goT+29TtCrJyHGJNt0Z+zCXseCJxEH5FyWdoF5rkiku/YgWFPv2dBI\n"
                           "TA3G0wBN3Ex9rOBKgWSX7JYmjGVZIWZPsiiZKoZunuLrILsLGPFnIevEpK3TeWhp\n"
                           "R61slc17IxxzEV9rVBDOHzEAyoY/aUw13HwtErecUftlLmEPY31p0KXx2FLPGMOX\n"
                           "zeUcFOR3zedshXO96Yhw5vZ8bND9ZsQhPL++geUV75LS2B8CAwEAAaMhMB8wHQYD\n"
                           "VR0OBBYEFNjDwPAJh1wYvC5OinFZvM5KQoZCMA0GCSqGSIb3DQEBCwUAA4IBAQAR\n"
                           "p6EG0plqCIfVH3PISA9tymh1wQ9V2dHxdmifotAR5jIxIH3qJYcQjjzWKWHEKNTW\n"
                           "Nn+1oD6Ci2glCugtJ6M7vjQC082g2tuxyTYgxDZTF8SU1K0F0/FWYipIYnlymcQC\n"
                           "ZHoXRR76R0MizzFNoj/0n1aEDbeiqAd2lXuJvEY8Yj4oVpufKO5dH007ypDEhjLF\n"
                           "fqz6uwbV9N//n7qf3gzuKeV318pHY4/+pjhmFzykUBfJn3jlC+x/f4P7/gb30qqX\n"
                           "K/Q+PRfaIgurQxOa2Aqku00VfLyvJkXydRt/to3t1vIJXQwpNgiwKeQyMRy8BpQP\n"
                           "RGJBUgmxUsuQ86tNNG0S\n"
                           "-----END CERTIFICATE-----\n";

const char server_key[] = "-----BEGIN PRIVATE KEY-----\n"
                          "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC42qEUYfpODNpP\n"
                          "tX/9NnHdWoplYSpdIJJU8zsV277XFZHTzc2ZwSNJs847jQdxNSjjOIme4tx9Keto\n"
                          "StOOSOQso7vyb6v3p194Xlb0ilTkRKIdQmeYKE/tvU7QqychxiTbdGfswl7HgicR\n"
                          "B+RclnaBea5IpLv2IFhT79nQSEwNxtMATdxMfazgSoFkl+yWJoxlWSFmT7IomSqG\n"
                          "bp7i6yC7CxjxZyHrxKSt03loaUetbJXNeyMccxFfa1QQzh8xAMqGP2lMNdx8LRK3\n"
                          "nFH7ZS5hD2N9adCl8dhSzxjDl83lHBTkd83nbIVzvemIcOb2fGzQ/WbEITy/voHl\n"
                          "Fe+S0tgfAgMBAAECggEAUNU5ruIRRGQWatihOSa/r93Ruvc8QTDnDEK4SZyn3QHP\n"
                          "nODX0S5FsgrwWdYXtQdb89BbTaFCbXERQa/GxudU77hBjYgLmOYVt1r5h0GeUZs5\n"
                          "kLXi+wRVv04S0uik2f39TAGhGET+kRt23dmnoF0jQQbmkVmH397RARNXGd8jcroJ\n"
                          "TJZGX0O6XwR25Ln2F1ZajNxcvl3EpEmvTkTehJ2tLHZRjIgUxBzU/In2U3w7zMvD\n"
                          "5AHzuu8SYYXJLRSEZpb7t7zVSygk6dq/eA5TEYnnBHWikUZgmWGqLzfEKExg//lj\n"
                          "7o3QDtMEy/tEMyCo4wJBzycxDu3Lpv7Au6iGZEwSsQKBgQDy8IQNo//eRlK0T16k\n"
                          "3V06m6331Icry1bTOUS05SA/dZBRwx/PQdQHbvIS4b1Co34yPQ0jWX9ByJJq0DIT\n"
                          "SHKY/QiCKexLDGfOH1aCflWeRBOe+8GpyyrcKswVhHrUHNDUPKpvqKt0QVVksF++\n"
                          "lNoa27yJb4k/lXYDoCCli92z3QKBgQDCyrRuXYHrYG6zAYcmcaiN8vv0kqLj7WXl\n"
                          "0V6cfmzM2XeUeEhGrJHcmoYdtbCeGrYNPQPuh+qZJYpd1libhbD1Lbx+nmOCO5Zg\n"
                          "R5wzJUJeVe8cAV8ZcII9ssIu20Ygh8JLLdD9TJvEQe7i8KSNDvnx5gXQ5Qyqa9jn\n"
                          "zKNfrFsKKwKBgQDTgURmn85m1PCyHJu4iyTcnqcSKgzYuvpHgQdpOkYeGZthI7Dw\n"
                          "/026fwPwHmpotvBqiq6ChSt+uUGIDuRZ12w1963NrPQUzqMdWDk3+QdPd9NhSavs\n"
                          "yH3zGqt9XE6XltLUxFBaUA2ddDgFJmzk/rD4SzBUsy9ZlSUeuO1TU3bcXQKBgDDY\n"
                          "h9pNFHEjo167+VoIkk3WKpwv0Sz6sNyqXeE8SqgruDLA3s7qI0GAdjUpBHsBE7nK\n"
                          "HRxJdOimY0YYo+uL4M36hBP0P9u+eXz3OBITO8tQHT+WkHRepCo4kz0Qu0sY//2+\n"
                          "JXP/K+hQ6Eh8M6czpDWmirrRVRW/KYsm46jwHXw1AoGAPuKGJA7VJ2UFCNL0Dl+V\n"
                          "2J1Ukw0Y+GdwJUn+GEEEGraii+hbighUVB4IcRP02wHEiFxy+Ytn6jAApZxApccU\n"
                          "H4xOWQpldyg4wERAKsoa+1A3Q4DbxLxcBv/GNQfifbl3dp5pyqTH3kjWqPL7PJXP\n"
                          "eXEf9iDwQ2+BpAMYH5t8vOk=\n"
                          "-----END PRIVATE KEY-----\n";

bool RTSPServer::prepRTSP() {
  uint64_t mac = ESP.getEfuseMac();
  this->videoSSRC = static_cast<uint32_t>(mac & 0xFFFFFFFF);
  this->audioSSRC = static_cast<uint32_t>((mac >> 32) & 0xFFFFFFFF);
  this->subtitlesSSRC = static_cast<uint32_t>((mac >> 48) & 0xFFFFFFFF);

  if (this->useSecure) {
    // Initialize mbedtls
    mbedtls_ssl_init(&this->ssl);
    mbedtls_ssl_config_init(&this->conf);
    mbedtls_x509_crt_init(&this->srvcert);
    mbedtls_pk_init(&this->pkey);
    mbedtls_entropy_init(&this->entropy);
    mbedtls_ctr_drbg_init(&this->ctr_drbg);

    // Load certificates and private key
    if (mbedtls_x509_crt_parse(&this->srvcert, (const unsigned char*)userCert, strlen(userCert) + 1) != 0 ||
        mbedtls_pk_parse_key(&this->pkey, (const unsigned char*)userKey, strlen(userKey) + 1, NULL, 0, mbedtls_ctr_drbg_random, &this->ctr_drbg) != 0) {
      RTSP_LOGE(LOG_TAG, "Failed to load server certificates and key.");
      return false;
    }
    // Seed the random number generator
    if (mbedtls_ctr_drbg_seed(&this->ctr_drbg, mbedtls_entropy_func, &this->entropy, NULL, 0) != 0) {
      RTSP_LOGE(LOG_TAG, "Failed to seed the random number generator.");
      return false;
    }

    // Set up SSL configuration
    if (mbedtls_ssl_config_defaults(&this->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
      RTSP_LOGE(LOG_TAG, "Failed to set up SSL configuration.");
      return false;
    }

    mbedtls_ssl_conf_ca_chain(&this->conf, this->srvcert.next, NULL);
    mbedtls_ssl_conf_rng(&this->conf, mbedtls_ctr_drbg_random, &this->ctr_drbg);
    if (mbedtls_ssl_conf_own_cert(&this->conf, &this->srvcert, &this->pkey) != 0) {
      RTSP_LOGE(LOG_TAG, "Failed to set server certificate.");
      return false;
    }

    if (mbedtls_ssl_setup(&this->ssl, &this->conf) != 0) {
      RTSP_LOGE(LOG_TAG, "Failed to set up SSL context.");
      return false;
    }
  }

  // Socket setup code remains unchanged
  this->rtspSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (this->rtspSocket < 0) {
    RTSP_LOGE(LOG_TAG, "Failed to create RTSP socket.");
    return false;
  }

  if (!setNonBlocking(this->rtspSocket)) {
    RTSP_LOGE(LOG_TAG, "Failed to set RTSP socket to non-blocking mode.");
    close(this->rtspSocket);
    return false;
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(this->rtspPort);

  if (bind(this->rtspSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    RTSP_LOGE(LOG_TAG, "Failed to bind RTSP socket: %d", this->rtspSocket);
    close(this->rtspSocket);
    return false;
  }
  if (listen(this->rtspSocket, 5) < 0) {
    RTSP_LOGE(LOG_TAG, "Failed to listen on RTSP socket.");
    close(this->rtspSocket);
    return false;
  }

  if (this->rtspTaskHandle == NULL) {
    if (xTaskCreate(rtspTaskWrapper, "rtspTask", RTSP_STACK_SIZE, this, RTSP_PRI, &this->rtspTaskHandle) != pdPASS) {
      RTSP_LOGE(LOG_TAG, "Failed to create RTSP task.");
      close(this->rtspSocket);
      return false;
    }
  }

  RTSP_LOGI(LOG_TAG, "RTSP server setup completed, listening on port: %d", this->rtspPort);
  return true;
}


// bool RTSPServer::prepRTSP() {
//   uint64_t mac = ESP.getEfuseMac();
//   this->videoSSRC = static_cast<uint32_t>(mac & 0xFFFFFFFF);
//   this->audioSSRC = static_cast<uint32_t>((mac >> 32) & 0xFFFFFFFF);
//   this->subtitlesSSRC = static_cast<uint32_t>((mac >> 48) & 0xFFFFFFFF);

//   this->rtspSocket = socket(AF_INET, SOCK_STREAM, 0);
//   if (this->rtspSocket < 0) {
//     RTSP_LOGE(LOG_TAG, "Failed to create RTSP socket.");
//     return false;
//   }

//   if (!setNonBlocking(this->rtspSocket)) {
//     RTSP_LOGE(LOG_TAG, "Failed to set RTSP socket to non-blocking mode.");
//     close(this->rtspSocket);
//     return false;
//   }

//   struct sockaddr_in serverAddr;
//   serverAddr.sin_family = AF_INET;
//   serverAddr.sin_addr.s_addr = INADDR_ANY;
//   serverAddr.sin_port = htons(this->rtspPort);

//   if (bind(this->rtspSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
//     RTSP_LOGE(LOG_TAG, "Failed to bind RTSP socket: %d", this->rtspSocket);
//     close(this->rtspSocket);
//     return false;
//   }
  
//   if (listen(this->rtspSocket, 5) < 0) {
//     RTSP_LOGE(LOG_TAG, "Failed to listen on RTSP socket.");
//     close(this->rtspSocket);
//     return false;
//   }

//   if (this->rtspTaskHandle == NULL) {
//     if (xTaskCreate(rtspTaskWrapper, "rtspTask", RTSP_STACK_SIZE, this, RTSP_PRI, &this->rtspTaskHandle) != pdPASS) {
//       RTSP_LOGE(LOG_TAG, "Failed to create RTSP task.");
//       close(this->rtspSocket);
//       return false;
//     }
//   }

//   RTSP_LOGI(LOG_TAG, "RTSP server setup completed, listening on port: %d", this->rtspPort);
//   return true;
// }

void RTSPServer::rtspTaskWrapper(void* pvParameters) {
  RTSPServer* server = static_cast<RTSPServer*>(pvParameters);
  server->rtspTask();
}

void RTSPServer::rtspTask() {
  fd_set read_fds;
  int client_sockets[MAX_CLIENTS] = {0};
  int max_sd, activity, client_sock;
  mbedtls_net_context client_fd;
  mbedtls_ssl_context ssl;

  while (true) {
    FD_ZERO(&read_fds);
    FD_SET(this->rtspSocket, &read_fds);
    max_sd = this->rtspSocket;

    uint8_t currentMaxClients = getMaxClients();

    for (int i = 0; i < currentMaxClients; i++) {
      int sd = client_sockets[i];
      if (sd > 0) FD_SET(sd, &read_fds);
      if (sd > max_sd) max_sd = sd;
    }

    activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

    if (activity < 0 && errno != EINTR) {
      RTSP_LOGE(LOG_TAG, "Select error");
      continue;
    }

    if (FD_ISSET(this->rtspSocket, &read_fds)) {
      if (getActiveRTSPClients() >= currentMaxClients) {
        if (this->useSecure) {
          if (!acceptSecureClient(client_fd, ssl)) continue;
          sendErrorResponse(true, client_fd, client_sock);
        } else {
          if (!acceptNonSecureClient(client_sock)) continue;
          sendErrorResponse(false, client_fd, client_sock);
        }
        continue;
      }

      if (this->useSecure) {
        if (!acceptSecureClient(client_fd, ssl)) continue;
      } else {
        if (!acceptNonSecureClient(client_sock)) continue;
      }

      RTSP_LOGI(LOG_TAG, "New client connected");
      createSession(client_sock, client_fd, ssl, this->useSecure);

      for (int i = 0; i < currentMaxClients; i++) {
        if (client_sockets[i] == 0) {
          client_sockets[i] = this->useSecure ? client_fd.fd : client_sock;
          incrementActiveRTSPClients();
          RTSP_LOGI(LOG_TAG, "Added to list of sockets as %d", i);
          break;
        }
      }
    }

    for (int i = 0; i < currentMaxClients; i++) {
      int sd = client_sockets[i];

      if (FD_ISSET(sd, &read_fds)) {
        // Get the session for this client
        RTSP_Session* session = nullptr;
        for (auto& sess : sessions) {
          if ((this->useSecure && sess.second.client_fd.fd == sd) || (!this->useSecure && sess.second.sock == sd)) {
            session = &sess.second;
            break;
          }
        }
        if (session) {
          bool keepConnection = handleRTSPRequest(*session);
          if (!keepConnection) {
            if (getActiveRTSPClients() == 1) {
              setIsPlaying(false);
              closeSockets();
              RTSP_LOGD(LOG_TAG, "All clients disconnected. Resetting firstClientConnected flag."); 
              this->firstClientConnected = false; 
              this->firstClientIsMulticast = false; 
              this->firstClientIsTCP = false; 
            }
            if (this->useSecure) {
              mbedtls_ssl_close_notify(&session->ssl);
              mbedtls_ssl_free(&session->ssl);
              mbedtls_net_free(&session->client_fd);
            } else {
              close(sd);
            }
            client_sockets[i] = 0;
            sessions.erase(session->sessionID); // Remove session when client disconnects
            decrementActiveRTSPClients();
          }
        }
      }
    }
  }
}

// void RTSPServer::rtspTask() {
//   struct sockaddr_in clientAddr;
//   socklen_t addr_len = sizeof(clientAddr);
//   fd_set read_fds;
//   int client_sockets[MAX_CLIENTS] = {0};
//   int max_sd, activity, client_sock;

//   while (true) {
//     FD_ZERO(&read_fds);
//     FD_SET(this->rtspSocket, &read_fds);
//     max_sd = this->rtspSocket;

//     uint8_t currentMaxClients = getMaxClients();

//     for (int i = 0; i < currentMaxClients; i++) {
//       int sd = client_sockets[i];
//       if (sd > 0) FD_SET(sd, &read_fds);
//       if (sd > max_sd) max_sd = sd;
//     }

//     activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

//     if (activity < 0 && errno != EINTR) {
//       RTSP_LOGE(LOG_TAG, "Select error");
//       continue;
//     }

//     if (FD_ISSET(this->rtspSocket, &read_fds)) {
//       if (getActiveRTSPClients() >= currentMaxClients) {
//         client_sock = accept(this->rtspSocket, (struct sockaddr *)&clientAddr, &addr_len);
//         if (client_sock < 0) {
//           RTSP_LOGE(LOG_TAG, "Accept error");
//           continue;
//         }

//         const char* response = "RTSP/1.0 503 Service Unavailable\r\n\r\n";
//         write(client_sock, response, strlen(response));
//         close(client_sock);
//         RTSP_LOGE(LOG_TAG, "Max clients reached. Sent 503 error to new client.");
//         continue;
//       }

//       client_sock = accept(this->rtspSocket, (struct sockaddr *)&clientAddr, &addr_len);
//       if (client_sock < 0) {
//         RTSP_LOGE(LOG_TAG, "Accept error");
//         continue;
//       }

//       if (!setNonBlocking(client_sock)) {
//         RTSP_LOGE(LOG_TAG, "Failed to set RTSP socket to non-blocking mode.");
//         close(client_sock);
//         continue;
//       }

//       RTSP_LOGI(LOG_TAG, "New client connected");

//       // Create a new session for the new client
//       RTSP_Session session = {
//         esp_random(),
//         client_sock,
//         0,
//         0,
//         0,
//         0,
//         false,
//         false,
//         false
//       };
//       sessions[session.sessionID] = session;

//       for (int i = 0; i < currentMaxClients; i++) {
//         if (client_sockets[i] == 0) {
//           client_sockets[i] = client_sock;
//           incrementActiveRTSPClients();
//           RTSP_LOGI(LOG_TAG, "Added to list of sockets as %d", i);
//           break;
//         }
//       }
//     }

//     for (int i = 0; i < currentMaxClients; i++) {
//       int sd = client_sockets[i];

//       if (FD_ISSET(sd, &read_fds)) {
//         // Get the session for this client
//         RTSP_Session* session = nullptr;
//         for (auto& sess : sessions) {
//           if (sess.second.sock == sd) {
//             session = &sess.second;
//             break;
//           }
//         }
//         if (session) {
//           bool keepConnection = handleRTSPRequest(*session);
//           if (!keepConnection) {
//             if (getActiveRTSPClients() == 1) {
//               setIsPlaying(false);
//               closeSockets();
//               RTSP_LOGD(LOG_TAG, "All clients disconnected. Resetting firstClientConnected flag."); 
//               this->firstClientConnected = false; 
//               this->firstClientIsMulticast = false; 
//               this->firstClientIsTCP = false; 
//             }
//             close(sd);
//             client_sockets[i] = 0;
//             sessions.erase(session->sessionID); // Remove session when client disconnects
//             decrementActiveRTSPClients();
//           }
//         }
//       }
//     }
//   }
// }
