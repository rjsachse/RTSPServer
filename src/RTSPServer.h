#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <WiFi.h>
#include "lwip/sockets.h"
#include <esp_log.h>
#include <map>
#include <Arduino.h>  // Include the Arduino header for String type

#define MAX_RTSP_BUFFER (512 * 1024)
#define RTP_STACK_SIZE (1024 * 4)
#define RTP_PRI 10
#define RTSP_STACK_SIZE (1024 * 8)
#define RTSP_PRI 10

/**
 * @brief Structure representing an RTSP session.
 */
struct RTSP_Session {
  uint32_t sessionID;
  IPAddress clientIP;
  uint16_t cVideoPort;
  uint16_t cAudioPort;
  uint16_t cSrtPort;
  int sock;
  int cseq;
  bool isMulticast;
  bool isPlaying;
  bool isTCP;
};

/**
 * @brief Class representing the RTSP Server.
 */
class RTSPServer {
public:
  /**
   * @brief Enumeration for transport types.
   */
  enum TransportType {
    NONE,
    VIDEO_ONLY,
    AUDIO_ONLY,
    SUBTITLES_ONLY,
    VIDEO_AND_AUDIO,
    VIDEO_AND_SUBTITLES,
    AUDIO_AND_SUBTITLES,
    VIDEO_AUDIO_SUBTITLES
  };

  RTSPServer();  // Default constructor
  ~RTSPServer();  // Destructor

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
  bool init(TransportType transport = VIDEO_ONLY, uint16_t rtspPort = 0, uint32_t sampleRate = 0, uint16_t port1 = 0, uint16_t port2 = 0, uint16_t port3 = 0, IPAddress rtpIp = IPAddress(), uint8_t rtpTTL = 255);
  
  /**
   * @brief Deinitialize the RTSP server. 
   */
  void deinit();

  /**
   * @brief Reinitialize the RTSP server. 
   * 
   * @return true if reinitialization was successful, false otherwise.
   */
  bool reinit();

  /**
   * @brief Sends an RTSP frame.
   * @param data Pointer to the frame data.
   * @param len Length of the frame data.
   * @param quality Quality of the frame.
   * @param width Width of the frame.
   * @param height Height of the frame.
   */
  void sendRTSPFrame(const uint8_t* data, size_t len, int quality, int width, int height);

  /**
   * @brief Sends RTSP audio data.
   * @param data Pointer to the audio data.
   * @param len Length of the audio data.
   */
  void sendRTSPAudio(int16_t* data, size_t len);

  /**
   * @brief Sends RTSP subtitles.
   * @param data Pointer to the subtitles data.
   * @param len Length of the subtitles data.
   */
  void sendRTSPSubtitles(char* data, size_t len);

  /**
   * @brief Checks if the server is ready to send a frame.
   * @return true if ready, false otherwise.
   */
  bool readyToSendFrame() const;

  /**
   * @brief Checks if the server is ready to send audio.
   * @return true if ready, false otherwise.
   */
  bool readyToSendAudio() const;

  /**
   * @brief Checks if the server is ready to send subtitles.
   * @return true if ready, false otherwise.
   */
  bool readyToSendSubtitles() const;

  uint32_t rtpFps;
  TransportType transport;
  uint32_t sampleRate;
  int rtspPort;
  IPAddress rtpIp;
  uint8_t rtpTTL;
  uint16_t rtpVideoPort;
  uint16_t rtpAudioPort;
  uint16_t rtpSubtitlesPort;

private:
  int rtspSocket;
  int videoRtpSocket;
  int audioRtpSocket;
  int subtitlesRtpSocket;
  int activeRTSPClients;
  SemaphoreHandle_t clientsMutex;  // Mutex for protecting access
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

  /**
   * @brief Sets up RTP streaming.
   * @param rtpSocket Reference to the RTP socket.
   * @param isMulticast Indicates if multicast is used.
   * @param rtpPort The RTP port to use.
   * @param rtpIp The RTP IP address.
   */
  void setupRTP(int& rtpSocket, bool isMulticast, uint16_t rtpPort, IPAddress rtpIp = IPAddress());

