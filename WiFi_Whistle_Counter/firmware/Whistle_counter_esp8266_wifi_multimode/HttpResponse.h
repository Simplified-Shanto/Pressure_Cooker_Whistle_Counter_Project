#pragma once

// Incremental HTTP/1 response framing. No sockets, allocations, or blocking.
// The JSON reply is only an acknowledgement; its body is drained, not stored.
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

class HttpResponse {
 public:
  void reset() { *this = HttpResponse(); }
  bool complete() const { return state_ == State::DONE; }
  bool failed() const { return state_ == State::ERROR; }
  int statusCode() const { return status_; }
  bool closeAfter() const { return close_; }

  void disconnected() {
    if (state_ == State::UNTIL_CLOSE) state_ = State::DONE;
    else if (!complete()) state_ = State::ERROR;
  }

  void consume(char c) {
    if (complete() || failed()) return;
    if (state_ == State::FIXED || state_ == State::CHUNK || state_ == State::UNTIL_CLOSE) {
      if (++bodyBytes_ > 2048) { state_ = State::ERROR; return; }
      if (state_ != State::UNTIL_CLOSE && --remaining_ == 0)
        state_ = state_ == State::FIXED ? State::DONE : State::CHUNK_CR;
      return;
    }
    if (state_ == State::CHUNK_CR) {
      state_ = c == '\r' ? State::CHUNK_LF : State::ERROR;
      return;
    }
    if (state_ == State::CHUNK_LF) {
      state_ = c == '\n' ? State::CHUNK_SIZE : State::ERROR;
      return;
    }
    if (++headerBytes_ > 4096) { state_ = State::ERROR; return; }
    if (c == '\r') return;
    if (c != '\n') {
      if (used_ >= sizeof(line_) - 1) { state_ = State::ERROR; return; }
      line_[used_++] = c;
      return;
    }
    line_[used_] = 0;
    processLine();
    used_ = 0;
  }

 private:
  enum class State { STATUS, HEADERS, FIXED, UNTIL_CLOSE, CHUNK_SIZE, CHUNK,
                     CHUNK_CR, CHUNK_LF, TRAILERS, DONE, ERROR };
  State state_ = State::STATUS;
  char line_[256] = {};
  unsigned used_ = 0, headerBytes_ = 0, bodyBytes_ = 0;
  uint32_t remaining_ = 0, contentLength_ = 0;
  int status_ = 0;
  bool chunked_ = false, hasLength_ = false, close_ = false;

  bool number(char *value, int base, uint32_t &out) {
    if (!*value || *value == '-' || *value == '+' || *value == ' ') return false;
    char *end;
    unsigned long parsed = strtoul(value, &end, base);
    if (*end || parsed > 2048) return false;
    out = static_cast<uint32_t>(parsed);
    return true;
  }

  void processLine() {
    if (state_ == State::STATUS) {
      int major = 0, minor = 0;
      if (sscanf(line_, "HTTP/%d.%d %d", &major, &minor, &status_) != 3 ||
          major != 1 || status_ < 100 || status_ > 599) {
        state_ = State::ERROR; return;
      }
      close_ = minor == 0;
      state_ = State::HEADERS;
    } else if (state_ == State::HEADERS) {
      if (!used_) {
        if (status_ < 200) { state_ = State::STATUS; chunked_ = hasLength_ = false; return; }
        if (chunked_ && hasLength_) { state_ = State::ERROR; return; }
        if (status_ == 204 || status_ == 304) state_ = State::DONE;
        else if (chunked_) state_ = State::CHUNK_SIZE;
        else if (hasLength_) { remaining_ = contentLength_; state_ = remaining_ ? State::FIXED : State::DONE; }
        else { state_ = State::UNTIL_CLOSE; close_ = true; }
        return;
      }
      char *colon = strchr(line_, ':');
      if (!colon) { state_ = State::ERROR; return; }
      *colon = 0;
      char *value = colon + 1;
      while (*value == ' ' || *value == '\t') ++value;
      char *end = value + strlen(value);
      while (end > value && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
      if (!strcasecmp(line_, "Content-Length")) {
        uint32_t parsed;
        if (!number(value, 10, parsed) || (hasLength_ && parsed != contentLength_)) { state_ = State::ERROR; return; }
        contentLength_ = parsed; hasLength_ = true;
      } else if (!strcasecmp(line_, "Transfer-Encoding")) {
        if (strcasecmp(value, "chunked")) state_ = State::ERROR;
        else chunked_ = true;
      } else if (!strcasecmp(line_, "Connection") && !strcasecmp(value, "close")) close_ = true;
    } else if (state_ == State::CHUNK_SIZE) {
      char *extension = strchr(line_, ';');
      if (extension) *extension = 0;
      if (!number(line_, 16, remaining_)) { state_ = State::ERROR; return; }
      state_ = remaining_ ? State::CHUNK : State::TRAILERS;
    } else if (state_ == State::TRAILERS && !used_) state_ = State::DONE;
  }
};
