#include "ESP32-RTSPServer.h"

void RTSPServer::startSubtitlesTimer(esp_timer_cb_t userCallback) { 
  const esp_timer_create_args_t timerConfig = { 
    .callback = userCallback, // User-defined callback function 
    .arg = nullptr, // Optional argument, can be set to NULL
    .dispatch_method = ESP_TIMER_TASK, // Dispatch method, set to default
    .name = "periodic_timer" ,
    .skip_unhandled_events = false // Optional, can be set to false
    }; 
    esp_timer_create(&timerConfig, &sendSubtitlesTimer); 
    esp_timer_start_periodic(sendSubtitlesTimer, 1000000); 
}

void RTSPServer::setMaxClients(uint8_t newMaxClients) {
  if (xSemaphoreTake(maxClientsMutex, portMAX_DELAY) == pdTRUE) {
    if (newMaxClients <= MAX_CLIENTS) {
      this->maxClients = newMaxClients;
      RTSP_LOGI(LOG_TAG, "Max clients updated to: %d", this->maxClients);
    } else {
      RTSP_LOGW(LOG_TAG, "Requested max clients (%d) exceeds the hardcoded limit (%d). Max clients set to %d.", newMaxClients, MAX_CLIENTS, MAX_CLIENTS);
      this->maxClients = MAX_CLIENTS;
    }
    xSemaphoreGive(maxClientsMutex);
  } else {
    RTSP_LOGE(LOG_TAG, "Failed to acquire maxClients mutex");
  }
}

uint8_t RTSPServer::getMaxClients() {
  uint8_t clients = 0;
  if (xSemaphoreTake(maxClientsMutex, portMAX_DELAY) == pdTRUE) {
    clients = this->maxClients;
    xSemaphoreGive(maxClientsMutex);
  } else {
    RTSP_LOGE(LOG_TAG, "Failed to acquire maxClients mutex");
  }
  return clients;
}

void RTSPServer::incrementActiveRTSPClients() {
  if (this->activeRTSPClients < 255) {
    this->activeRTSPClients++;
    RTSP_LOGI(LOG_TAG, "Active RTSP clients count incremented: %d", this->activeRTSPClients);
  } else {
    RTSP_LOGW(LOG_TAG, "Max RTSP clients reached: %d", 255);
  }
}

void RTSPServer::decrementActiveRTSPClients() {
  if (this->activeRTSPClients > 0) {
    this->activeRTSPClients--;
    RTSP_LOGI(LOG_TAG, "Active RTSP clients count decremented: %d", this->activeRTSPClients);
  } else {
    RTSP_LOGW(LOG_TAG, "Min RTSP clients already: %d", 0);
  }
}

uint8_t RTSPServer::getActiveRTSPClients() {
  return this->activeRTSPClients;
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
    xSemaphoreTake(isPlayingMutex, portMAX_DELAY);
    this->isPlaying = playing;
    xSemaphoreGive(isPlayingMutex);
}

bool RTSPServer::getIsPlaying() const {
    bool playing;
    xSemaphoreTake(isPlayingMutex, portMAX_DELAY);
    playing = this->isPlaying;
    xSemaphoreGive(isPlayingMutex);
    return playing;
}

bool RTSPServer::readyToSendFrame() const {
  return getIsPlaying() && this->rtpFrameSent;
}

bool RTSPServer::readyToSendAudio() const {
  return getIsPlaying() && this->rtpAudioSent;
}

bool RTSPServer::readyToSendSubtitles() const {
  return getIsPlaying() && this->rtpSubtitlesSent;
}

int RTSPServer::captureCSeq(char* request) {
  char* cseqStr = strstr(request, "CSeq: ");
  if (cseqStr == NULL) {
    return -1;
  }
  cseqStr += 6;
  char* endOfLine = strchr(cseqStr, '\n');
  if (endOfLine) {
    *endOfLine = 0;
  }
  int cseq = atoi(cseqStr);
  if (endOfLine) {
    *endOfLine = '\n';
  }
  return cseq;
}

uint32_t RTSPServer::generateSessionID() {
  return esp_random();
}

uint32_t RTSPServer::extractSessionID(char* request) {
  char* sessionStr = strstr(request, "Session: ");
  if (sessionStr == NULL) {
    return 0;
  }
  sessionStr += 9;
  char* endOfLine = strchr(sessionStr, '\n');
  if (endOfLine) {
    *endOfLine = 0;
  }

  while (isspace(*sessionStr)) sessionStr++;
  char* end = sessionStr + strlen(sessionStr) - 1;
  while (end > sessionStr && isspace(*end)) end--;
  *(end + 1) = 0;

  uint32_t sessionID = strtoul(sessionStr, NULL, 10);

  if (endOfLine) {
    *endOfLine = '\n';
  }

  return sessionID;
}

const char* RTSPServer::dateHeader() {
  static char buffer[50];
  time_t now = time(NULL);
  strftime(buffer, sizeof(buffer), "Date: %a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
  return buffer;
}
