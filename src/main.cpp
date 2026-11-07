#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main() {
    std::string apiKey;
    bool apiKeyLoaded = false;
    if (const char* apiKeyFile = std::getenv("API_KEY_FILE")) {
        std::ifstream secretFile(apiKeyFile);
        if (!secretFile || !std::getline(secretFile, apiKey) || apiKey.empty()) {
            std::cerr << "Unable to read a non-empty API key from the configured file." << std::endl;
            return 1;
        }
        apiKeyLoaded = true;
    }

    std::cout << "==========================================" << std::endl;
    std::cout << " C++ Microservice Started Successfully!   " << std::endl;
    std::cout << "==========================================" << std::endl;
    if (apiKeyLoaded) {
        std::cout << "API key loaded from configured file." << std::endl;
    }

    unsigned int iteration = 0;
    while (true) {
        std::cout << "[Heartbeat #" << ++iteration << "] Service running normally..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return 0;
}