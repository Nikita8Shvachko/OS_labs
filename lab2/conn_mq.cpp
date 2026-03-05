#include "conn_interface.hpp"
#include "ipc_utils.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mqueue.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string queue_name(const std::string &dir, int id) {
  std::ostringstream oss;
  oss << "/" << dir << "_" << id;
  return oss.str();
}

class ConnMQ : public IConn {
public:
  ConnMQ(int id, bool is_host) : id_(id), owner_(is_host) {
    up_name_ = queue_name("chat_mq_up", id_);
    down_name_ = queue_name("chat_mq_down", id_);

    mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(ChatMessage);

    if (owner_) {
      mq_unlink(up_name_.c_str());
      mq_unlink(down_name_.c_str());
    }

    mqd_t up = mq_open(up_name_.c_str(), owner_ ? (O_CREAT | O_RDWR) : O_RDWR,
                       0666, owner_ ? &attr : nullptr);
    if (up == (mqd_t)-1) {
      std::ostringstream oss;
      oss << "mq_open(" << up_name_ << ") failed: " << strerror(errno);
      log_line("MQ", oss.str());
    }

    mqd_t down =
        mq_open(down_name_.c_str(), owner_ ? (O_CREAT | O_RDWR) : O_RDWR, 0666,
                owner_ ? &attr : nullptr);
    if (down == (mqd_t)-1) {
      std::ostringstream oss;
      oss << "mq_open(" << down_name_ << ") failed: " << strerror(errno);
      log_line("MQ", oss.str());
    }

    if (is_host) {
      read_queue_ = up;
      write_queue_ = down;
    } else {
      read_queue_ = down;
      write_queue_ = up;
    }
  }

  ~ConnMQ() override {
    if (read_queue_ != static_cast<mqd_t>(-1)) {
      mq_close(read_queue_);
    }
    if (write_queue_ != static_cast<mqd_t>(-1)) {
      mq_close(write_queue_);
    }
    if (owner_) {
      mq_unlink(up_name_.c_str());
      mq_unlink(down_name_.c_str());
    }
  }

  bool Write(const ChatMessage &msg) override {
    if (write_queue_ == static_cast<mqd_t>(-1)) {
      return false;
    }
    timespec ts = deadline_after(WAIT_TIMEOUT_SEC);
    if (mq_timedsend(write_queue_, reinterpret_cast<const char *>(&msg),
                     sizeof(ChatMessage), 0, &ts) != 0) {
      std::ostringstream oss;
      oss << "mq_timedsend failed: " << strerror(errno);
      log_line("MQ", oss.str());
      return false;
    }
    return true;
  }

  ReadResult Read(ChatMessage &msg) override {
    if (read_queue_ == static_cast<mqd_t>(-1)) {
      return ReadResult::Error;
    }
    timespec ts = deadline_after(WAIT_TIMEOUT_SEC);
    ssize_t r = mq_timedreceive(read_queue_, reinterpret_cast<char *>(&msg),
                                sizeof(ChatMessage), nullptr, &ts);
    if (r == -1) {
      if (errno == ETIMEDOUT) {
        return ReadResult::Timeout;
      }
      std::ostringstream oss;
      oss << "mq_timedreceive failed: " << strerror(errno);
      log_line("MQ", oss.str());
      return ReadResult::Error;
    }
    if (r != static_cast<ssize_t>(sizeof(ChatMessage))) {
      log_line("MQ", "mq_timedreceive: wrong message size");
      return ReadResult::Error;
    }
    return ReadResult::Success;
  }

private:
  int id_{};
  bool owner_{};
  std::string up_name_;
  std::string down_name_;
  mqd_t read_queue_{-1};
  mqd_t write_queue_{-1};
};

} // namespace

std::unique_ptr<IConn> create_connection(int id, bool is_host) {
  return std::make_unique<ConnMQ>(id, is_host);
}

std::string connection_type_code() { return "mq"; }
