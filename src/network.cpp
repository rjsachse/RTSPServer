#include "ESP32-RTSPServer.h"

/**
 * @brief Sets up RTP UDP streaming.
 * 
 * @param rtpSocket Reference to the RTP socket.
 * @param isMulticast Indicates if multicast is used.
 * @param rtpPort The RTP port to use.
 * @param rtpIp The RTP IP address.
 */
 void RTSPServer::checkAndSetupUDP(int& rtpSocket, bool isMulticast, uint16_t rtpPort, IPAddress rtpIp) {
  if (rtpSocket == -1) {
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
}

/** 
 * @brief Sends a TCP packet.
 * @param packet Pointer to the packet data.
 * @param packetSize Size of the packet data.
 * @param sock Socket to send the packet through. 
 */
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
