#ifndef DAEMON_H
#define DAEMON_H

#include <string>
#include <csignal>
#include <atomic>

// Uncomment the following line to force portable mode (kill 0) even on Linux
// #define FORCE_PORTABLE_MODE

class Daemon {
public:
    static Daemon& getInstance();

    // Deleted copy constructor and assignment operator
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    void run(const std::string& configPath);
    static void signalHandler(int signal);

private:
    Daemon();
    ~Daemon();

    void daemonize();
    void loadConfig();
    void checkPidFile();
    void writePidFile();
    void doWork();
    long getDirectorySize(const std::string& path);
    void clearDirectory(const std::string& path);
    
    // Helper to check process existence depending on OS/Configuration
    bool processExists(int pid);

    std::string configPath;
    std::string folder1;
    std::string folder2;
    int interval;
    std::string pidFilePath;

    static std::atomic<bool> m_running;
    static std::atomic<bool> m_reloadConfig;
};

#endif // DAEMON_H
