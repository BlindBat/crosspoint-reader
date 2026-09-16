#pragma once

// Canned-response stand-in for the FreeInk SDK's freeink::SecureHttpClient.
// begin() applies the SDK's URL acceptance rule (scheme://host with an http or
// https scheme); each request records what the client would have sent and pops
// the next queued response. With nothing queued the request fails with -1, the
// SDK's transport-failure return.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace freeink {

struct HttpStubResponse {
  int status = -1;
  std::string body;
};

struct HttpStubExchange {
  std::string url;
  std::string method;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  bool insecure = false;
  bool ended = false;

  const std::string* header(const std::string& name) const {
    for (const auto& h : headers) {
      if (h.first == name) return &h.second;
    }
    return nullptr;
  }
};

namespace http_stub {

inline std::deque<HttpStubResponse>& responses() {
  static std::deque<HttpStubResponse> queue;
  return queue;
}

inline std::vector<HttpStubExchange>& exchanges() {
  static std::vector<HttpStubExchange> log;
  return log;
}

inline void reset() {
  responses().clear();
  exchanges().clear();
}

inline void enqueue(const int status, std::string body = "") {
  responses().push_back(HttpStubResponse{status, std::move(body)});
}

}  // namespace http_stub

class SecureHttpClient {
 public:
  void setInsecure() { insecure_ = true; }
  void setTimeout(uint32_t) {}
  void setReuse(bool) {}

  bool begin(const std::string& url) {
    headers_.clear();
    body_.clear();
    url_ = url;
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    std::string scheme = url.substr(0, schemeEnd);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const size_t hostStart = schemeEnd + 3;
    const size_t pathStart = url.find('/', hostStart);
    const std::string host =
        pathStart == std::string::npos ? url.substr(hostStart) : url.substr(hostStart, pathStart - hostStart);
    return !host.empty() && (scheme == "http" || scheme == "https");
  }

  void end() {
    if (exchangeIndex_ >= 0) http_stub::exchanges()[static_cast<size_t>(exchangeIndex_)].ended = true;
  }

  void addHeader(const std::string& name, const std::string& value) { headers_.emplace_back(name, value); }

  int GET() { return sendRequest("GET", ""); }

  int sendRequest(const char* method, const std::string& payload) {
    HttpStubExchange ex;
    ex.url = url_;
    ex.method = method;
    ex.body = payload;
    ex.headers = headers_;
    ex.insecure = insecure_;
    http_stub::exchanges().push_back(std::move(ex));
    exchangeIndex_ = static_cast<int>(http_stub::exchanges().size()) - 1;

    auto& queue = http_stub::responses();
    if (queue.empty()) {
      body_.clear();
      return -1;
    }
    const HttpStubResponse resp = std::move(queue.front());
    queue.pop_front();
    body_ = resp.body;
    return resp.status;
  }

  const std::string& getString() const { return body_; }

 private:
  std::string url_;
  std::string body_;
  std::vector<std::pair<std::string, std::string>> headers_;
  bool insecure_ = false;
  int exchangeIndex_ = -1;
};

}  // namespace freeink
