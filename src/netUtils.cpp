#include "ESP32-RTSPServer.h"

void RTSPServer::checkAndSetupUDP(int& rtpSocket, bool isMulticast, uint16_t rtpPort, IPAddress rtpIp) {
  if (rtpSocket == -1) {
    rtpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (rtpSocket < 0) {
      RTSP_LOGE(LOG_TAG, "Failed to create RTP socket");
      return;
    }
    if (!setNonBlocking(rtpSocket)) {
      RTSP_LOGE(LOG_TAG, "Failed to set RTP socket to non-blocking mode.");
      close(rtpSocket);
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
        RTSP_LOGE(LOG_TAG, "Failed to bind RTP socket on port %d", rtpPort);
        return;
      }
    }
  }
}

void RTSPServer::sendTcpPacket(const uint8_t* packet, size_t packetSize, int sock) {
  if (xSemaphoreTake(sendTcpMutex, portMAX_DELAY) == pdTRUE) {
    ssize_t sent = 0;
    while (sent < packetSize) {
      ssize_t result = send(sock, packet + sent, packetSize - sent, 0);
      if (result < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
          fd_set write_fds;
          FD_ZERO(&write_fds);
          FD_SET(sock, &write_fds);
          struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; // 100ms
          int ret = select(sock + 1, NULL, &write_fds, NULL, NULL);
          if (ret <= 0) {
            RTSP_LOGE(LOG_TAG, "Failed to send TCP packet, select timeout or error");
            break;
          }
          continue;
        } else if (err != EPIPE && err != ECONNRESET && err != ENOTCONN && err != EBADF) {
          RTSP_LOGE(LOG_TAG, "Failed to send TCP packet, errno: %d", err);
          break;
        } else {
          break;
        }
      } else {
        sent += result;
      }
    }
    xSemaphoreGive(sendTcpMutex);
  } else {
    RTSP_LOGE(LOG_TAG, "Failed to acquire mutex");
  }
}

bool RTSPServer::setNonBlocking(int sock) { 
  int flags = fcntl(sock, F_GETFL, 0); 
  if (flags == -1) { 
    RTSP_LOGE(LOG_TAG, "Failed to get socket flags"); 
    return false; 
  } 
  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) { 
    RTSP_LOGE(LOG_TAG, "Failed to set socket to non-blocking mode"); 
    return false;
  } 
  RTSP_LOGI(LOG_TAG, "Socket set to non-blocking mode");
  return true;
}
