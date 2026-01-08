#include "ipc_utils.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

sem_t *open_named_semaphore(const std::string &name, bool create) {
  sem_t *sem = SEM_FAILED;
  if (create) {
    sem = sem_open(name.c_str(), O_CREAT | O_EXCL, 0666, 0);
    if (sem == SEM_FAILED && errno == EEXIST) {
      sem_unlink(name.c_str());
      sem = sem_open(name.c_str(), O_CREAT | O_EXCL, 0666, 0);
    }
  } else {
    sem = sem_open(name.c_str(), 0);
  }

  if (sem == SEM_FAILED) {
    std::ostringstream oss;
    oss << "sem_open(" << name << ") failed: " << strerror(errno);
    log_line("SEM", oss.str());
  }
  return sem;
}

void close_named_semaphore(const std::string &name, sem_t *sem, bool owner) {
  if (sem != SEM_FAILED && sem != nullptr) {
    sem_close(sem);
    if (owner) {
      sem_unlink(name.c_str());
    }
  }
}

// Returns: 0 on success, 1 on timeout, -1 on error
int sem_wait_with_timeout(sem_t *sem, int seconds) {
  if (sem == SEM_FAILED || sem == nullptr) {
    return -1;
  }
  timespec ts = deadline_after(seconds);
  while (true) {
    if (sem_timedwait(sem, &ts) == 0) {
      return 0;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == ETIMEDOUT) {
      return 1;
    }
    std::ostringstream oss;
    oss << "sem_timedwait failed: " << strerror(errno);
    log_line("SEM", oss.str());
    return -1;
  }
}

std::string make_sem_name(const std::string &prefix, int id) {
  std::ostringstream oss;
  oss << "/" << prefix << "_" << id;
  return oss.str();
}
