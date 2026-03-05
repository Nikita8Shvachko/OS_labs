#include "common.hpp"
#include "conn_interface.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

struct ClientCtx {
  int id{};
  pid_t pid{};
  std::unique_ptr<IConn> conn;
  std::thread reader;
  std::atomic<bool> active{true};
  std::chrono::steady_clock::time_point last_activity;
};

std::atomic<bool> running{true};
std::atomic<int> next_client_id{1};
std::mutex clients_mu;
std::unordered_map<int, std::shared_ptr<ClientCtx>> clients;

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

void send_message(const ChatMessage &msg) {
  std::vector<std::shared_ptr<ClientCtx>> targets;
  {
    std::lock_guard<std::mutex> lk(clients_mu);
    if (msg.to == CHAT_MSG_BROADCAST_ID) {
      for (auto &kv : clients) {
        targets.push_back(kv.second);
      }
    } else if (msg.to == CHAT_MSG_HOST_ID) {
      if (msg.from > 0) {
        auto it_from = clients.find(msg.from);
        if (it_from != clients.end()) {
          targets.push_back(it_from->second);
        }
      }
    } else {
      auto it = clients.find(msg.to);
      if (it != clients.end()) {
        targets.push_back(it->second);
      }
      if (msg.from != CHAT_MSG_HOST_ID && msg.from != msg.to) {
        auto it_from = clients.find(msg.from);
        if (it_from != clients.end()) {
          targets.push_back(it_from->second);
        }
      }
    }
  }

  for (auto &c : targets) {
    if (c->active.load()) {
      c->conn->Write(msg);
    }
  }
}

void drop_client(int id) {
  std::shared_ptr<ClientCtx> ctx;
  {
    std::lock_guard<std::mutex> lk(clients_mu);
    auto it = clients.find(id);
    if (it != clients.end()) {
      ctx = it->second;
      clients.erase(it);
    }
  }
  if (ctx) {
    ctx->active.store(false);
    if (ctx->reader.get_id() == std::this_thread::get_id()) {
      ctx->reader.detach();
    } else if (ctx->reader.joinable()) {
      ctx->reader.join();
    }
    log_line("HOST", "client " + std::to_string(id) + " disconnected");
  }
}

void reader_loop(std::shared_ptr<ClientCtx> ctx) {
  ChatMessage msg{};
  while (running.load() && ctx->active.load()) {
    ReadResult res = ctx->conn->Read(msg);
    if (res == ReadResult::Timeout) {
      continue;
    }
    if (res == ReadResult::Error) {
      drop_client(ctx->id);
      break;
    }

    if (msg.type == MsgType::DISCONNECT) {
      log_line("HOST",
               "Client " + std::to_string(ctx->id) + " sent disconnect.");
      drop_client(ctx->id);
      break;
    }

    ctx->last_activity = std::chrono::steady_clock::now();
    if (msg.to == CHAT_MSG_BROADCAST_ID) {
      log_line("CHAT",
               "[" + std::to_string(msg.from) + "] -> [all] " + msg.text);
    } else {
      log_line("CHAT", "[" + std::to_string(msg.from) + "] -> [" +
                           std::to_string(msg.to) + "] " + msg.text);
    }
    send_message(msg);
  }
  drop_client(ctx->id);
}

void input_loop() {
  std::string line;
  while (running.load() && std::getline(std::cin, line)) {
    if (line == "/quit") {
      running.store(false);
      break;
    }
    ChatMessage msg = build_message_from_input(line, CHAT_MSG_HOST_ID);
    if (msg.to == CHAT_MSG_BROADCAST_ID) {
      log_line("CHAT", "[0] -> [all] " + std::string(msg.text));
    } else {
      log_line("CHAT", "[0] -> [" + std::to_string(msg.to) + "] " +
                           std::string(msg.text));
    }
    send_message(msg);
  }
}

void watchdog_loop() {
  while (running.load()) {
    for (int i = 0; i < 50; ++i) {
      if (!running.load())
        return;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto now = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<ClientCtx>> snapshot;
    {
      std::lock_guard<std::mutex> lk(clients_mu);
      for (auto &kv : clients) {
        snapshot.push_back(kv.second);
      }
    }
    for (auto &c : snapshot) {
      if (!c->active.load()) {
        continue;
      }
      auto idle = std::chrono::duration_cast<std::chrono::seconds>(
                      now - c->last_activity)
                      .count();
      if (idle > IDLE_KILL_SEC) {
        log_line("HOST",
                 "client " + std::to_string(c->id) + " idle, sending SIGKILL");
        kill(c->pid, SIGKILL);
        c->active.store(false);
      }
    }
  }
}

void handshake_loop(const sigset_t &mask) {
  while (running.load()) {
    siginfo_t info{};
    timespec ts = deadline_after(WAIT_TIMEOUT_SEC);
    int sig = sigtimedwait(&mask, &info, &ts);
    if (sig == HANDSHAKE_SIGNAL) {
      if (info.si_pid == getpid()) {
        continue;
      }
      pid_t client_pid = info.si_pid;
      int client_id = next_client_id.fetch_add(1);
      auto conn = create_connection(client_id, true);
      if (!conn) {
        log_line("HOST", "failed to create connection for client");
        continue;
      }
      union sigval sv{};
      sv.sival_int = client_id;
      if (sigqueue(client_pid, HANDSHAKE_SIGNAL, sv) != 0) {
        log_line("HOST", "sigqueue failed");
        continue;
      }
      auto ctx = std::make_shared<ClientCtx>();
      ctx->id = client_id;
      ctx->pid = client_pid;
      ctx->conn = std::move(conn);
      ctx->last_activity = std::chrono::steady_clock::now();
      ctx->reader = std::thread(reader_loop, ctx);
      {
        std::lock_guard<std::mutex> lk(clients_mu);
        clients[client_id] = ctx;
      }
      log_line("HOST", "client " + std::to_string(client_id) +
                           " connected pid=" + std::to_string(client_pid));
    }
  }
}

int main() {
  sigset_t set{};
  sigemptyset(&set);
  sigaddset(&set, HANDSHAKE_SIGNAL);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);

  log_line("HOST", "pid=" + std::to_string(getpid()) +
                       " type=" + connection_type_code());
  log_line("HOST", "send SIGUSR1 to pid above to join");

  std::thread handshaker(handshake_loop, set);
  std::thread watchdog(watchdog_loop);
  input_loop();

  running.store(false);

  kill(getpid(), HANDSHAKE_SIGNAL);

  handshaker.join();
  watchdog.join();

  std::vector<int> ids;
  {
    std::lock_guard<std::mutex> lk(clients_mu);
    for (auto &kv : clients) {
      ids.push_back(kv.first);
    }
  }

  for (int id : ids) {
    std::shared_ptr<ClientCtx> ctx;
    {
      std::lock_guard<std::mutex> lk(clients_mu);
      auto it = clients.find(id);
      if (it != clients.end()) {
        ctx = it->second;
        clients.erase(it);
      }
    }
    if (ctx) {
      bool was_active = ctx->active.load();
      ctx->active.store(false);
      if (ctx->reader.joinable()) {
        ctx->reader.detach();
      }

      ctx->conn.reset();

      if (was_active) {
        log_line("HOST",
                 "client " + std::to_string(id) + " disconnected (shutdown)");
      }
    }
  }
  return 0;
}
