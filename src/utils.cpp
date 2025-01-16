#include "ESP32-RTSPServer.h"

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

void RTSPServer::updateIsPlayingStatus() {
  bool anyClientStreaming = false;
  for (const auto& sessionPair : sessions) {
    if (sessionPair.second.isPlaying) {
      anyClientStreaming = true;
      break;
    }
  }
  setIsPlaying(anyClientStreaming);
}

void RTSPServer::setIsPlaying(bool playing) {
    xSemaphoreTake(isPlayingMutex, portMAX_DELAY); // Take the mutex
    this->isPlaying = playing;
    xSemaphoreGive(isPlayingMutex); // Release the mutex
}

bool RTSPServer::getIsPlaying() const {
    bool playing;
    xSemaphoreTake(isPlayingMutex, portMAX_DELAY); // Take the mutex
    playing = this->isPlaying;
    xSemaphoreGive(isPlayingMutex); // Release the mutex
    return playing;
}

/**
 * @brief Checks if the server is ready to send a frame.
 * 
 * @return true if ready, false otherwise.
 */
bool RTSPServer::readyToSendFrame() const {
  return getIsPlaying() && this->rtpFrameSent;
}

/**
 * @brief Checks if the server is ready to send audio.
 * 
 * @return true if ready, false otherwise.
 */
bool RTSPServer::readyToSendAudio() const {
  return getIsPlaying() && this->rtpAudioSent;
}

/**
 * @brief Checks if the server is ready to send subtitles.
 * 
 * @return true if ready, false otherwise.
 */
bool RTSPServer::readyToSendSubtitles() const {
  return getIsPlaying() && this->rtpSubtitlesSent;
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
