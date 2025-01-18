#ifndef ESP32_RTSP_SERVER_H
#define ESP32_RTSP_SERVER_H

#include <WiFi.h>
#include "lwip/sockets.h"
#include <esp_log.h>
#include <map>

#define MAX_RTSP_BUFFER (512 * 1024)
#define RTP_STACK_SIZE (1024 * 4)
#define RTP_PRI 10
#define RTSP_STACK_SIZE (1024 * 8)
#define RTSP_PRI 10
#define MAX_CLIENTS 10 // max rtsp clients

#define RTSP_BUFFER_SIZE 8092

// Define ESP32_RTSP_LOGGING_ENABLED to enable logging
//#define RTSP_LOGGING_ENABLED // save 7.7kb of flash

#ifdef RTSP_LOGGING_ENABLED
  #define RTSP_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
  #define RTSP_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
  #define RTSP_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
  #define RTSP_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#else
  #define RTSP_LOGI(tag, format, ...)
  #define RTSP_LOGW(tag, format, ...)
  #define RTSP_LOGE(tag, format, ...)
  #define RTSP_LOGD(tag, format, ...)
#endif

// User defined options in sketch
//#define OVERRIDE_RTSP_SINGLE_CLIENT_MODE // Override the default behavior of allowing only one client for unicast or TCP
//#define RTSP_VIDEO_NONBLOCK // Enable non-blocking video streaming by creating a separate task for video streaming, preventing it from blocking the main sketch.
struct RTSP_Session {
  uint32_t sessionID;
  int sock;
  IPAddress clientIP;
  int cseq;
  uint16_t cVideoPort;
  uint16_t cAudioPort;
  uint16_t cSrtPort;
  bool isMulticast;
  bool isPlaying;
  bool isTCP;
};
class RTSPServer {
public:
  enum TransportType {
    VIDEO_ONLY,
    AUDIO_ONLY,
    SUBTITLES_ONLY,
    VIDEO_AND_AUDIO,
    VIDEO_AND_SUBTITLES,
    AUDIO_AND_SUBTITLES,
    VIDEO_AUDIO_SUBTITLES,
    NONE,
  };

  RTSPServer();  // Defined in ESP32-RTSPServer.cpp
  ~RTSPServer();  // Destructor, defined in ESP32-RTSPServer.cpp

  bool init(TransportType transport = NONE, uint16_t rtspPort = 0, uint32_t sampleRate = 0, uint16_t port1 = 0, uint16_t port2 = 0, uint16_t port3 = 0, IPAddress rtpIp = IPAddress(), uint8_t rtpTTL = 255);  // Defined in ESP32-RTSPServer.cpp
  
  void deinit();  // Defined in ESP32-RTSPServer.cpp

  bool reinit();  // Defined in ESP32-RTSPServer.cpp

  void sendRTSPFrame(const uint8_t* data, size_t len, int quality, int width, int height);  // Defined in rtp.cpp

  void sendRTSPAudio(int16_t* data, size_t len);  // Defined in rtp.cpp

  void sendRTSPSubtitles(char* data, size_t len);  // Defined in rtp.cpp

  void startSubtitlesTimer(esp_timer_cb_t userCallback);  // Defined in utils.cpp

  bool readyToSendFrame() const;  // Defined in utils.cpp

  bool readyToSendAudio() const;  // Defined in utils.cpp

  bool readyToSendSubtitles() const;  // Defined in utils.cpp

