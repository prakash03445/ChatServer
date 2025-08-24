#include "server.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    int port = 8080;

    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port number. Using default port 8080.\n";
            port = 8080;
        }
    }
    
    std::cout << "Starting server on port " << port << "...\n";
    Server server(port);
    server.start();

    return 0;
}