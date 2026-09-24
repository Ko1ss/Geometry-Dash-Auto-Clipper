#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <Geode/Geode.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include <chrono>

using namespace geode::prelude;

class OBSManager {
public:
    static OBSManager& get() {
        static OBSManager instance;
        return instance;
    }

    void init(const std::string& host = "127.0.0.1", int port = 4455, const std::string& password = "") {
        m_host = host;
        m_port = port;
        m_password = password;
        connectAsync();
    }

    void connectAsync() {
        if (m_connected || m_connecting) return;
        m_connecting = true;

        std::thread([this]() {
            connectPersistent();
            m_connecting = false;
        }).detach();
    }

    void connect() {
        connectAsync();
    }

    void disconnect() {
        m_connected = false;
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        WSACleanup();
    }

    bool isConnected() const {
        return m_connected.load();
    }

    void setConnected(bool connected) {
        m_connected = connected;
    }

    std::string getConnectionStatus() const {
        return m_connected ? "Connected to OBS WebSocket v5" : "Disconnected from OBS";
    }

    void startRecord(std::function<void(bool success)> callback = nullptr) {
        if (!m_connected) {
            if (callback) callback(false);
            return;
        }
        std::string request = "{\"op\":6,\"d\":{\"requestType\":\"StartRecord\",\"requestId\":\"gd_start_rec\"}}";
        sendWebSocketFrame(request);
        if (callback) callback(true);
    }

    void stopRecord(std::function<void(const std::string& outputPath)> callback = nullptr) {
        if (!m_connected) return;
        m_onRecordStopped = callback;
        std::string request = "{\"op\":6,\"d\":{\"requestType\":\"StopRecord\",\"requestId\":\"gd_stop_rec\"}}";
        sendWebSocketFrame(request);
    }

    void startReplayBuffer() {
        if (!m_connected) return;
        std::string request = "{\"op\":6,\"d\":{\"requestType\":\"StartReplayBuffer\",\"requestId\":\"gd_start_replay\"}}";
        sendWebSocketFrame(request);
    }

    void saveReplayBuffer(std::function<void(const std::string& clipPath)> callback = nullptr) {
        if (callback) {
            m_onReplaySaved = callback;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastReplaySaveTime).count();
        if (elapsedMs < 600) {
            return;
        }
        m_lastReplaySaveTime = now;

        if (!m_connected) {
            m_pendingReplaySave = true;
            connectAsync();
            return;
        }

        sendSaveReplayBufferRequest();
    }

    void sendSaveReplayBufferRequest() {
        std::string request = "{\"op\":6,\"d\":{\"requestType\":\"SaveReplayBuffer\",\"requestId\":\"gd_replay_save\"}}";
        sendWebSocketFrame(request);
    }

private:
    OBSManager() 
        : m_socket(INVALID_SOCKET), 
          m_connected(false), 
          m_connecting(false), 
          m_pendingReplaySave(false), 
          m_port(4455), 
          m_host("127.0.0.1") {}

    ~OBSManager() { disconnect(); }

