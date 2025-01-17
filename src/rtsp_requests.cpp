#include "ESP32-RTSPServer.h"

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

#ifndef OVERRIDE_RTSP_SINGLE_CLIENT_MODE
  // Track the first client's connection type
  if (!firstClientConnected) {
    firstClientConnected = true;
    firstClientIsMulticast = session.isMulticast;
    firstClientIsTCP = session.isTCP;

    // Set max clients based on the first client's connection type
    setMaxClients(firstClientIsMulticast ? this->maxRTSPClients : 1);
  } else {
    // Determine if the connection should be rejected
    bool rejectConnection = (firstClientIsMulticast && !session.isMulticast) ||
                            (!firstClientIsMulticast && (session.isMulticast || session.isTCP != firstClientIsTCP));

    if (rejectConnection) {
      ESP_LOGW(LOG_TAG, "Rejecting connection because it does not match the first client's connection type");
      char response[512];
      snprintf(response, sizeof(response),
               "RTSP/1.0 461 Unsupported Transport\r\n"
               "CSeq: %d\r\n"
               "%s\r\n\r\n",
               session.cseq, dateHeader());
      if (write(session.sock, response, strlen(response)) < 0) {
        ESP_LOGE(LOG_TAG, "Failed to send rejection response to client.");
      }
      return;
    }
  }
#else
  setMaxClients(this->maxRTSPClients);
#endif

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
    if (!session.isTCP) {
      if (session.isMulticast) {
        this->checkAndSetupUDP(this->videoMulticastSocket, true, serverPort, this->rtpIp);
      } else {
        this->checkAndSetupUDP(this->videoUnicastSocket, false, serverPort, this->rtpIp);
      }
    }
  }
  
  if (setAudio) {
    session.cAudioPort = clientPort;
    serverPort = this->rtpAudioPort;
    this->audioCh = rtpChannel;
    if (!session.isTCP) {
      if (session.isMulticast) {
        this->checkAndSetupUDP(this->audioMulticastSocket, true, serverPort, this->rtpIp);
      } else {
        this->checkAndSetupUDP(this->audioUnicastSocket, false, serverPort, this->rtpIp);
      }
    }
  }
  
  if (setSubtitles) {
    session.cSrtPort = clientPort;
    serverPort = this->rtpSubtitlesPort;
    this->subtitlesCh = rtpChannel;
    if (!session.isTCP) {
      if (session.isMulticast) {
        this->checkAndSetupUDP(this->subtitlesMulticastSocket, true, serverPort, this->rtpIp);
      } else {
        this->checkAndSetupUDP(this->subtitlesUnicastSocket, false, serverPort, this->rtpIp);
      }
    }
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
  session.isPlaying = true;
  this->sessions[session.sessionID] = session;
  setIsPlaying(true);

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
  session.isPlaying = false;
  this->sessions[session.sessionID] = session;
  updateIsPlayingStatus();
  char response[128];
  int len = snprintf(response, sizeof(response),
                     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %lu\r\n\r\n",
                     session.cseq, session.sessionID);
  write(session.sock, response, len);
  ESP_LOGD(LOG_TAG, "Session %u is now paused.", session.sessionID);
}

/**
 * @brief Handles the TEARDOWN RTSP request.
 * 
 * @param session The RTSP session.
 */
void RTSPServer::handleTeardown(RTSP_Session& session) {
  session.isPlaying = false;
  this->sessions[session.sessionID] = session;
  updateIsPlayingStatus();

  char response[128];
  int len = snprintf(response, sizeof(response),
                     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %lu\r\n\r\n",
                     session.cseq, session.sessionID);
  write(session.sock, response, len);

  ESP_LOGD(LOG_TAG, "RTSP Session %u has been torn down.", session.sessionID);
}

/**
 * @brief Handles incoming RTSP requests.
 * 
 * @param sock The socket file descriptor.
 * @param clientAddr The client address.
 * @return true if the request was handled successfully, false otherwise.
 */
bool RTSPServer::handleRTSPRequest(RTSP_Session& session) {
  char *buffer = (char *)ps_malloc(RTSP_BUFFER_SIZE);
  if (!buffer) {
    ESP_LOGE(LOG_TAG, "Failed to allocate buffer with ps_malloc");
    return false;
  }

  int totalLen = 0;
  int len = 0;

  // Read data from socket until end of RTSP header or buffer limit is reached
  while ((len = recv(session.sock, buffer + totalLen, RTSP_BUFFER_SIZE - totalLen - 1, 0)) > 0) {
    totalLen += len;
    if (strstr(buffer, "\r\n\r\n")) {
      break;
    }
    if (totalLen >= RTSP_BUFFER_SIZE) { // Adjusted for null-terminator
      ESP_LOGE(LOG_TAG, "Request too large for buffer. Total length: %d", totalLen);
      free(buffer); // Free allocated memory
      return false;
    }
  }
  //ESP_LOGD(LOG_TAG, "Request total length: %d", totalLen);

  if (totalLen <= 0) {
    int err = errno;
    free(buffer); // Free allocated memory
    if (err == EWOULDBLOCK || err == EAGAIN) {
      return true;
    } else if (err == ECONNRESET || err == ENOTCONN) {
      // Handle teardown when connection is reset or not connected based on client IP
      ESP_LOGD(LOG_TAG, "HandleTeardown");
      this->handleTeardown(session);
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
    write(session.sock, "RTSP/1.0 400 Bad Request\r\n\r\n", 29);
    free(buffer); // Free allocated memory
    return true;
  }

  session.cseq = cseq;

  // Extract session ID using the provided function
  uint32_t sessionID = extractSessionID(buffer);
  if (sessionID != 0 && sessions.find(sessionID) != sessions.end()) {
    session.sessionID = sessionID;
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
