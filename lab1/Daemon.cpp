#include "Daemon.h"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

std::atomic<bool> Daemon::m_running(true);
std::atomic<bool> Daemon::m_reloadConfig(false);

Daemon::Daemon() : interval(30), pidFilePath("/tmp/daemon_lab1.pid") {}

Daemon::~Daemon() { closelog(); }

Daemon &Daemon::getInstance() {
  static Daemon instance;
  return instance;
}

void Daemon::signalHandler(int signal) {
  if (signal == SIGHUP) {
    m_reloadConfig = true;
  } else if (signal == SIGTERM) {
    m_running = false;
  }
}

void Daemon::loadConfig() {
  std::ifstream file(configPath);
  if (!file.is_open()) {
    syslog(LOG_ERR, "Failed to open config file: %s", configPath.c_str());
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    std::istringstream is_line(line);
    std::string key;
    if (std::getline(is_line, key, '=')) {
      std::string value;
      if (std::getline(is_line, value)) {
        if (key == "folder1")
          folder1 = value;
        else if (key == "folder2")
          folder2 = value;
        else if (key == "interval")
          interval = std::stoi(value);
      }
    }
  }
  syslog(LOG_INFO, "Config loaded. Interval: %d", interval);
}

bool Daemon::processExists(int pid) {
  // Linux specific requirement: check /proc filesystem
  std::string procPath = "/proc/" + std::to_string(pid);
  return fs::exists(procPath);
}

void Daemon::checkPidFile() {
  std::ifstream pidFile(pidFilePath);
  if (pidFile.is_open()) {
    int oldPid;
    pidFile >> oldPid;
    pidFile.close();

    if (processExists(oldPid)) {
      syslog(LOG_INFO, "Found existing daemon with PID %d. Sending SIGTERM.",
             oldPid);
      kill(oldPid, SIGTERM);
      // Give it to die...
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } else {
      // Process in PID file doesn't exist, we can overwrite it later (in
      // writePidFile) Maybe log this cleanup
      syslog(LOG_INFO,
             "Stale PID file found (PID %d), but process not running.", oldPid);
    }
  }
}

void Daemon::writePidFile() {
  std::ofstream pidFile(pidFilePath);
  if (pidFile.is_open()) {
    pidFile << getpid();
    pidFile.close();
  } else {
    syslog(LOG_ERR, "Failed to write PID file.");
  }
}

void Daemon::daemonize() {
  pid_t pid = fork();

  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  if (pid > 0) {
    exit(EXIT_SUCCESS); // Parent exits
  }

  if (setsid() < 0) {
    exit(EXIT_FAILURE);
  }

  // Fork again to prevent acquiring a controlling terminal
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  umask(0);

  // Change working directory to root to avoid blocking file systems
  chdir("/");

  // Close file descriptors
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  // Redirect to /dev/null
  open("/dev/null", O_RDONLY); // stdin
  open("/dev/null", O_WRONLY); // stdout
  open("/dev/null", O_WRONLY); // stderr

  openlog("lab1_daemon", LOG_PID, LOG_DAEMON);

#if defined(FORCE_PORTABLE_MODE) || defined(__APPLE__)
  syslog(LOG_INFO, "Daemon started (Portable Mode).");
#else
  syslog(LOG_INFO, "Daemon started (Linux /proc Mode).");
#endif
}

long Daemon::getDirectorySize(const std::string &path) {
  long size = 0;
  if (!fs::exists(path))
    return 0;

  // Use error_code to prevent exceptions if permission denied
  std::error_code ec;

  // If path is a file, just return its size
  if (!fs::is_directory(path, ec)) {
    return fs::file_size(path, ec);
  }

  for (const auto &entry : fs::recursive_directory_iterator(
           path, fs::directory_options::skip_permission_denied, ec)) {
    if (!fs::is_directory(entry.status(ec))) {
      size += fs::file_size(entry, ec);
    }
  }
  return size;
}

void Daemon::clearDirectory(const std::string &path) {
  if (!fs::exists(path))
    return;
  std::error_code ec;

  for (const auto &entry : fs::directory_iterator(
           path, fs::directory_options::skip_permission_denied, ec)) {
    fs::remove_all(entry.path(), ec);
  }
}

void Daemon::doWork() {
  if (folder1.empty() || folder2.empty()) {
    syslog(LOG_WARNING, "Folders not configured correctly.");
    return;
  }

  long size = getDirectorySize(folder1);

  // Get current time
  auto now = std::chrono::system_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);

  std::stringstream logEntry;
  logEntry << "Time: " << std::ctime(&now_c);
  std::string timeStr = logEntry.str();
  if (!timeStr.empty() && timeStr.back() == '\n')
    timeStr.pop_back();

  std::string logFilePath = folder2 + "/size.log";
  std::ofstream logFile(logFilePath, std::ios_base::app);
  if (logFile.is_open()) {
    logFile << "Time: " << timeStr << ", Size: " << size << " bytes"
            << std::endl;
    logFile.close();
  } else {
    syslog(LOG_ERR, "Failed to write to log file: %s", logFilePath.c_str());
  }

  clearDirectory(folder1);
  syslog(LOG_INFO, "Work done. Size %ld logged, folder cleared.", size);
}

void Daemon::run(const std::string &cfgPath) {
  //  absolute path for config
  try {
    configPath = fs::absolute(cfgPath).string();
  } catch (const std::filesystem::filesystem_error &ex) {
    configPath = cfgPath;
  }

  checkPidFile(); // Check and kill existing daemon if any
  daemonize();    // Become a daemon
  writePidFile(); // Write new PID

  // Setup signal handling
  signal(SIGHUP, signalHandler);
  signal(SIGTERM, signalHandler);

  loadConfig();

  while (m_running) {
    if (m_reloadConfig) {
      loadConfig();
      m_reloadConfig = false;
    }

    doWork();

    // Sleep for interval
    for (int i = 0; i < interval; ++i) {
      if (!m_running || m_reloadConfig)
        break;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  syslog(LOG_INFO, "Daemon stopping...");
  // Remove PID file
  fs::remove(pidFilePath);
  closelog();
}