  uint32_t rtpFps;
  TransportType transport;
  uint32_t sampleRate;
  int rtspPort;
  IPAddress rtpIp;
  uint8_t rtpTTL;
  uint16_t rtpVideoPort;
  uint16_t rtpAudioPort;
  uint16_t rtpSubtitlesPort;
  uint8_t maxRTSPClients;

private:
  int rtspSocket;
  int videoUnicastSocket; 
  int audioUnicastSocket; 
  int subtitlesUnicastSocket; 
  int videoMulticastSocket; 
  int audioMulticastSocket; 
  int subtitlesMulticastSocket;
  uint8_t activeRTSPClients; 
  uint8_t maxClients;
  TaskHandle_t rtpVideoTaskHandle;
  TaskHandle_t rtspTaskHandle;
  std::map<uint32_t, RTSP_Session> sessions;
  byte* rtspStreamBuffer;
  size_t rtspStreamBufferSize;
  bool rtpFrameSent;
  bool rtpAudioSent;
  bool rtpSubtitlesSent;
  uint8_t vQuality;
  uint16_t vWidth;
  uint16_t vHeight;
  uint16_t videoSequenceNumber;
  uint32_t videoTimestamp;
  uint32_t videoSSRC;
  uint16_t audioSequenceNumber;
  uint32_t audioTimestamp;
  uint32_t audioSSRC;
  uint16_t subtitlesSequenceNumber;
  uint32_t subtitlesTimestamp;
  uint32_t subtitlesSSRC;
  uint32_t rtpFrameCount;
  uint32_t lastRtpFPSUpdateTime;
  uint8_t videoCh;
  uint8_t audioCh;
  uint8_t subtitlesCh;
  bool isVideo;
  bool isAudio;
  bool isSubtitles;
  bool isPlaying;
  bool firstClientConnected; 
  bool firstClientIsMulticast; 
  bool firstClientIsTCP;
  esp_timer_handle_t sendSubtitlesTimer;
  SemaphoreHandle_t isPlayingMutex;  // Mutex for protecting access
  SemaphoreHandle_t sendTcpMutex;  // Mutex for protecting TCP send access
  SemaphoreHandle_t maxClientsMutex; // FreeRTOS mutex for maxClients

  void closeSockets();  // Defined in ESP32-RTSPServer.cpp
  
  void sendTcpPacket(const uint8_t* packet, size_t packetSize, int sock);  // Defined in network.cpp

  void checkAndSetupUDP(int& rtpSocket, bool isMulticast, uint16_t rtpPort, IPAddress rtpIp = IPAddress());  // Defined in network.cpp

  void sendRtpSubtitles(const char* data, size_t len, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP, bool isMulticast);  // Defined in rtp.cpp

  void sendRtpAudio(const int16_t* data, size_t len, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP, bool isMulticast);  // Defined in rtp.cpp

  void sendRtpFrame(const uint8_t* data, size_t len, uint8_t quality, uint16_t width, uint16_t height, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP, bool isMulticast);  // Defined in rtp.cpp

  static void rtpVideoTaskWrapper(void* pvParameters);  // Defined in rtp.cpp

  void rtpVideoTask();  // Defined in rtp.cpp

  void setMaxClients(uint8_t newMaxClients);  // Defined in utils.cpp

  uint8_t getMaxClients();  // Defined in utils.cpp

  uint8_t getActiveClients();  // Defined in utils.cpp
  
  void incrementActiveRTSPClients();  // Defined in utils.cpp

  void decrementActiveRTSPClients();  // Defined in utils.cpp

  uint8_t getActiveRTSPClients();  // Defined in utils.cpp

  void updateIsPlayingStatus();  // Defined in utils.cpp
  
  void setIsPlaying(bool playing);  // Defined in utils.cpp
  
  bool getIsPlaying() const;  // Defined in utils.cpp

  int captureCSeq(char* request);  // Defined in utils.cpp

  uint32_t generateSessionID();  // Defined in utils.cpp

  uint32_t extractSessionID(char* request);  // Defined in utils.cpp

  const char* dateHeader();  // Defined in utils.cpp

  void handleOptions(char* request, RTSP_Session& session);  // Defined in rtsp_requests.cpp

  void handleDescribe(const RTSP_Session& session);  // Defined in rtsp_requests.cpp

  void handleSetup(char* request, RTSP_Session& session);  // Defined in rtsp_requests.cpp

  void handlePlay(RTSP_Session& session);  // Defined in rtsp_requests.cpp

  void handlePause(RTSP_Session& session);  // Defined in rtsp_requests.cpp

  void handleTeardown(RTSP_Session& session);  // Defined in rtsp_requests.cpp

  bool handleRTSPRequest(RTSP_Session& session);  // Defined in rtsp_requests.cpp

  bool setNonBlocking(int sockfd);  // Defined in network.cpp

  bool prepRTSP();  // Defined in ESP32-RTSPServer.cpp

  static void rtspTaskWrapper(void* pvParameters);  // Defined in ESP32-RTSPServer.cpp

  void rtspTask();  // Defined in ESP32-RTSPServer.cpp

  static const char* LOG_TAG;  // Define a log tag for the class
};

#endif // ESP32_RTSP_SERVER_H