  /**
   * @brief Sends RTP subtitles.
   * @param data Pointer to the subtitles data.
   * @param len Length of the subtitles data.
   * @param sock Socket to use for sending.
   * @param clientIP Client IP address.
   * @param sendRtpPort RTP port to use for sending.
   * @param useTCP Indicates if TCP is used.
   */
  void sendRtpSubtitles(const char* data, size_t len, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP);

  /**
   * @brief Sends RTP audio.
   * @param data Pointer to the audio data.
   * @param len Length of the audio data.
   * @param sock Socket to use for sending.
   * @param clientIP Client IP address.
   * @param sendRtpPort RTP port to use for sending.
   * @param useTCP Indicates if TCP is used.
   */
  void sendRtpAudio(const int16_t* data, size_t len, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP);

  /**
   * @brief Sends an RTP frame.
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
  void sendRtpFrame(const uint8_t* data, size_t len, uint8_t quality, uint16_t width, uint16_t height, int sock, IPAddress clientIP, uint16_t sendRtpPort, bool useTCP);

  /**
   * @brief Task wrapper for RTP video.
   * @param pvParameters Task parameters.
   */
  static void rtpVideoTaskWrapper(void* pvParameters);

  /**
   * @brief Task for handling RTP video.
   */
  void rtpVideoTask();

  /**
   * @brief Task wrapper for RTSP.
   * @param pvParameters Task parameters.
   */
  static void rtspTaskWrapper(void* pvParameters);

  /**
   * @brief Increments the count of active clients.
   */
  void incrementActiveClients();

  /**
   * @brief Decrements the count of active clients.
   */
  void decrementActiveClients();

  /**
   * @brief Captures the CSeq from an RTSP request.
   * @param request The RTSP request.
   * @return The CSeq value.
   */
  int captureCSeq(String request);

  /**
   * @brief Generates a new session ID.
   * @return The generated session ID.
   */
  uint32_t generateSessionID();

  /**
   * @brief Extracts the session ID from an RTSP request.
   * @param request The RTSP request.
   * @return The extracted session ID.
   */
  uint32_t extractSessionID(const String& request);

  /**
   * @brief Generates the Date header for RTSP responses.
   * @return The Date header as a string.
   */
  const char* dateHeader();

  /**
   * @brief Handles the OPTIONS RTSP request.
   * @param request The RTSP request.
   * @param session The RTSP session.
   */
  void handleOptions(const String& request, RTSP_Session& session);

  /**
   * @brief Handles the DESCRIBE RTSP request.
   * @param session The RTSP session.
   */
  void handleDescribe(const RTSP_Session& session);

  /**
   * @brief Handles the SETUP RTSP request.
   * @param request The RTSP request.
   * @param session The RTSP session.
   */
  void handleSetup(const String& request, RTSP_Session& session);

  /**
   * @brief Handles the PLAY RTSP request.
   * @param session The RTSP session.
   */
  void handlePlay(RTSP_Session& session);

  /**
   * @brief Handles the PAUSE RTSP request.
   * @param session The RTSP session.
   */
  void handlePause(RTSP_Session& session);

  /**
   * @brief Handles the TEARDOWN RTSP request.
   * @param session The RTSP session.
   */
  void handleTeardown(RTSP_Session& session);

  /**
   * @brief Handles incoming RTSP requests.
   * @param sockfd The socket file descriptor.
   * @param clientAddr The client address.
   * @return true if the request was handled successfully, false otherwise.
   */
  bool handleRTSPRequest(int sockfd, struct sockaddr_in clientAddr);

  /**
   * @brief Sets a socket to non-blocking mode.
   * @param sockfd The socket file descriptor.
   */
  void setNonBlocking(int sockfd);

  /**
   * @brief Task for handling RTSP requests.
   */
  void rtspTask();

  /**
   * @brief Prepares the RTSP server for streaming.
   * @return true if the preparation is successful, false otherwise.
   */
  bool prepRTSP();

  static const char* LOG_TAG;  // Define a log tag for the class
};

#endif // RTSP_SERVER_H

