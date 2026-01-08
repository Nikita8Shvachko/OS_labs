#include "common.hpp"
#include "conn_interface.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <pthread.h>
#include <string>
#include <thread>

std::atomic<bool> running{true};

ChatMessage build_message_from_input(const std::string &line, int from_id) {
  ChatMessage msg{};
  msg.from = from_id;
  msg.to = CHAT_MSG_BROADCAST_ID;
  if (!line.empty() && line[0] == '@') {
    size_t space = line.find(' ');
    if (space != std::string::npos) {
      msg.to = std::stoi(line.substr(1, space - 1));
      fill_text(msg, line.substr(space + 1));
      return msg;
    }
  }
  fill_text(msg, line);
  return msg;
}

void reader_loop(std::shared_ptr<IConn> conn) {
  ChatMessage msg{};
  while (running.load()) {
    ReadResult res = conn->Read(msg);
    if (res == ReadResult::Timeout) {
      continue;
    }
    if (res == ReadResult::Error) {
      break;
    }
    if (msg.to == CHAT_MSG_BROADCAST_ID) {
      std::cout << "[" << msg.from << "] -> [all] " << msg.text << std::endl;
    } else {
      std::cout << "[" << msg.from << "] -> [" << msg.to << "] " << msg.text
                << std::endl;
    }
  }
  running.store(false);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <host_pid>" << std::endl;
    return 1;
  }
  pid_t host_pid = static_cast<pid_t>(std::stoi(argv[1]));

  sigset_t set{};
  sigemptyset(&set);
  sigaddset(&set, HANDSHAKE_SIGNAL);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);

  if (kill(host_pid, HANDSHAKE_SIGNAL) != 0) {
    std::cerr << "cannot send handshake to host" << std::endl;
    return 1;
  }
  siginfo_t info{};
  timespec ts = deadline_after(WAIT_TIMEOUT_SEC);
  int sig = sigtimedwait(&set, &info, &ts);
  if (sig != HANDSHAKE_SIGNAL) {
    std::cerr << "handshake timeout" << std::endl;
    return 1;
  }
  int my_id = info.si_value.sival_int;
  log_line("CLIENT", "connected as id " + std::to_string(my_id));

  auto conn = create_connection(my_id, false);
  if (!conn) {
    std::cerr << "failed to open connection" << std::endl;
    return 1;
  }
  auto shared_conn = std::shared_ptr<IConn>(std::move(conn));
  std::thread reader(reader_loop, shared_conn);

  std::string line;
  while (running.load() && std::getline(std::cin, line)) {
    if (line == "/quit") {
      ChatMessage msg{};
      msg.from = my_id;
      msg.type = MsgType::DISCONNECT;
      shared_conn->Write(msg);
      break;
    }
    ChatMessage msg = build_message_from_input(line, my_id);
    shared_conn->Write(msg);
  }
  running.store(false);

  if (reader.joinable()) {
    reader.detach();
  }
  return 0;
}
