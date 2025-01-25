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
void RTSPServer::sendTcpPacket(const uint8_t* packet, size_t packetSize, int sock, mbedtls_ssl_context* ssl) {
  if (xSemaphoreTake(sendTcpMutex, portMAX_DELAY) == pdTRUE) {
    ssize_t sent = 0;
    while (sent < packetSize) {
      ssize_t result = useSecure ? mbedtls_ssl_write(ssl, packet + sent, packetSize - sent)
                                 : send(sock, packet + sent, packetSize - sent, 0);
      if (result < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
          fd_set write_fds;
          FD_ZERO(&write_fds);
          FD_SET(sock, &write_fds);
          //struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; // 100ms
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

bool RTSPServer::setCertificates(const char* cert, const char* key) {
  this->userCert = cert;
  this->userKey = key;
  this->useSecure = true;
  return true;
}

bool RTSPServer::acceptSecureClient(mbedtls_net_context& client_fd, mbedtls_ssl_context& ssl) {
  mbedtls_net_init(&client_fd);
  if (mbedtls_net_accept((mbedtls_net_context*)&this->rtspSocket, &client_fd, NULL, 0, NULL) != 0) {
    RTSP_LOGE(LOG_TAG, "Accept error");
    return false;
  }

  mbedtls_ssl_init(&ssl);
  if (mbedtls_ssl_setup(&ssl, &this->conf) != 0) {
    RTSP_LOGE(LOG_TAG, "Failed to set up SSL context.");
    mbedtls_net_free(&client_fd);
    return false;
  }

  mbedtls_ssl_set_bio(&ssl, &client_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

  if (mbedtls_ssl_handshake(&ssl) != 0) {
    RTSP_LOGE(LOG_TAG, "SSL handshake failed.");
    mbedtls_ssl_free(&ssl);
    mbedtls_net_free(&client_fd);
    return false;
  }

  return true;
}

bool RTSPServer::acceptNonSecureClient(int& client_sock) {
  struct sockaddr_in clientAddr;
  socklen_t addr_len = sizeof(clientAddr);

  client_sock = accept(this->rtspSocket, (struct sockaddr *)&clientAddr, &addr_len);
  if (client_sock < 0) {
    RTSP_LOGE(LOG_TAG, "Accept error");
    return false;
  }

  if (!setNonBlocking(client_sock)) {
    RTSP_LOGE(LOG_TAG, "Failed to set RTSP socket to non-blocking mode.");
    close(client_sock);
    return false;
  }

  return true;
}

void RTSPServer::createSession(int client_sock, mbedtls_net_context& client_fd, mbedtls_ssl_context& ssl, bool useSecure) {
  RTSP_Session session = {
    esp_random(),
    useSecure ? 0 : client_sock,
    ssl,
    client_fd,
    0,
    0,
    0,
    0,
    false,
    false,
    false
  };
  sessions[session.sessionID] = session;
}

void RTSPServer::sendErrorResponse(bool useSecure, mbedtls_net_context& client_fd, int client_sock) {
  const char* response = "RTSP/1.0 503 Service Unavailable\r\n\r\n";
  if (useSecure) {
    mbedtls_net_send(&client_fd, reinterpret_cast<const unsigned char*>(response), strlen(response));
    mbedtls_net_free(&client_fd);
  } else {
    write(client_sock, response, strlen(response));
    close(client_sock);
  }
  RTSP_LOGE(LOG_TAG, "Max clients reached. Sent 503 error to new client.");
}
