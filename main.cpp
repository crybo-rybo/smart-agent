/**
 * @file main.cpp
 * @brief Entry point for the application, initializes and runs the main application instance.
 */
#include "Application.h"
#include "ModelManager.h"
#include <iostream>
#include <vector>
#include <string>

int main() {
    try {
        Application app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
