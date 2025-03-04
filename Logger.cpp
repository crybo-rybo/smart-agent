#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    init();
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::init() {
    if (initialized) return;
    
    try {
        // Create logs directory if it doesn't exist
        std::filesystem::path logsDir = "logs";
        if (!std::filesystem::exists(logsDir)) {
            std::filesystem::create_directory(logsDir);
        }
        
        // Get current time for the log filename
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now = *std::localtime(&time_t_now);
        
        std::ostringstream filename;
        filename << "logs/log_" 
                 << std::put_time(&tm_now, "%Y-%m-%d_%H-%M-%S") 
                 << ".txt";
        
        logFilePath = filename.str();
        
        // Open the log file
        logFile.open(logFilePath, std::ios::out | std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file: " << logFilePath << std::endl;
            return;
        }
        
        // Write initial log entry
        logFile << "=== Log started at " 
                << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") 
                << " ===" << std::endl;
        
        initialized = true;
        
        // Log the initialization
        log("Logger initialized successfully");
    } catch (const std::exception& e) {
        std::cerr << "Error initializing logger: " << e.what() << std::endl;
    }
}

void Logger::log(const std::string& message) {
    if (!initialized) {
        init();
    }
    
    try {
        std::lock_guard<std::mutex> lock(logMutex);
        
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now = *std::localtime(&time_t_now);
        
        // Write to log file with timestamp
        logFile << "[" << std::put_time(&tm_now, "%H:%M:%S") << "] " 
                << message << std::endl;
        logFile.flush();
    } catch (const std::exception& e) {
        std::cerr << "Error writing to log: " << e.what() << std::endl;
    }
}

void Logger::error(const std::string& message) {
    if (!initialized) {
        init();
    }
    
    try {
        std::lock_guard<std::mutex> lock(logMutex);
        
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now = *std::localtime(&time_t_now);
        
        // Write to log file with timestamp and ERROR prefix
        logFile << "[" << std::put_time(&tm_now, "%H:%M:%S") << "] ERROR: " 
                << message << std::endl;
        logFile.flush();
    } catch (const std::exception& e) {
        std::cerr << "Error writing to log: " << e.what() << std::endl;
    }
} 