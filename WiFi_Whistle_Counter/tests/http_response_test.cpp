#include "../firmware/Whistle_counter_esp8266_wifi_multimode/HttpResponse.h"
#include <assert.h>
#include <string>
#include <iostream>

static HttpResponse parse(const std::string &text, bool close = false) {
  HttpResponse parser;
  for (char c : text) parser.consume(c);
  if (close) parser.disconnected();
  return parser;
}

int main() {
  auto ok = parse("HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: keep-alive\r\n\r\n{\"ok\":true}");
  assert(ok.complete() && !ok.failed() && ok.statusCode() == 200 && !ok.closeAfter());
  auto chunks = parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6;ext=value\r\n world\r\n0\r\nX-Final: yes\r\n\r\n");
  assert(chunks.complete() && !chunks.failed());
  assert(parse("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort", true).failed());
  auto closed = parse("HTTP/1.0 200 OK\r\n\r\nbody", true);
  assert(closed.complete() && closed.closeAfter());
  assert(parse("HTTP/1.1 204 No Content\r\n\r\n").complete());
  assert(parse("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n").failed());
  assert(parse("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n").failed());
  assert(parse("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n").failed());
  assert(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nz\r\n").failed());
  assert(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naX").failed());
  assert(parse("HTTP/1.1 200 OK\r\nLong: " + std::string(300, 'a')).failed());
  assert(parse("HTTP/1.1 200 OK\r\nContent-Length: 2049\r\n\r\n").failed());
  auto denied = parse("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
  assert(denied.complete() && denied.statusCode() == 401);
  auto interim = parse("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  assert(interim.complete() && interim.statusCode() == 200);
  auto reset = ok;
  reset.reset();
  assert(!reset.complete() && !reset.failed() && reset.statusCode() == 0);
  std::cout << "15 HTTP response framing checks passed\n";
}
