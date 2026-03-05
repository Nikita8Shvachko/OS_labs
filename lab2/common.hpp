#pragma once

#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

constexpr int CHAT_MSG_TEXT_SIZE = 256;
constexpr int WAIT_TIMEOUT_SEC = 5;
constexpr int IDLE_KILL_SEC = 60;
constexpr int HANDSHAKE_SIGNAL = SIGUSR1;

constexpr int CHAT_MSG_BROADCAST_ID = -1;
constexpr int CHAT_MSG_HOST_ID = 0;

enum class MsgType : int { TEXT = 0, DISCONNECT = 1 };

struct ChatMessage {
  MsgType type{MsgType::TEXT};
  int from{};
  int to{CHAT_MSG_BROADCAST_ID};
  char text[CHAT_MSG_TEXT_SIZE]{};
};

inline std::string now_string() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%F %T");
  return oss.str();
}

inline void log_line(const std::string &tag, const std::string &msg) {
  std::cerr << "[" << now_string() << "] [" << tag << "] " << msg << std::endl;
}

inline timespec deadline_after(int seconds) {
  timespec ts{};
#if defined(CLOCK_REALTIME)
  clock_gettime(CLOCK_REALTIME, &ts);
#else
  auto now = std::chrono::system_clock::now();
  auto s = std::chrono::time_point_cast<std::chrono::seconds>(now);
  ts.tv_sec = s.time_since_epoch().count();
  ts.tv_nsec =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now - s).count();
#endif
  ts.tv_sec += seconds;
  return ts;
}

inline void fill_text(ChatMessage &msg, const std::string &src) {
  std::memset(msg.text, 0, sizeof(msg.text));
  std::strncpy(msg.text, src.c_str(), sizeof(msg.text) - 1);
}
