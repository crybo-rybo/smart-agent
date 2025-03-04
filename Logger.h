#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <filesystem>

class Logger {
public:
    static Logger& getInstance();
    
    void init();
    void log(const std::string& message);
    void error(const std::string& message);
    
    ~Logger();
    
private:
    Logger();
    
    std::ofstream logFile;
    std::mutex logMutex;
    std::string logFilePath;
    bool initialized = false;
};

#define LOG(message) Logger::getInstance().log(message)
#define LOG_ERROR(message) Logger::getInstance().error(message) 