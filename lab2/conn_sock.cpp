#include "conn_interface.hpp"
#include "ipc_utils.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::string socket_path(int id) {
  std::ostringstream oss;
  oss << "/tmp/chat_sock_" << id << ".sock";
  return oss.str();
}

bool send_all(int fd, const void *data, size_t len) {
  const char *ptr = static_cast<const char *>(data);
  while (len > 0) {
    ssize_t w = ::send(fd, ptr, len, 0);
    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    ptr += w;
    len -= w;
  }
  return true;
}

class ConnSock : public IConn {
public:
  ConnSock(int id, bool is_host) : id_(id), owner_(is_host) {
    sem_name_up_ = make_sem_name("chat_sock_sem_up", id_);
    sem_name_down_ = make_sem_name("chat_sock_sem_down", id_);
    path_ = socket_path(id_);

    if (is_host) {
      read_sem_ = open_named_semaphore(sem_name_up_, true);
      write_sem_ = open_named_semaphore(sem_name_down_, true);
      setup_server();
    } else {
      read_sem_ = open_named_semaphore(sem_name_down_, false);
      write_sem_ = open_named_semaphore(sem_name_up_, false);
      connect_client();
    }
  }

  ~ConnSock() override {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
    }
    if (owner_) {
      ::unlink(path_.c_str());
    }
    close_named_semaphore(sem_name_up_, read_sem_, owner_);
    close_named_semaphore(sem_name_down_, write_sem_, owner_);
  }

  bool Write(const ChatMessage &msg) override {
    if (!ensure_connected()) {
      return false;
    }
    if (!send_all(fd_, &msg, sizeof(ChatMessage))) {
      std::ostringstream oss;
      oss << "socket send failed: " << strerror(errno);
      log_line("SOCK", oss.str());
      return false;
    }
    if (write_sem_ != SEM_FAILED && write_sem_ != nullptr) {
      sem_post(write_sem_);
    }
    return true;
  }

  ReadResult Read(ChatMessage &msg) override {
    if (!ensure_connected()) {
      return ReadResult::Error;
    }
    int w = sem_wait_with_timeout(read_sem_);
    if (w == 1)
      return ReadResult::Timeout;
    if (w == -1)
      return ReadResult::Error;

    ssize_t r = ::recv(fd_, &msg, sizeof(ChatMessage), MSG_WAITALL);
    if (r != static_cast<ssize_t>(sizeof(ChatMessage))) {
      std::ostringstream oss;
      oss << "socket recv failed: " << strerror(errno);
      log_line("SOCK", oss.str());
      return ReadResult::Error;
    }
    return ReadResult::Success;
  }

private:
  void setup_server() {
    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      log_line("SOCK", "socket create failed");
      return;
    }
    ::unlink(path_.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) !=
        0) {
      log_line("SOCK", "bind failed");
      ::close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
    if (listen(listen_fd_, 4) != 0) {
      log_line("SOCK", "listen failed");
      ::close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
  }

  void connect_client() {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
      log_line("SOCK", "socket create failed");
      return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

    int attempts = WAIT_TIMEOUT_SEC;
    while (attempts-- > 0) {
      if (::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
          0) {
        return;
      }
      ::sleep(1);
    }
    log_line("SOCK", "connect timed out");
  }

  bool ensure_connected() {
    if (fd_ >= 0) {
      return true;
    }
    if (owner_) {
      if (listen_fd_ < 0) {
        return false;
      }
      pollfd pfd{};
      pfd.fd = listen_fd_;
      pfd.events = POLLIN;
      int pr = ::poll(&pfd, 1, WAIT_TIMEOUT_SEC * 1000);
      if (pr <= 0) {
        log_line("SOCK", "accept timeout");
        return false;
      }
      fd_ = ::accept(listen_fd_, nullptr, nullptr);
      if (fd_ < 0) {
        log_line("SOCK", "accept failed");
        return false;
      }
      return true;
    }
    connect_client();
    return fd_ >= 0;
  }

  int id_{};
  bool owner_{};
  std::string sem_name_up_;
  std::string sem_name_down_;
  std::string path_;
  int fd_{-1};
  int listen_fd_{-1};
  sem_t *read_sem_{SEM_FAILED};
  sem_t *write_sem_{SEM_FAILED};
};

} // namespace

std::unique_ptr<IConn> create_connection(int id, bool is_host) {
  return std::make_unique<ConnSock>(id, is_host);
}

std::string connection_type_code() { return "sock"; }