    void connectPersistent() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            log::error("WSAStartup failed");
            return;
        }

        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        std::string portStr = std::to_string(m_port);
        if (getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &res) != 0) {
            WSACleanup();
            return;
        }

        m_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (m_socket == INVALID_SOCKET) {
            freeaddrinfo(res);
            WSACleanup();
            return;
        }

        if (::connect(m_socket, res->ai_addr, static_cast<int>(res->ai_addrlen)) == SOCKET_ERROR) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            freeaddrinfo(res);
            WSACleanup();
            return;
        }
        freeaddrinfo(res);

        std::string handshake = "GET / HTTP/1.1\r\n";
        handshake += "Host: " + m_host + ":" + portStr + "\r\n";
        handshake += "Upgrade: websocket\r\n";
        handshake += "Connection: Upgrade\r\n";
        handshake += "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
        handshake += "Sec-WebSocket-Version: 13\r\n\r\n";

        send(m_socket, handshake.c_str(), static_cast<int>(handshake.length()), 0);

        char buffer[2048] = {0};
        int bytes = recv(m_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0 || std::string(buffer).find("101") == std::string::npos) {
            log::error("OBS WebSocket handshake rejected!");
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return;
        }

        log::info("WebSocket upgrade successful with OBS v5!");

        std::string identify = "{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":1023}}";
        sendWebSocketFrame(identify);
        m_connected = true;

        if (m_pendingReplaySave.exchange(false)) {
            sendSaveReplayBufferRequest();
        }

        listenLoop();
    }

    void sendWebSocketFrame(const std::string& message) {
        if (m_socket == INVALID_SOCKET) return;

        std::vector<uint8_t> frame;
        frame.push_back(0x81);

        size_t len = message.size();
        if (len <= 125) {
            frame.push_back(static_cast<uint8_t>(0x80 | len));
        } else if (len <= 65535) {
            frame.push_back(static_cast<uint8_t>(0x80 | 126));
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
        }

        uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
        frame.insert(frame.end(), mask, mask + 4);

        for (size_t i = 0; i < len; ++i) {
            frame.push_back(static_cast<uint8_t>(message[i] ^ mask[i % 4]));
        }

        send(m_socket, reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()), 0);
    }

    void listenLoop() {
        std::vector<uint8_t> buffer(8192);
        while (m_connected && m_socket != INVALID_SOCKET) {
            int bytes = recv(m_socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()) - 1, 0);
            if (bytes <= 0) {
                if (m_connected) {
                    log::warn("OBS WebSocket disconnected.");
                    m_connected = false;
                }
                break;
            }

            uint8_t opcode = buffer[0] & 0x0F;
            if (opcode == 0x8) {
                m_connected = false;
                break;
            } else if (opcode == 0x1) {
                size_t payloadLen = buffer[1] & 0x7F;
                size_t offset = 2;
                if (payloadLen == 126 && bytes >= 4) {
                    payloadLen = (static_cast<uint8_t>(buffer[2]) << 8) | static_cast<uint8_t>(buffer[3]);
                    offset = 4;
                }
                if (offset + payloadLen <= static_cast<size_t>(bytes)) {
                    std::string payload(buffer.begin() + offset, buffer.begin() + offset + payloadLen);

                    if (payload.find("OBS_WEBSOCKET_OUTPUT_STARTED") != std::string::npos) {
                        std::string path = extractJsonString(payload, "outputPath");
                        if (!path.empty()) {
                            log::info("[GD Auto Clipper] OBS recording started -> writing to: {}", path);
                        }
                    } else if (payload.find("OBS_WEBSOCKET_OUTPUT_STOPPED") != std::string::npos || payload.find("gd_stop_rec") != std::string::npos) {
                        std::string path = extractJsonString(payload, "outputPath");
                        if (path.empty() || path == "null") {
                            path = extractJsonString(payload, "savedReplayPath");
                        }
                        if (!path.empty() && path != "null") {
                            log::info(">>> OBS RECORDING STOPPED. Saved to: {} <<<", path);
                            if (m_onRecordStopped) {
                                auto cb = m_onRecordStopped;
                                m_onRecordStopped = nullptr;
                                cb(path);
                            }
                        }
                    }

                    if (payload.find("ReplayBufferSaved") != std::string::npos || payload.find("savedReplayPath") != std::string::npos) {
                        std::string path = extractJsonString(payload, "savedReplayPath");
                        if (!path.empty() && path != "null") {
                            if (m_onReplaySaved) {
                                auto cb = m_onReplaySaved;
                                m_onReplaySaved = nullptr;
                                cb(path);
                            }
                        }
                    }
                }
            }
        }
    }

    std::string extractJsonString(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return "";

        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";

        pos = json.find('\"', pos);
        if (pos == std::string::npos) return "";

        size_t end = json.find('\"', pos + 1);
        if (end == std::string::npos) return "";

        std::string raw = json.substr(pos + 1, end - (pos + 1));
        std::string clean = "";
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && (i + 1) < raw.size()) {
                char next = raw[i + 1];
                if (next == '\\') {
                    clean.push_back('\\');
                    ++i;
                } else if (next == '/') {
                    clean.push_back('/');
                    ++i;
                } else {
                    clean.push_back(raw[i]);
                }
            } else {
                clean.push_back(raw[i]);
            }
        }
        return clean;
    }

    SOCKET m_socket;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_connecting;
    std::atomic<bool> m_pendingReplaySave;
    std::string m_host;
    int m_port;
    std::string m_password;
    std::function<void(const std::string&)> m_onReplaySaved;
    std::function<void(const std::string&)> m_onRecordStopped;
    std::chrono::steady_clock::time_point m_lastReplaySaveTime{std::chrono::steady_clock::time_point::min()};
};