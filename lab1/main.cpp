#include "Daemon.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // Default config file name
    std::string configPath = "config.txt";

    // Allow passing config file as argument
    if (argc > 1) {
        configPath = argv[1];
    }

    Daemon::getInstance().run(configPath);

    return 0;
}

