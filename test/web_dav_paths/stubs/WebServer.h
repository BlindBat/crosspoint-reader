#pragma once

// Fake of the ESP32 Arduino WebServer with the surface WebDAVHandler uses.
// The fake is a plain recorder: tests set the request (uri/method/headers/
// body length), the production handler runs, and the test inspects the
// recorded status code, headers and streamed content.
//
// WebServer::urlDecode mirrors the ESP32 Arduino core implementation
// byte-for-byte (including the '+' -> ' ' conversion and the two-hex-digit
// strtol parse) so the decode+normalise composition under test behaves as it
// does on device.

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "HalStorage.h"
#include "WString.h"

#define CONTENT_LENGTH_UNKNOWN (static_cast<size_t>(-1))

enum HTTPMethod {
  HTTP_ANY,
  HTTP_GET,
  HTTP_HEAD,
  HTTP_POST,
  HTTP_PUT,
  HTTP_PATCH,
  HTTP_DELETE,
  HTTP_OPTIONS,
  HTTP_PROPFIND,
  HTTP_MKCOL,
  HTTP_MOVE,
  HTTP_COPY,
  HTTP_LOCK,
  HTTP_UNLOCK,
};

enum HTTPRawStatus {
  RAW_START,
  RAW_WRITE,
  RAW_END,
  RAW_ABORTED,
};

struct HTTPRaw {
  HTTPRawStatus status = RAW_START;
  size_t totalSize = 0;
  size_t currentSize = 0;
  uint8_t buf[1460] = {};
};

class WebServer;

class NetworkClient {
 public:
  explicit NetworkClient(std::string* sink) : sink_(sink) {}

  // The real handler streams a file body straight to the socket.
  size_t write(HalFile& file) {
    size_t total = 0;
    char buf[512];
    while (file.available() > 0) {
      const int n = file.read(buf, sizeof(buf));
      if (n <= 0) break;
      if (sink_) sink_->append(buf, static_cast<size_t>(n));
      total += static_cast<size_t>(n);
    }
    return total;
  }

 private:
  std::string* sink_;
};

class WebServer {
 public:
  // --- test control surface ---
  String requestUri;
  HTTPMethod requestMethod = HTTP_GET;
  std::map<std::string, String> requestHeaders;
  size_t requestContentLength = 0;

  // --- recorded response ---
  int statusCode = -1;
  String contentTypeSent;
  std::string bodySent;  // send() body + sendContent() chunks + client() writes
  std::vector<std::pair<std::string, std::string>> headersSent;
  size_t contentLengthSet = 0;

  // --- production-facing API ---
  String uri() const { return requestUri; }
  HTTPMethod method() const { return requestMethod; }
  size_t clientContentLength() const { return requestContentLength; }

  String header(const char* name) const {
    const auto it = requestHeaders.find(name);
    return it == requestHeaders.end() ? String() : it->second;
  }

  void send(int code) { statusCode = code; }
  void send(int code, const char* contentType, const char* body) {
    statusCode = code;
    contentTypeSent = contentType;
    bodySent += body ? body : "";
  }
  void send(int code, const char* contentType, const String& body) {
    statusCode = code;
    contentTypeSent = contentType;
    bodySent += body.str();
  }

  void sendHeader(const char* name, const char* value) { headersSent.emplace_back(name, value); }
  void sendHeader(const String& name, const String& value) { headersSent.emplace_back(name.str(), value.str()); }

  void sendContent(const String& content) { bodySent += content.str(); }
  void sendContent(const char* content) { bodySent += content ? content : ""; }

  void setContentLength(size_t length) { contentLengthSet = length; }

  NetworkClient client() { return NetworkClient(&bodySent); }

  // Mirrors ESP32 arduino-esp32 WebServer::urlDecode (Parsing.cpp).
  static String urlDecode(const String& text) {
    String decoded = "";
    char temp[] = "0x00";
    const unsigned int len = text.length();
    unsigned int i = 0;
    while (i < len) {
      char decodedChar;
      const char encodedChar = text.charAt(i++);
      if ((encodedChar == '%') && (i + 1 < len)) {
        temp[2] = text.charAt(i++);
        temp[3] = text.charAt(i++);
        decodedChar = static_cast<char>(strtol(temp, nullptr, 16));
      } else if (encodedChar == '+') {
        decodedChar = ' ';
      } else {
        decodedChar = encodedChar;
      }
      decoded += decodedChar;
    }
    return decoded;
  }
};

class RequestHandler {
 public:
  virtual ~RequestHandler() = default;
  virtual bool canHandle(WebServer& server, HTTPMethod method, const String& uri) = 0;
  virtual bool canRaw(WebServer& server, const String& uri) = 0;
  virtual void raw(WebServer& server, const String& uri, HTTPRaw& raw) = 0;
  virtual bool handle(WebServer& server, HTTPMethod method, const String& uri) = 0;
};

inline void yield() {}
