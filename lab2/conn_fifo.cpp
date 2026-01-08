#include "conn_interface.hpp"
#include "ipc_utils.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

std::string fifo_path(const std::string &dir, int id) {
  std::ostringstream oss;
  oss << "/tmp/" << dir << "_" << id;
  return oss.str();
}

class ConnFifo : public IConn {
public:
  ConnFifo(int id, bool is_host) : id_(id), owner_(is_host) {
    up_path_ = fifo_path("chat_fifo_up", id_);
    down_path_ = fifo_path("chat_fifo_down", id_);
    read_sem_name_ = make_sem_name("chat_fifo_sem_up", id_);
    write_sem_name_ = make_sem_name("chat_fifo_sem_down", id_);

    if (owner_) {
      mkfifo(up_path_.c_str(), 0666);
      mkfifo(down_path_.c_str(), 0666);
      read_sem_ = open_named_semaphore(read_sem_name_, true);
      write_sem_ = open_named_semaphore(write_sem_name_, true);
    } else {
      read_sem_ = open_named_semaphore(write_sem_name_, false);
      write_sem_ = open_named_semaphore(read_sem_name_, false);
    }
  }

  ~ConnFifo() override {
    if (read_fd_ >= 0) {
      ::close(read_fd_);
    }
    if (write_fd_ >= 0) {
      ::close(write_fd_);
    }
    if (owner_) {
      ::unlink(up_path_.c_str());
      ::unlink(down_path_.c_str());
    }
    close_named_semaphore(read_sem_name_, read_sem_, owner_);
    close_named_semaphore(write_sem_name_, write_sem_, owner_);
  }

  void ensure_connected() {
    if (read_fd_ >= 0 && write_fd_ >= 0)
      return;

    if (owner_) {
      if (read_fd_ < 0)
        read_fd_ = ::open(up_path_.c_str(), O_RDONLY);
      if (write_fd_ < 0)
        write_fd_ = ::open(down_path_.c_str(), O_WRONLY);
    } else {
      if (write_fd_ < 0)
        write_fd_ = ::open(up_path_.c_str(), O_WRONLY);
      if (read_fd_ < 0)
        read_fd_ = ::open(down_path_.c_str(), O_RDONLY);
    }
  }

  bool Write(const ChatMessage &msg) override {
    ensure_connected();
    if (write_fd_ < 0)
      return false;

    ssize_t written = ::write(write_fd_, &msg, sizeof(ChatMessage));
    if (written != static_cast<ssize_t>(sizeof(ChatMessage))) {
      std::ostringstream oss;
      oss << "fifo write failed: " << strerror(errno);
      log_line("FIFO", oss.str());
      return false;
    }
    if (write_sem_ != SEM_FAILED && write_sem_ != nullptr) {
      sem_post(write_sem_);
    }
    return true;
  }

  ReadResult Read(ChatMessage &msg) override {
    ensure_connected();
    if (read_fd_ < 0)
      return ReadResult::Error;

    int w = sem_wait_with_timeout(read_sem_);
    if (w == 1)
      return ReadResult::Timeout;
    if (w == -1)
      return ReadResult::Error;

    ssize_t r = ::read(read_fd_, &msg, sizeof(ChatMessage));
    if (r != static_cast<ssize_t>(sizeof(ChatMessage))) {
      std::ostringstream oss;
      oss << "fifo read failed: " << strerror(errno);
      log_line("FIFO", oss.str());
      return ReadResult::Error;
    }
    return ReadResult::Success;
  }

private:
  int id_{};
  bool owner_{};
  std::string up_path_;
  std::string down_path_;
  std::string read_sem_name_;
  std::string write_sem_name_;
  int read_fd_{-1};
  int write_fd_{-1};
  sem_t *read_sem_{SEM_FAILED};
  sem_t *write_sem_{SEM_FAILED};
};

} // namespace

std::unique_ptr<IConn> create_connection(int id, bool is_host) {
  return std::make_unique<ConnFifo>(id, is_host);
}

std::string connection_type_code() { return "fifo"; }
